#include "Arduino.h"
#include "Lin_Tom.h"
/*
bool LIN::CheckBuffer(byte* buffer) {

  if (_stream->available() > 11) {
    _stream->read();
    _stream->readBytes(buffer, 12);
    return 1;
  }
  return 0;
}
*/

bool LIN::CheckBuffer(Lin_Frame_t& frame) {
  uint8_t buffer[12];

  if (_stream->available() > NextLength+2) {
    _stream->read();
    _stream->readBytes(buffer, 12);

    frame.id = buffer[1];
    if(frame.id == SentId)
    {
        return 0;//kick out if refelection of a comms frame
    }
    frame.len = NextLength;
    frame.chk = buffer[NextLength + 2];
    for (int ixx = 0; ixx < NextLength; ixx++) {
      frame.buf[ixx] = buffer[ixx + 2];
    }
    return 1;
  }
  return 0;
}

// Read answer from Lin bus
void LIN::readLinFrame(byte mID, uint8_t length, bool Type) {
  byte linID = mID & 0x3F | addIDParity(mID);
  NextLength = length;
  NextChkType = Type;
  while (_stream->available() > 0) {
    _stream->read();
  }
  send_break();

  _stream->write(linID);  // ID
  //_stream->flush();
}

void LIN::sendLinFrame(Lin_Frame_t& frame) {
  /*byte linID */frame.id = frame.id & 0x3F | addIDParity(frame.id);
  frame.chk = LINChecksum(frame);
  uint8_t Ix = 0;
  send_break();
  _stream->write(/*linID*/frame.id );  // ID
  SentId = frame.id;
  while (Ix < frame.len) {
    _stream->write(frame.buf[Ix]);  // Message (array from 1..8)
    Ix++;
  }
  _stream->write(frame.chk);
  _stream->flush();
}


void LIN::begin(HardwareSerial* stream, uint16_t baudrate) {
  LinBaud = baudrate;
  (*stream).begin(baudrate);
  this->_stream = stream;

  Tbit = 1000000 / baudrate;
  responsespace = responsedelay * Tbit;
  interbytespace = interbytedelay * Tbit;
  syncfieldPIDinterbytespace = syncfieldPIDinterbytedelay * Tbit;
  breakfieldinterbytespace = breakfieldinterbytedelay * Tbit;
  response_nominalspace = response_nominal * Tbit;
  response_maximalspace = response_nominalspace * response_max_factor;

  if (stream == &Serial1) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART6_STAT;
    PortRegister_LPUART_BAUD = &LPUART6_BAUD;
    PortRegister_LPUART_CTRL = &LPUART6_CTRL;
#endif
  }
  if (stream == &Serial2) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART4_STAT;
    PortRegister_LPUART_BAUD = &LPUART4_BAUD;
    PortRegister_LPUART_CTRL = &LPUART4_CTRL;
#endif
  }
  if (stream == &Serial3) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART2_STAT;
    PortRegister_LPUART_BAUD = &LPUART2_BAUD;
    PortRegister_LPUART_CTRL = &LPUART2_CTRL;
#endif
  }
  if (stream == &Serial4) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART3_STAT;
    PortRegister_LPUART_BAUD = &LPUART3_BAUD;
    PortRegister_LPUART_CTRL = &LPUART3_CTRL;
#endif
  }
  if (stream == &Serial5) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART8_STAT;
    PortRegister_LPUART_BAUD = &LPUART8_BAUD;
    PortRegister_LPUART_CTRL = &LPUART8_CTRL;
#endif
  }
  if (stream == &Serial6) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    PortRegister_LPUART_STAT = &LPUART1_STAT;
    PortRegister_LPUART_BAUD = &LPUART1_BAUD;
    PortRegister_LPUART_CTRL = &LPUART1_CTRL;
#endif
  }
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
  if (stream == &Serial7) {
    PortRegister_LPUART_STAT = &LPUART7_STAT;
    PortRegister_LPUART_BAUD = &LPUART7_BAUD;
    PortRegister_LPUART_CTRL = &LPUART7_CTRL;
  }
#endif
  breaklength_LP(13);
}

uint8_t LIN::LINChecksum(Lin_Frame_t& frame) {
  uint16_t sum = 0;
  uint8_t nByte = frame.len;
  if (frame.chktype == CLASSIC) sum = 0;
  else sum = frame.id;
  while (nByte-- > 0) sum += frame.buf[nByte];
  while (sum >> 8)
    sum = (sum & 255) + (sum >> 8);
  return (~sum);
}

uint8_t LIN::addIDParity(byte linID) {
  byte p0 = bitRead(linID, 0) ^ bitRead(linID, 1) ^ bitRead(linID, 2) ^ bitRead(linID, 4);
  byte p1 = ~(bitRead(linID, 1) ^ bitRead(linID, 3) ^ bitRead(linID, 4) ^ bitRead(linID, 5));  // evtl. Klammer rum ???
  return ((p0 | (p1 << 1)) << 6);
}

void LIN::breaklength_LP(uint8_t length) {
  switch (length) {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
    case 9:
      //  9 Bits transmitted
      (*PortRegister_LPUART_STAT) &= ~LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
      (*PortRegister_LPUART_CTRL) |= LPUART_CTRL_M7;
#endif
    case 10:
      // 10 Bits transmitted
      (*PortRegister_LPUART_STAT) &= ~LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M7;
#endif
      break;
    case 11:
      // 11 Bits transmitted
      (*PortRegister_LPUART_STAT) &= ~LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) |= LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M7;
#endif
      break;
    case 12:
      // 12 Bits transmitted
      (*PortRegister_LPUART_STAT) &= ~LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) |= LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) |= LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
      break;
    case 13:
      // 13 Bits transmitted
      (*PortRegister_LPUART_STAT) |= LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
      (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_M7;
#endif
      break;
    case 14:
      // 14 Bits transmitted
      (*PortRegister_LPUART_STAT) |= LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_CTRL) |= LPUART_CTRL_M;
      (*PortRegister_LPUART_BAUD) &= ~LPUART_BAUD_M10;
      break;
    case 15:
      // 15 Bits transmitted
      (*PortRegister_LPUART_STAT) |= LPUART_STAT_BRK13;
      (*PortRegister_LPUART_BAUD) |= LPUART_BAUD_SBNS;
      (*PortRegister_LPUART_BAUD) |= LPUART_BAUD_M10;
      break;
  }
}


// Generate Break signal LOW on LIN bus
void LIN::send_break() {
#if defined(__IMXRT1062__)  // Teensy 4.0 & 4.1
  (*PortRegister_LPUART_CTRL) |= LPUART_CTRL_SBK;
  (*PortRegister_LPUART_CTRL) &= ~LPUART_CTRL_SBK;
#endif

  _stream->write(SYNC);  //Sync 0x55
  _stream->flush();

  delayMicroseconds(syncfieldPIDinterbytespace);
}