/*

  LIN Interface for VW HV AC compressor and PTC Heater

  Tom de Bree - Volt Influx Ltd

********************************************************************
*/

#include "Lin_Tom.h"

LIN lin1;  // LIN channel 1

Lin_Frame_t outFrame;
Lin_Frame_t inFrame;

/////Version Identifier/////////
uint32_t firmver = 240318;  //Year Month Day Ver

uint8_t BMWtype = 2;  //1-5 Series, 2- E90 3Series

int ledState = LOW;               // ledState used to set the LED
unsigned long interval = 200000;  // interval at which to blinkLED to run every 0.2 seconds

// Create an IntervalTimer object
IntervalTimer myTimer;

// LIN serial Interface

int Lin1serSpeed = 9600;   //19200;  //20000;    // speed LIN
int Lin2serSpeed = 19200;  //20000;    // speed LIN

int Lin1CSPin = 32;  // CS Pin
int Lin1_Fault = 28;
int Lin2CSPin = 18;  // CS Pin

//LIN bus IDs//

#define POS_CMD 0x2F  //0x20


//Lin Info
uint16_t PosCMD[16];
uint16_t Position[16];
uint8_t Motor = 5;
uint8_t MotSpeed = 0;

//////////////////////////////////////////

////Software Timers////
unsigned long looptime500, looptime100, looptime50 = 0;

// Global Variables
byte LinMessageA[200] = { 0 };
byte lin22[9] = { 0 };

uint16_t StartBreak = 0;

int led1 = 23;

bool SendCMDpos, SendCMDset, SendCMDlock, SendCMDunlock = 0;

//
uint8_t can_data;
uint8_t MSG_CNT = 3;

//SerialUSB comms

uint8_t menuload = 0;
char msgString[128];  // Array to store serial string
byte incomingByte = 0;

// Configuration Debug
bool debug_Lin = 1;
bool debug_rawLin = 0;
bool Debug = 0;
bool candebug1, candebug2, candebug3 = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(LED_BUILTIN, HIGH);

  lin1.begin(&Serial1, Lin1serSpeed);

  //Lin2Serial.begin(Lin2serSpeed);

  Serial.begin(115200);

  delay(1000);
  pinMode(led1, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.print("Teensy 4.0 Lin Test");
  Serial.println();
  Serial.print(Lin1serSpeed);
  Serial.println(" Baud Rate");

  myTimer.begin(blinkLED, interval);
}

void loop() {
  if (Serial.available() > 0) {
    Serialcomms();
  }
  if (lin1.CheckBuffer(inFrame)) {
    DecodeLin(inFrame, 1);
  }
  if (millis() - looptime50 > 50) {
    looptime50 = millis();
    Tasks50ms();
  }
}

void Tasks50ms() {
  send_LIN();
}

void blinkLED() {
  ledState = !ledState;

  digitalWrite(LED_BUILTIN, ledState);
  digitalWrite(led1, ledState);
}

void Serialcomms() {
  incomingByte = Serial.read();

  if (menuload == 0)  //normal run no serial menu opened
  {
    switch (incomingByte) {

      case 'l':
        debug_rawLin = ~debug_rawLin;
        break;

      case 'm':
        if (Serial.available() > 0) {
          Motor = Serial.parseInt();
          if (Motor > 0xF) {
            Motor = 0;
          }
          Serial.print("Motor = ");
          Serial.print(Motor);
          Serial.println();
        }
        break;

      case 'p':
        if (Serial.available() > 0) {
          PosCMD[Motor - 1] = Serial.parseInt();
          if (PosCMD[Motor - 1] > 0xFFFF) {
            PosCMD[Motor - 1] = 0;
          }
          Serial.print("Position CMD = ");
          Serial.print(PosCMD[Motor - 1]);
          Serial.println();
          //Send_Pos_Cmd();
        }
        break;

      case 'v':
        if (Serial.available() > 0) {
          MotSpeed = Serial.parseInt();
          if (MotSpeed > 0xFF) {
            MotSpeed = 0;
          }
          Serial.print("CMD Spd = ");
          Serial.print(MotSpeed);
          Serial.println();
          //Send_Pos_Cmd();
        }
        break;

      case 's':
        SendCMDpos = 1;
        break;

      case 'u':
        SendCMDunlock = 1;
        break;

      case 'i':
        SendCMDlock = 1;
        break;

      case 'z':
        SendCMDset = 1;
        break;

      case 'c':
        if (Serial.available() > 0) {
          switch (Serial.read()) {
            case '0':
              candebug1 = 0;
              candebug2 = 0;
              candebug3 = 0;

              Serial.print("ALL Can Debug OFF");
              Serial.println();
              break;

            case '1':
              candebug1 = !candebug1;
              if (candebug1 == 0) {
                Serial.print("Can1 Debug OFF");
              } else {
                Serial.print("Can1 Debug ON");
              }
              Serial.println();
              break;

            case '2':
              candebug2 = !candebug2;
              if (candebug2 == 0) {
                Serial.print("Can2 Debug OFF");
              } else {
                Serial.print("Can2 Debug ON");
              }
              Serial.println();
              break;

            case '3':
              candebug3 = !candebug3;
              if (candebug3 == 0) {
                Serial.print("Can3 Debug OFF");
              } else {
                Serial.print("Can3 Debug ON");
              }
              Serial.println();
              break;

            default:
              // if nothing else matches, do the default
              // default is optional
              break;
          }
        }
      default:
        // if nothing else matches, do the default
        // default is optional
        break;
    }
  }
}

void Send_Pos_Cmd() {
  outFrame.id = POS_CMD;  //positon command 0x2F works for Motor 1 + 5
  outFrame.len = 4;       // LenLin = amount of data bytes
  outFrame.chktype = CLASSIC;
  outFrame.buf[0] = 0x20 | Motor;
  outFrame.buf[1] = PosCMD[Motor - 1] & 0xFF;         //Position LSB
  outFrame.buf[2] = (PosCMD[Motor - 1] >> 8) & 0xFF;  //Position MSB
  /*
  if (Motor == 1 || Motor == 5 || Motor == 6 || Motor == 7 || Motor == 8) {
    outFrame.buf[3] = 0x05;
  }
  if (Motor == 4) {
    outFrame.buf[3] = 0x6F;
  }
  */
  outFrame.buf[3] = MotSpeed;
  lin1.sendLinFrame(outFrame);

  SendCMDpos = 0;
}

void Send_Lock_Cmd() {

  outFrame.id = 0x02E;  //positon command 0x2F works for Motor 1 + 5
  outFrame.len = 4;     // LenLin = amount of data bytes
  outFrame.chktype = CLASSIC;
  outFrame.buf[0] = 0x20 | Motor;
  outFrame.buf[1] = 0xFE;  //Position LSB
  outFrame.buf[2] = 0xFF;  //Position MSB
  outFrame.buf[3] = 0xFF;

  lin1.sendLinFrame(outFrame);

  SendCMDlock = 0;
}

void Send_Set_Cmd() {
  outFrame.id = 0x02C;  //positon command 0x2F works for Motor 1 + 5
  outFrame.len = 4;     // LenLin = amount of data bytes
  outFrame.chktype = CLASSIC;
  outFrame.buf[0] = 0x20 | Motor;
  outFrame.buf[1] = 0x4A;  //Position LSB
  outFrame.buf[2] = 0xBD;  //Position MSB
  outFrame.buf[3] = 0xFF;

  lin1.sendLinFrame(outFrame);

  SendCMDset = 0;
}


void Send_Unlock_Cmd() {
  outFrame.id = 0x02E;  //positon command 0x2F works for Motor 1 + 5
  outFrame.len = 4;     // LenLin = amount of data bytes
  outFrame.chktype = CLASSIC;
  outFrame.buf[0] = 0x20 | Motor;
  outFrame.buf[1] = 0xFC;  //Position LSB
  outFrame.buf[2] = 0xFF;  //Position MSB
  outFrame.buf[3] = 0xFF;

  lin1.sendLinFrame(outFrame);

  SendCMDunlock = 0;
}

void send_LIN()  // this runs every 50ms and read/write data
{
  if (BMWtype == 1) {
    switch (MSG_CNT) {
      case 0:
        {
          if (SendCMDpos == 1) {
            Send_Pos_Cmd();
          }
        }
        break;
      case 1:
        {
          lin1.readLinFrame(0x01, 2, CLASSIC);
        }
        break;
      case 2:
        {
          lin1.readLinFrame(0x05, 2, CLASSIC);
        }
        break;
      case 3:
        {
          lin1.readLinFrame(0x04, 2, CLASSIC);
        }
        break;
      case 4:
        {
          lin1.readLinFrame(0x06, 2, CLASSIC);
        }
        break;
      case 5:
        {
          lin1.readLinFrame(0x07, 2, CLASSIC);
        }
        break;
      case 6:
        {
          lin1.readLinFrame(0x08, 2, CLASSIC);
        }
        break;
      case 7:
        {
          lin1.readLinFrame(0x09, 2, CLASSIC);
        }
        break;
      case 8:
        {
          lin1.readLinFrame(0x0A, 2, CLASSIC);
        }
        break;
      case 9:
        {
          lin1.readLinFrame(0x0B, 2, CLASSIC);
        }
        break;
      case 10:
        {
          lin1.readLinFrame(0x0C, 2, CLASSIC);
        }
        break;
      case 11:
        {
          lin1.readLinFrame(0x0D, 2, CLASSIC);
        }
        break;
      case 12:
        {
          lin1.readLinFrame(0x0E, 2, CLASSIC);
        }
        break;
      case 13:
        {
          lin1.readLinFrame(0x0F, 2, CLASSIC);
        }
        break;

      case 14:

        break;
      default:
        {
        }
        break;
    }
    MSG_CNT++;
    if (MSG_CNT > 14) MSG_CNT = 0;
  }

  if (BMWtype == 2)  //E90 3 Series
  {
    switch (MSG_CNT) {
      case 0:
        {
          if (SendCMDset == 1) {
            Send_Set_Cmd();
            MSG_CNT = 3;
          } else if (SendCMDpos == 1) {
            Send_Pos_Cmd();
            MSG_CNT = 3;
          } else if (SendCMDunlock == 1) {
            Send_Unlock_Cmd();
            MSG_CNT = 3;
          } else if (SendCMDlock == 1) {
            Send_Lock_Cmd();
            MSG_CNT = 3;
          } else {
            MSG_CNT = 3;
          }
        }
        break;
      case 3:
        {
          lin1.readLinFrame(0x02, 2, CLASSIC);
          MSG_CNT++;
        }
        break;
      case 4:
        {
          lin1.readLinFrame(0x06, 2, CLASSIC);
          MSG_CNT++;
        }
        break;
      case 5:
        {
          lin1.readLinFrame(0x07, 2, CLASSIC);
          MSG_CNT++;
        }
        break;
      case 6:
        {
          lin1.readLinFrame(0x08, 2, CLASSIC);
          MSG_CNT++;
        }
        break;
      default:
        {
        }
        break;
    }
    if (MSG_CNT > 6) MSG_CNT = 0;
  }
}

void DecodeLin(Lin_Frame_t& frame, uint8_t bus) {
  if (debug_rawLin) {
    Serial.print("Lin");
    Serial.print(bus);
    Serial.print("|ID: ");
    Serial.print(frame.id, HEX);
    Serial.print("- Data ");
    for (int ixx = 0; ixx < frame.len; ixx++) {
      Serial.print(frame.buf[ixx], HEX);
      Serial.print(":");
    }
    Serial.print("- Check: ");
    Serial.print(frame.chk, HEX);
    Serial.println();
  }
  if (frame.id == 0x85)  //0x05
  {
    Position[4] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 5 Pos: ");
      Serial.print(Position[4]);
      Serial.println();
    }
  }
  if (frame.id == 0xC1)  //0x01
  {
    Position[0] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 1 Pos: ");
      Serial.print(Position[0]);
      Serial.println();
    }
  }
  if (frame.id == 0x42)  //0x02
  {
    Position[1] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 2 Pos: ");
      Serial.print(Position[1]);
      Serial.println();
    }
  }
  if (frame.id == 0xC4)  //0x04
  {
    Position[3] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 4 Pos: ");
      Serial.print(Position[3]);
      Serial.println();
    }
  }
  if (frame.id == 0x06)  //0x06
  {
    Position[5] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 6 Pos: ");
      Serial.print(Position[5]);
      Serial.println();
    }
  }
  if (frame.id == 0x47)  //0x07
  {
    Position[6] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 7 Pos: ");
      Serial.print(Position[6]);
      Serial.println();
    }
  }
  if (frame.id == 0x08)  //0x08
  {
    Position[7] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 8 Pos: ");
      Serial.print(Position[7]);
      Serial.println();
    }
  }
  if (frame.id == 0x49)  //0x09
  {
    Position[8] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 9 Pos: ");
      Serial.print(Position[8]);
      Serial.println();
    }
  }
  if (frame.id == 0xCA)  //0x0A
  {
    Position[9] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 10 Pos: ");
      Serial.print(Position[9]);
      Serial.println();
    }
  }
  if (frame.id == 0x8B)  //0x0B
  {
    Position[10] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 11 Pos: ");
      Serial.print(Position[10]);
      Serial.println();
    }
  }
  if (frame.id == 0x4C)  //0x0C
  {
    Position[11] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 12 Pos: ");
      Serial.print(Position[11]);
      Serial.println();
    }
  }
  if (frame.id == 0x0D)  //0x0D
  {
    Position[12] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 13 Pos: ");
      Serial.print(Position[12]);
      Serial.println();
    }
  }
  if (frame.id == 0x8E)  //0x0E
  {
    Position[13] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 14 Pos: ");
      Serial.print(Position[13]);
      Serial.println();
    }
  }
  if (frame.id == 0xCF)  //0x0F
  {
    Position[14] = frame.buf[0] + ((frame.buf[1] & 0x7F) << 8);
    //Anzeigen ();
    if (debug_Lin) {
      Serial.print("Recieved ID: ");
      Serial.print(frame.id, HEX);
      Serial.print("- Mot 15 Pos: ");
      Serial.print(Position[14]);
      Serial.println();
    }
  }
}