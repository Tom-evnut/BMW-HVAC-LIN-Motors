// APGDT001 bench runner for the BMW HVAC flap motors: polls the motors both ways in a loop and sends commands
// appended to a command file, logging every bus frame.
//   apgrun <baud> <out base path> <command file> <motors, e.g. 2,6,7,8> <slot ms>
// Writes <base>.csv (SavvyCAN native CSV, LIN flags column), <base>.txt (decoded) and <base>.raw.log (HID reports).
// Command file lines (only lines appended after start are read):
//   f <id> <hex bytes...>   master data frame, classic checksum     h <id>    header only
//   motors 2,6 | motors none                                        poll id|addr|both   (default both)
//   pause | resume          stop/restart the poll loop              mark <text>         note in the .txt log
//   quit
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

class Runner
{
    [DllImport("hid.dll")] static extern void HidD_GetHidGuid(out Guid g);
    [DllImport("hid.dll")] static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr p);
    [DllImport("hid.dll")] static extern bool HidD_FreePreparsedData(IntPtr p);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr p, out HIDP_CAPS caps);
    [StructLayout(LayoutKind.Sequential)] struct HIDP_CAPS
    {
        public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
        public ushort a, b, c, d, e, f, g, h, i, j;
    }
    [DllImport("setupapi.dll", SetLastError = true)] static extern IntPtr SetupDiGetClassDevs(ref Guid g, IntPtr e, IntPtr w, int flags);
    [DllImport("setupapi.dll", SetLastError = true)] static extern bool SetupDiEnumDeviceInterfaces(IntPtr s, IntPtr d, ref Guid g, int i, ref SP_DEVICE_INTERFACE_DATA data);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)] static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref SP_DEVICE_INTERFACE_DATA data, IntPtr detail, int size, out int required, IntPtr info);
    [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
    [StructLayout(LayoutKind.Sequential)] struct SP_DEVICE_INTERFACE_DATA { public int cbSize; public Guid g; public int flags; public IntPtr reserved; }
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);

    static Stopwatch clock = Stopwatch.StartNew();
    static TextWriter raw, txt, csv;
    static FileStream dev;
    static int inLen, outLen;
    static volatile bool running = true;
    static readonly object logLock = new object();
    static HashSet<int> masterIds = new HashSet<int> { 0x1B, 0x2C, 0x2E, 0x2F, 0x3C };
    static int lastReqAddr = -1;

    static string FindPath()
    {
        Guid g; HidD_GetHidGuid(out g);
        IntPtr set = SetupDiGetClassDevs(ref g, IntPtr.Zero, IntPtr.Zero, 0x12);
        try
        {
            for (int i = 0; ; i++)
            {
                var d = new SP_DEVICE_INTERFACE_DATA(); d.cbSize = Marshal.SizeOf(d);
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref g, i, ref d)) return null;
                int req; SetupDiGetDeviceInterfaceDetail(set, ref d, IntPtr.Zero, 0, out req, IntPtr.Zero);
                IntPtr buf = Marshal.AllocHGlobal(req);
                try
                {
                    Marshal.WriteInt32(buf, IntPtr.Size == 8 ? 8 : 6);
                    if (!SetupDiGetDeviceInterfaceDetail(set, ref d, buf, req, out req, IntPtr.Zero)) continue;
                    string path = Marshal.PtrToStringAuto(new IntPtr(buf.ToInt64() + 4));
                    if (path.ToLower().Contains("vid_04d8&pid_0a04")) return path;
                }
                finally { Marshal.FreeHGlobal(buf); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(set); }
    }

    static string Hex(IList<byte> b, int off, int n)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < n; i++) sb.Append(b[off + i].ToString("X2")).Append(i + 1 < n ? " " : "");
        return sb.ToString();
    }

    static double Now() { return clock.Elapsed.TotalMilliseconds; }

    static void Raw(string s) { lock (logLock) { raw.WriteLine(string.Format("{0,10:F3} {1}", Now(), s)); raw.Flush(); } }
    static void Txt(string s)
    {
        string line = string.Format("{0} {1,10:F1} {2}", DateTime.Now.ToString("HH:mm:ss.fff"), Now(), s);
        lock (logLock) { txt.WriteLine(line); txt.Flush(); }
        Console.WriteLine(line);
    }

    static void Send(byte[] report64, string what)
    {
        var buf = new byte[outLen];
        Array.Copy(report64, 0, buf, 1, Math.Min(report64.Length, outLen - 1));
        int n = report64.Length; while (n > 0 && report64[n - 1] == 0) n--;
        Raw("OUT " + what + ": " + Hex(report64, 0, Math.Max(n, 2)));
        dev.Write(buf, 0, buf.Length);
    }

    static byte[] Cmd(params byte[] bytes) { var r = new byte[64]; Array.Copy(bytes, r, bytes.Length); return r; }

    static byte[] ControlBlockReport(int baud)
    {
        var block = new byte[29];
        block[0] = 0xC0; block[3] = 10; block[4] = (byte)Math.Round(10 / 0.409);
        block[8] = 0x0A; block[9] = 0x33; block[10] = 0x20;
        block[16] = 0xC8; block[17] = 0x98; block[18] = 0x0F;
        int div = (5000000 + baud / 2) / baud - 1;
        block[22] = (byte)(div & 0xFF); block[23] = (byte)(div >> 8);
        var r = new byte[64]; r[0] = 0x02; Array.Copy(block, 0, r, 1, 29);
        return r;
    }

    static byte Pid(int id)
    {
        int p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1;
        int p1 = ~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 1;
        return (byte)(id | (p0 << 6) | (p1 << 7));
    }

    static byte Classic(IList<byte> data)
    {
        int s = 0;
        foreach (byte b in data) { s += b; if (s > 255) s -= 255; }
        return (byte)(~s & 0xFF);
    }

    static void SendHeader(int id) { Send(Cmd(0x03, 0x05, 0x84, 0x01, Pid(id), 0x1F, 0x77, 0x00), string.Format("header {0:X2}", id)); }

    static void SendFrame(int id, byte[] data)
    {
        int n = data.Length;
        var r = new byte[64];
        r[0] = 0x03; r[1] = (byte)(n + 6); r[2] = 0x84; r[3] = (byte)(n + 2); r[4] = Pid(id);
        Array.Copy(data, 0, r, 5, n);
        r[5 + n] = Classic(data); r[6 + n] = 0x1F; r[7 + n] = 0x77;
        Send(r, string.Format("frame {0:X2}", id));
        if (id == 0x1B && n > 0) lastReqAddr = data[0];
    }

    // ---- receive stream decoding (packets 86 <n> <tags>, ending at 80) ----
    static List<byte> stream = new List<byte>();
    static List<byte> cur = null;
    static long curTicks = -1, lastTicks = 0, rollovers = 0;

    static void Packet(byte[] b, int n)
    {
        int i = 1; // skip report ID
        while (i < n)
        {
            byte t = b[i];
            if (t == 0x80) break;
            if (t == 0x85 || t == 0x86 || t == 0x87)
            {
                int len = b[i + 1];
                for (int k = 0; k < len && i + 2 + k < n; k++) stream.Add(b[i + 2 + k]);
                i += 2 + len;
            }
            else if (t == 0x88) i += 2;
            else if (t == 0x81) i += 3;
            else if (t == 0x82) i += 25;
            else if (t == 0x83) i += 21;
            else if (t == 0x84) i += 7;
            else { Txt("unknown packet " + t.ToString("X2")); break; }
        }
        ParseStream();
    }

    static void ParseStream()
    {
        int i = 0;
        while (i < stream.Count)
        {
            byte t = stream[i];
            int need = (t == 0x10) ? 2 : (t == 0x13 || t == 0x85) ? 3 : (t == 0x1C || t == 0x82) ? 2 : 1;
            if (i + need > stream.Count) break;
            if (t == 0x83) { Finish(); cur = new List<byte>(); curTicks = -1; }
            else if (t == 0x10) { if (cur != null) cur.Add(stream[i + 1]); }
            else if (t == 0x13)
            {
                long v = stream[i + 1] | (stream[i + 2] << 8);
                lastTicks = v + rollovers * 65536;
                if (cur != null && curTicks < 0) curTicks = lastTicks;
            }
            else if (t == 0x14) rollovers++;
            else if (t == 0x1C) { }
            else if (t == 0x85) Txt("measured divisor " + (stream[i + 1] | stream[i + 2] << 8));
            else if (t == 0x82) Txt("status error " + stream[i + 1].ToString("X2"));
            else Txt("tag " + t.ToString("X2"));
            i += need;
        }
        stream.RemoveRange(0, i);
    }

    static void Finish()
    {
        if (cur == null) return;
        var b = cur; cur = null;
        long us = (long)Math.Round(curTicks * 409.0);
        if (b.Count == 0) { Txt("break, no PID"); return; }
        int pid = b[0], id = pid & 0x3F;
        var d = b.Skip(1).ToList();
        string dir = masterIds.Contains(id) ? "Tx" : "Rx";
        if (d.Count < 2)
        {
            csv.WriteLine(string.Format("{0},{1:X8},false,{2},0,0,00,00,00,00,00,00,00,00,LIN,", us, id, dir)); csv.Flush();
            Txt(string.Format("{0,9:F1} {1:X2} no response{2}", us / 1000.0, id, d.Count == 1 ? " (1 byte " + d[0].ToString("X2") + ")" : ""));
            return;
        }
        var data = d.Take(d.Count - 1).ToList(); byte cs = d[d.Count - 1];
        bool ok = Classic(data) == cs;
        var cols = new StringBuilder();
        for (int k = 0; k < 8; k++) cols.Append(k < data.Count ? data[k].ToString("X2") : "00").Append(',');
        csv.WriteLine(string.Format("{0},{1:X8},false,{2},0,{3},{4}LIN+CS={5:X2}{6},", us, id, dir, data.Count, cols, cs, ok ? "+CLASSIC" : ""));
        csv.Flush();
        string meaning = "";
        if (id >= 1 && id <= 0x0F && data.Count == 2) meaning = string.Format("M{0} pos {1:X4}", id, data[0] | data[1] << 8);
        else if (id == 0x23 && data.Count == 4)
            meaning = string.Format("addr {0:X2}{1} pos {2:X4} flags {3:X2}", data[0], data[0] == lastReqAddr ? "" : "(req " + lastReqAddr.ToString("X2") + ")", data[1] | data[2] << 8, data[3]);
        else if (id == 0x1B) meaning = "status request";
        else if (id == 0x2C) meaning = "SET_POSITION";
        else if (id == 0x2E) meaning = "LOCK_UNLOCK";
        else if (id == 0x2F) meaning = "MOVE";
        Txt(string.Format("{0,9:F1} {1:X2} {2} {3}{4} {5}", us / 1000.0, id, dir, Hex(data, 0, data.Count), ok ? "" : " BAD CS " + cs.ToString("X2"), meaning));
    }

    static void Reader()
    {
        var buf = new byte[inLen];
        while (running)
        {
            IAsyncResult ar;
            try { ar = dev.BeginRead(buf, 0, buf.Length, null, null); }
            catch (Exception e) { Txt("read error: " + e.Message); return; }
            while (running && !ar.AsyncWaitHandle.WaitOne(100)) { }
            if (!running) return;
            int n;
            try { n = dev.EndRead(ar); } catch (Exception e) { Txt("read error: " + e.Message); return; }
            int used = n; while (used > 1 && buf[used - 1] == 0) used--;
            Raw("IN  " + Hex(buf, 1, Math.Max(used - 1, 1)));
            lock (logLock) { Packet(buf, n); }
        }
    }

    static byte[] ParseBytes(IEnumerable<string> parts) { return parts.Select(p => Convert.ToByte(p, 16)).ToArray(); }

    static int Main(string[] args)
    {
        if (args.Length < 5) { Console.WriteLine("usage: apgrun <baud> <out base> <command file> <motors> <slot ms>"); return 1; }
        int baud = int.Parse(args[0]); string bas = args[1], cmdFile = args[2];
        var motors = args[3] == "none" ? new List<int>() : args[3].Split(',').Select(int.Parse).ToList();
        int slot = int.Parse(args[4]);
        string path = FindPath();
        if (path == null) { Console.WriteLine("APGDT001 not found"); return 1; }
        var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
        if (h.IsInvalid) { Console.WriteLine("open failed (in use by SavvyCAN?): " + Marshal.GetLastWin32Error()); return 1; }
        IntPtr pp; HidD_GetPreparsedData(h, out pp); HIDP_CAPS caps; HidP_GetCaps(pp, out caps); HidD_FreePreparsedData(pp);
        inLen = caps.InputReportByteLength; outLen = caps.OutputReportByteLength;
        dev = new FileStream(h, FileAccess.ReadWrite, Math.Max(inLen, outLen), true);
        raw = new StreamWriter(bas + ".raw.log"); txt = new StreamWriter(bas + ".txt"); csv = new StreamWriter(bas + ".csv");
        csv.WriteLine("Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8"); csv.Flush();
        Txt("start " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " args " + string.Join(" ", args) + "; frame times are the analyser's clock (ms)");

        if (!File.Exists(cmdFile)) File.WriteAllText(cmdFile, "");
        long cmdPos = new FileInfo(cmdFile).Length;

        var reader = new Thread(Reader); reader.Start();
        Send(Cmd(0x01, 0x00), "cold reset"); Thread.Sleep(100);
        Send(ControlBlockReport(baud), "config " + baud); Thread.Sleep(50);
        Send(Cmd(0x01, 0x01), "warm reset"); Thread.Sleep(50);
        Send(Cmd(0x01, 0x02), "status request"); Thread.Sleep(300);

        bool paused = false; string pollMode = "both";
        int mi = 0, phase = 0;
        var pending = new Queue<string>();
        string partial = "";
        while (running)
        {
            // read new command lines
            try
            {
                using (var fs = new FileStream(cmdFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    if (fs.Length > cmdPos)
                    {
                        fs.Seek(cmdPos, SeekOrigin.Begin);
                        var bytes = new byte[fs.Length - cmdPos]; int got = fs.Read(bytes, 0, bytes.Length); cmdPos += got;
                        partial += Encoding.ASCII.GetString(bytes, 0, got);
                        int nl;
                        while ((nl = partial.IndexOf('\n')) >= 0) { string l = partial.Substring(0, nl).Trim(); partial = partial.Substring(nl + 1); if (l.Length > 0) pending.Enqueue(l); }
                    }
                }
            }
            catch (IOException) { }

            bool sent = false;
            while (pending.Count > 0 && !sent)
            {
                string line = pending.Dequeue();
                var p = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                string c = p[0].ToLower();
                try
                {
                    if (c == "f") { lock (logLock) Txt("CMD " + line); SendFrame(Convert.ToInt32(p[1], 16), ParseBytes(p.Skip(2))); sent = true; }
                    else if (c == "h") { lock (logLock) Txt("CMD " + line); SendHeader(Convert.ToInt32(p[1], 16)); sent = true; }
                    else if (c == "motors") { motors = p[1] == "none" ? new List<int>() : p[1].Split(',').Select(int.Parse).ToList(); mi = 0; phase = 0; Txt("CMD " + line); }
                    else if (c == "poll") { pollMode = p[1]; phase = 0; Txt("CMD " + line); }
                    else if (c == "pause") { paused = true; Txt("CMD pause"); }
                    else if (c == "resume") { paused = false; Txt("CMD resume"); }
                    else if (c == "mark") Txt("MARK " + line.Substring(4).Trim());
                    else if (c == "quit") { Txt("CMD quit"); running = false; }
                    else Txt("unknown command: " + line);
                }
                catch (Exception e) { Txt("bad command '" + line + "': " + e.Message); }
            }
            if (!running) break;
            if (!sent && !paused && motors.Count > 0)
            {
                if (mi >= motors.Count) mi = 0;
                int m = motors[mi];
                bool doId = pollMode != "addr", doAddr = pollMode != "id";
                if (phase == 0 && !doId) phase = 1;
                if (phase == 0) { SendHeader(m); phase = doAddr ? 1 : 3; }
                else if (phase == 1) { SendFrame(0x1B, new byte[] { (byte)(0x20 | m), 0xFF }); phase = 2; }
                else { SendHeader(0x23); phase = 3; }
                if (phase == 3) { phase = 0; mi = (mi + 1) % motors.Count; }
                sent = true;
            }
            Thread.Sleep(slot);
        }
        Thread.Sleep(300);
        running = false;
        reader.Join(1000);
        lock (logLock) { Finish(); }
        Txt("stop");
        raw.Close(); txt.Close(); csv.Close(); dev.Close();
        return 0;
    }
}
