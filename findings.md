# BMW E90 HVAC flap motors: what the LIN values mean

Bench measurements on 2026-10-17 with a Microchip APGDT001 LIN analyser (9600 bit/s, classic checksum), with
four E90 flap motors (part 41475520, addresses 2, 6, 7, 8) loose on the bench on an adjustable 12 V supply. The
car logs in `Lin Logs/` were analysed alongside. The LDF (`LDF/BMW_HVAC_Flap_Motors.ldf`) has been updated to
match; it loads in SavvyCAN (fork-ci-lin) and python ldfparser.

## Summary

| Item | Finding |
|---|---|
| Position | 15 bits, 0x0000-0x7FFF, **relative**, ~12.8 counts per degree. Power-up value 0x3FFF. Bit 15 of commanded positions is ignored |
| Status frame (ID = motor) | bits 1-14 position (bit 0 always 0), bit 15 = **not referenced** |
| 0x23 byte 2 bit 7 | 0 on the first answer after a **LIN timeout**, else 1 (it isn't a position bit) |
| Status_Flags bit 0 (0x01) | **Standby**: set by Lock_Unlock 0xFFFD or undervoltage; Move and Set_Position ignored |
| bit 1 (0x02) | **Locked / drive disabled**: set at power-up, by Lock (0xFFFE), by undervoltage; Move ignored |
| bit 2 (0x04) | **Reset / supply event**, latched: power-up and undervoltage; cleared after one 0x23 read |
| bit 6 (0x40) | **Blocked** (car logs only): the motor stopped short of its target at a flap end stop |
| bit 7 (0x80) | **Not referenced**: no Set_Position since power-up |
| bits 3-5 | never seen. No flag shows that the motor is moving |
| Move_Speed | bit 3 → fast (~735 counts/s), else bit 4 or 5 → medium (~318), else normal (~240, also for 0x00) |
| Set_Position | takes the position, references the motor, clears Lock, stops a running move (even a fail-safe run) |
| Lock_Unlock | byte 1 bits 0-1 set flag bits 0-1: 0xFFFC/0x0000 unlock, 0xFFFD standby, 0xFFFE lock, 0xFFFF no change |
| 0x1B byte 1 | ignored (0x00-0xFF all give the same answer). The motor answers one 0x23 header per request |
| Device 0xB1 | not a flap motor; nothing on the bench answers it |
| LIN timeout | ~2.7 s without bus traffic → every motor drives on its own towards one end of its range |

## Tools and captures

Captures are in `Bench captures/` in BMW-HVAC-LIN-Motors (`test/lin-data/captures/bmw-hvac/` in the SavvyCAN
checkout): SavvyCAN native CSV with `LIN` flags, plus a decoded `.txt` with the same frames, the commands and
`MARK` notes. They were taken with `Tools/apgrun.cs`, a small Windows console program that drives the APGDT001
over HID (build with .NET Framework's csc.exe; usage at the top of the file). Times below are the analyser clock in ms, which is the CSV `Time Stamp` / 1000.

- `01-baseline`: polling only, then a 12 V power cycle while polling.
- `02-setpos-m2`: a restart of the probe (3 s quiet bus), a fail-safe run and the first Set_Position.
- `03-session`: everything else, each step introduced by a `MARK` line (marks are named below). The `MARK S..`
  series (speed tests, first attempt) sent nothing: a script bug made all 7 commands invalid and the probe
  rejected them ("bad command"). The `T..` marks are the repeat.
- `04-abs-rel`: the absolute/relative test.

Polling ran both ways in a loop: header on the motor's status ID, then 0x1B [0x20|m, FF], then a 0x23 header,
25 ms per slot (about 300 ms per round for 4 motors, 90 ms when only motor 2 was polled).

## 1. Status_Flags (0x23 byte 3)

### Power-up: 0x86, then 0x82

`01-baseline`, user power-cycled while polling: just before the supply fell every motor read **0x87**
(589640.2 ms: `23 Rx 22 00 80 87`), then no bus at all (the analyser's LIN supply is on the same 12 V), then
at 592039.8 ms the status frames read `FE BF` and the first 0x23 answers **0x86** (`22 FF BF 86`, 592114.6 ms),
the next ones **0x82** (592542.0 ms). Same in `03-session` (110711.0 ms 0x87, 117135.1 ms 0x86, 117517.6 ms 0x82) and
`04-abs-rel` (P0: 0x86 at 118235 ms, 0x82 from the next round).

### Bit 7 (0x80): not referenced

Cleared by Set_Position and only by it: `02-setpos-m2`, 7090.4 ms `2C Tx 22 00 C0 FF`, then 7463.0 ms
`23 Rx 22 .. .. 00` (was 0x82). Unlock alone clears bit 1 but leaves bit 7: `03-session` H1, 1133754.5 ms
`2E Tx 26 FC FF FF` → motor 6 flags **0x80**. Every power-up sets it again.

### Bit 1 (0x02): locked / drive disabled

- Lock sets it: L1, 526335.6 ms `2E Tx 22 FE FF FF` → `22 00 C2 02`.
- A move while locked is ignored: L2, 531412.5 ms `2F Tx 22 00 C1 05`, position stays 0xC200. Also E2,
  219633.4 ms: a move to motor 2 at power-up state (0x82) did nothing.
- Unlock clears it: L3, 536514.3 ms `2E Tx 22 FC FF FF` → flags 0x00; the earlier move is not remembered, the
  motor moves only on the next command (L4, 541775 ms onwards, 0xC200 → 0xC100).
- Set_Position clears it too: E1, `2C Tx 22 00 40 FF` while locked → flags 0x00; F2 then moved without Unlock.
- Lock stops a move at once: A1/A2, move 0xC700 → 0xC000 started, Lock at 720766.4 ms stopped it at 0xC58E; Unlock
  (A3) did not resume it.
- Undervoltage sets it (see bit 0); motor 2 got it back clear when the supply recovered.
- An unreferenced motor moves once unlocked: H2, 1136808.1 ms `2F Tx 26 FF 40 05` → motor 6 0x3FFF → 0x40FF with
  flags 0x80. So the reference is not needed to move.

### Bit 0 (0x01): standby

- Lock_Unlock 0xFFFD sets it: H4, 1142927.6 ms `2E Tx 22 FD FF FF` → `22 00 C0 01`.
- In this state a move **and** a Set_Position are both ignored: J2 (1416906.5 ms, move to 0xC100) and J3
  (1419964.2 ms, Set_Position 0xC100): position stays 0xC000, flags stay 0x01. Unlock 0xFFFC (J4) or 0x0000 (H6)
  clears it.
- Undervoltage sets it together with bits 1 and 2. With the supply turned down while polling (marks V12 ... V7):
  no change at 10, 9 and 8 V; at **7 V** motor 2 read **0x07** and motors 6/7/8 **0x87** (from 1066953.4 ms).
  Back at 12 V: motor 2 0x04 then 0x00, the others 0x86 then 0x82 (1084406.6-1085057.8 ms). So the undervoltage threshold is
  between 7 and 8 V, and a referenced motor stays referenced.

### Bit 2 (0x04): reset / supply event

Set at power-up and by undervoltage, and cleared after one 0x23 answer has carried it (the next answer, ~300 ms
later, is clear; see the times above). The car's device 0xB1 always reports 0x04.

### Bit 6 (0x40): blocked (car logs; not reproduced on the bench)

The bench motors are loose: the shafts can't be stopped by hand (K1: a hand hold didn't slow motor 2, which
counted on to 0xC700 at the normal rate with flags 0x00), and the gearbox can't be back-driven. So 0x40 was not
seen on the bench. In the car logs it shows every time the car drives a flap 0x58 counts past its known end and
the motor stops short:

| Log | Motor | Command | Report | Then |
|---|---|---|---|---|
| s-defrost on | 1 | 9.21 s `2F C305` (last target C2AD + 0x58) | 10.24 s C2E6 / **40** | 10.93 Lock → 10.99 C2E6/**02**; 11.70 Set_Position C281 → 00; 12.48 Unlock |
| s-defrost on | 5 | 9.41 s `2F BCC6` (BD1E - 0x58) | 10.48 s BCCB / **40** | Lock → 02; Set_Position BD4A; Unlock |
| s-toggle bottom | 1 | 2.28 s `2F BCF9` (BD51 - 0x58) | 2.95 s BD38 / **40** | Lock → 02; Set_Position BD7D; Unlock |
| s-toggle bottom | 5 | 2.48 s `2F C339` (C2E1 + 0x58) | 3.19 s C32E / **40** | Lock → 02; Set_Position C2B5; Unlock |
| s-toggle bottom | 4 | 6.34 s `2F B8AE 6F` | 12.25 s B893 / **40** | Lock → 02; Set_Position B8DA; Unlock |
| s-middle off | 3 | 4.49 s `2F C36A` (C312 + 0x58) | 5.20 s C33C / **40** | Lock → 02; Set_Position C2E6; Unlock |
| s-No buttons lit | 7 | 1.02 s `2F BDC2` | 4.57 s BDD6 / **40** | Lock → 02; Set_Position BDEE; Unlock |

So 0x40 = blocked / stalled before the target. Lock replaces it with 0x02, and the car then re-references the
motor: Set_Position with that flap's end value (motor 1: 0xBD7D and 0xC281, motor 5: 0xBD4A and 0xC2B5, motor 4:
0xB8DA, motor 3: 0xC2E6, motor 7: 0xBDEE). The car later commands exactly those end values. The motor drives at
the normal ~240 counts/s in all these moves (e.g. defrost on, motor 4: 0xAD counts per 0.72 s poll interval).

### What the flags don't show

Nothing marks a motor as moving: flags stayed 0x00 during every bench move (e.g. E3b, 251341-252436 ms, 0xC000 →
0xC100) and 0x82 throughout the fail-safe runs. Motion has to be seen from the position changing.

## 2. Position

### 15 bits, relative, 0x3FFF at power-up

- Set_Position 0x4000 reads back as 0xC000 on 0x23 and 0x4000 in the status frame; 0x0123 → 0x8123 / 0x0122;
  0xFFFF → 0xFFFF / 0x7FFE; 0x8001 → 0x8001 / 0x0000 (E1-E4, 803135.3-809758.2 ms). Bit 15 of a commanded position is
  ignored, both for Set_Position and Move (F2: a move to 0x4100 went 0xC000 → 0xC100).
- No wrap, no 16-bit compare: C1/C2, Set_Position 0x8040 then move to 0x7F40 (that is 0x0040 → 0x7F40), and the
  motor drove **up** the long way until Locked (C3, 772699.6 ms) at 0x8B39.
- Range ends are accepted: G2, 0x0080 → 0x0000; G4, 0x7F80 → 0x7FFF; no flag at either end.
- **Relative**: `04-abs-rel`. Set_Position 0x4000 (P1), tape mark on the shaft, move to 0x4400 (P2,
  311340-315859 ms). Power-cycled while polling: it came back as 0x3FFF / flags 0x82, like every power-up (the
  motors were at 0x22E3, 0x237A, 0x22D3 and 0x5D32 before the 12 V was switched off at the end of `03-session`,
  and all came back as 0x3FFF too). Then Set_Position 0x4400 (P5) and move back to 0x4000 (P6, 550634-555159
  ms): the shaft landed back on the tape mark. The position is the motor's own count, not a sensor reading.
- Scale: 1024 counts turned the shaft about 80° (P2), so about **12.8 counts per degree**; the whole range is
  about 2560°. The car's 0xB800-0xC7FF are 0x3800-0x47FF (±2048 counts, ±160° around the power-up value), and a
  flap's travel is about 0x500 counts (motor 1: 0xBD7D-0xC281 = 1284 counts, about 100°).
- The status frame drops bit 0: across all captures 3448 of 3448 status frames have bit 0 clear, while 70% of
  0x23 positions are odd. The Teensy code masks bit 15 of the status frame, which matches the flag.

The notes' power-up value "0xBF 0xFE" is the status frame read MSB first: on the wire it is `FE BF` (0xBFFE =
not referenced + 0x3FFE). The "unassigned" values seen before, 0x8000 (motor 2) and 0xFFFE (motor 8), are
not-referenced motors at the two ends of the range after a fail-safe run (start of `01-baseline`: motors 2, 6
and 7 at 0x0000, motor 8 at 0x7FFF).

### LIN timeout: the fail-safe run

- About 2.7 s after the bus goes quiet every motor starts driving at normal speed (240 counts/s) towards one end:
  motors 2, 6, 7 down to 0x0000, motor 8 up to 0x7FFF. That's up to 0x3FFF counts (~68 s, ~1280°) from the
  power-up position, so on a car it ends at the flap stop. Evidence: Q1/Q2 in `03-session`. Last frame before
  the silence at 1439794.9 ms, polling resumed at 1449883.7 ms (10.09 s). Motor 8 was at 0x46EB (from 0x3FFF, 1772
  counts = 7.4 s) and motor 2 at 0x392C (from 0x4000, 1748 counts = 7.3 s), so they started ~2.7-2.8 s into the
  silence.
- It happens whether the motor is referenced (motor 2, flags 0x00), locked (7, 8, flags 0x82) or not.
- Resumed polling doesn't stop it (Q2: still moving 20 s later). Set_Position does (Q3, 1474071-1474165 ms, all
  four stopped, flags 0x00). It also stopped motor 2 in `02-setpos-m2` (7090.4 ms).
- Power-up with no master starts it too. The user saw all motors moving after a power cycle with the probe
  closed. With polling running through the power-up, the motors stay put (`01-baseline`, `03-session`, `04`).
- It also happened by accident when the probe was restarted between captures 1 and 2 (3.3 s quiet): `02` starts
  with all four already moving. In `02` the probe also stalled ~3 s (between 4044.6 and 7090.4 ms), which is the second set of
  bit-15-clear answers below.
- Any bus traffic keeps all motors awake: while only motor 2 was polled (15:09-15:21 wall clock, E2 to V12), 6, 7 and 8 stayed at 0x3FFF.
- The first 0x23 answer from each motor after a timeout has byte 2 bit 7 **clear** (`01-baseline` start 483168
  ms `22 00 00 80`; `02` start and 7245-8770 ms; `03` start 81010 ms; Q2 1449913-1450193 ms); every other answer
  has it set, including the first one after a power-up with polling running. So it reads as "no LIN timeout since
  the last answer" (Status_NoTimeout in the LDF).

## 3. Move_Speed (0x2F byte 3)

Motor 2, moves of 256 counts (T marks) and 512 counts (U marks, L5), rate from the middle of each move:

| Speed byte | counts/s | | Speed byte | counts/s |
|---|---|---|---|---|
| 0x00 | 241 | | 0x10 | 318 |
| 0x01 | 240 | | 0x20 | 318 |
| 0x02 | 240 | | 0x30 | 319 |
| 0x04 | 241 | | 0x08 | 735 |
| 0x05 | 240 (E3b, A1, B1, ...) | | 0x0C | 730 |
| 0x06 | 241 | | 0x0D | 736 |
| 0x40 | 239 | | 0x0F | 738 |
| 0x80 | 240 | | 0x18 | 732 |
| | | | 0x7F | 701 |
| | | | 0xFF | 702 |

So bit 3 selects fast (about 3x), otherwise bit 4 or 5 selects medium (about 1.3x), and the other bits had no
effect seen. 0x00 is not "stop": it moves at the normal speed. The 0x7F/0xFF figures come from 256-count moves,
which are short for the fast speed. Every move ended exactly on its target. Measured at 12 V on an unloaded shaft.

## 4. Set_Position and Lock_Unlock

- Set_Position (0x2C): takes the value as the current position without moving. It clears bit 7 (referenced),
  clears bit 1 (unlocks), stops a running move (B2, 740665.1 ms: Set_Position 0xC400 mid-move, motor stopped there)
  and stops a fail-safe run. Byte 3 is ignored (H3: 0x00 works like 0xFF). Ignored in standby (0x01).
- Lock_Unlock (0x2E): byte 1 bits 0-1 go to status flag bits 0-1: FC → 0x00, FD → 0x01, FE → 0x02, 00 → 0x00, FF →
  no change (H4-H8). Byte 3 is ignored (H7: `FE FF 00` locks). The gearbox can't be turned by hand, locked or not.
- The car's sequence after a blocked flap, Lock → Set_Position → Unlock, therefore re-references the motor. The
  Unlock is redundant after a Set_Position but harmless.

## 5. StatusReq_Byte1 and device 0xB1

- 0x1B byte 1 = 0xFF, 0x00, 0x01, 0x02, 0x7F, 0x80, 0xFE all give the same answer, for a referenced (motor 2) and
  an unreferenced (motor 6) motor (R1, from 655930.5 ms). A second 0x23 header after one request gets no answer.
- Addresses 0xB1, 0xB2, 0xA2, 0x20, 0x2F, 0x30, 0x02, 0x62, 0xE2 and motors 1, 3, 4, 5, 9 get no answer on the
  bench (R2). In the car 0xB1 always answers `B1 80 7F 04` (0x7F80 with byte 2 bit 7 clear, flags 0x04) and is sent
  `2F B1 FE FF 00` (target 0x7FFE, speed 0). It's another device on the HVAC LIN bus (perhaps a different actuator
  type), not one of these motors.
- Only IDs 0x02, 0x06, 0x07, 0x08 (status) and 0x23 answer in 0x00-0x3F (R3, R4).

## 6. Status frames vs the 0x23 answer

| | Status frame (ID = motor) | 0x23 answer |
|---|---|---|
| Position | bits 1-14 (bit 0 always 0) | bits 0-14 |
| Bit 15 | 1 = not referenced (same as flag 0x80) | 0 = LIN timeout since last answer |
| Flags | none | byte 3 |
| Lock, standby, undervoltage | not visible | bits 1, 0, 2 |

Both report the same live position during moves (the small differences match the ~60 ms between them).

## Still unknown

- Bit 6 (0x40) on the bench: needs a shaft that can be blocked (a flap or lever against a stop). How long the
  motor pushes before reporting it, and whether it then stops, is unknown.
- Bits 3-5: never seen.
- The car's 0x6F speed byte to motor 4 (s-toggle bottom, 6.34 s): motor 4 kept moving at 240 counts/s, though
  bit 3 is set. That motor was already moving on an earlier command; perhaps a new speed only applies to a move
  from rest. Not tested.
- The fail-safe run's direction per motor (2, 6, 7 down; 8 up): presumably the flap's safe position, fixed in
  each motor. Whether Lock or a new Move stops a fail-safe run (only Set_Position was tried).
- Device 0xB1.
- The other speed bits in combination (e.g. 0x28), and speeds under load or at other voltages.
- The odd 0x23 frames in the car logs (`21 7D A3`, 3 bytes, and `21 7D A3 55`), probably logger artefacts.

## Safety notes for anyone driving these motors

- Keep LIN traffic going while the motors are powered, including at power-up, or they start a fail-safe run
  to an end of their range (up to ~68 s). On a car that ends at a flap stop. Loose on a bench, the shaft turns
  up to ~1280°.
- Set_Position stops a run. Lock stops a normal move.
- Before closing the master, switch the motors off first.
