#ifndef Lin_Tom_H
#define Lin_Tom_H

#include "Stream.h"

typedef struct Lin_Frame_t {
  uint8_t id = 0;          // can identifier
  uint8_t len = 0;         // length of data
  uint8_t buf[8] = { 0 };  // data
  uint8_t chk = 0;   //checksum
  bool chktype = 0;//Type of checksum Classic 0 Enhanced 1
} Lin_Frame_t;

class LIN {
public:
  //Tbits Header bits
#define breakdelimiter 1
#define syncfieldPIDinterbytedelay 0
#define breakfieldinterbytedelay 2

//Tbit Response bits
#define responsedelay 8
#define interbytedelay 0
//LIN supports from 2-8bit Data and an additional CRC bit
#define response_nominal (8 + 1) * 10
#define response_max_factor 1.4

#define CLASSIC 0
#define ENHANCED 1


  byte SYNC = 0x55;
  unsigned long Tbit;
  int responsespace;
  int interbytespace;
  int syncfieldPIDinterbytespace;
  int breakfieldinterbytespace;
  unsigned long response_nominalspace;
  unsigned long response_maximalspace;
  uint8_t _breaklenght;

  // Teensy 3.0 & 3.1 & 3.2 & 3.5 & 3.6
  //#if defined (__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MKL26Z64__) || defined(__MK66FX1M0__)
  volatile uint8_t *PortRegister_C1;
  volatile uint8_t *PortRegister_C2;
  volatile uint8_t *PortRegister_C4;
  volatile uint8_t *PortRegister_S2;
  volatile uint8_t *PortRegister_BDH;
  //#elif defined(__IMXRT1062__) // Teensy 4.0 & 4.1
  volatile uint32_t *PortRegister_LPUART_STAT;
  volatile uint32_t *PortRegister_LPUART_BAUD;
  volatile uint32_t *PortRegister_LPUART_CTRL;
  //#endif

#define lin1x 1
#define lin2x 2

  void begin(HardwareSerial *stream, uint16_t baudrate);
  void readLinFrame(byte mID, uint8_t length, bool Type);
  bool CheckBuffer(Lin_Frame_t &frame);
  void sendLinFrame(Lin_Frame_t &frame);

private:
  Stream *_stream;
  uint8_t NextLength;
  uint8_t SentId;
  uint8_t NextChkType;
  uint16_t LinBaud;
  void send_break();
  void breaklength_LP(uint8_t length);
  uint8_t LINChecksum(Lin_Frame_t &framee);
  uint8_t addIDParity(byte linID);
};

#endif
