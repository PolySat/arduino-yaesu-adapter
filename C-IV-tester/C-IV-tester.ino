#include <SoftwareSerial.h>

// Delay after setting the TX frequency and before triggering the PTT line, in milliseconds
#define RX_2_TX_DELAY 0

// Delay after releasing the PTT and before setting the RX frequency, in milliseconds
#define TX_2_RX_DELAY 0

// Serial interface to co
SoftwareSerial mySerial(8, 9); // RX, TX

// Arduino pin number for the PTT input.  This is connected to your TNC.
int pttInputPin = 7;

// Arduino pin number for the PTT output.  This is connected to your radio.
int pttOutputPin = 10;

// Addresses
#define RADIO_ADDRESS 0x60
#define STATION_ADDRESS 0xE0

// Mode constants
#define MODE_RX 1
#define MODE_TX 2
#define MODE_DEFERRED_TX 3

// CI-V commands
#define CIV_PREAMBLE 0xFE
#define CIV_EOF 0xFD

// CI-V Packet Byte Offsets
#define DST_ADDR 2
#define SRC_ADDR 3
#define CIV_CMD 4
#define CIV_SUBCMD 5

// VFO Mode Constants
#define VFO_MODE_SIMPLEX 0x10
#define VFO_MDOE_POS_DUPLEX 0x11
#define VFO_MODE_NEG_DUPLEX 0x12
#define CIV_SET_FREQ_CMD 0x00
#define CIV_SET_DUP_OFFSET_CMD 0x0D
#define CIV_VFO_MODE_CMD 0x0F

// Globals
#define CIV_MAX_LEN 32
#define CIV_BUFF_LEN (CIV_MAX_LEN * 4)

// State for serial RX from controlling computer
int staCount = 0;
unsigned char staInput[CIV_MAX_LEN];

// State for serial RX from radio
unsigned char radioInput[CIV_MAX_LEN];
int radioCount = 0;

// Buffer to hold station commands until TX is done
unsigned char staBufferInput[CIV_BUFF_LEN];
int staBufferCount = 0;

// Current radio mode (RX or TX)
unsigned char mode = MODE_RX;
unsigned char deferTx = 0;

// Global to track current VFO mode
unsigned char vfoMode = VFO_MODE_SIMPLEX;


void setup() {
   // Open serial communications and wait for port to open:
   Serial.begin(9600);
   while (!Serial) {
      // wait for serial port to connect. Needed for native USB port only
   }

   // set the data rate for the SoftwareSerial port
   mySerial.begin(9600);

  sta_help();
}

// Sends a CIV command to the radio
void send_to_radio(unsigned char *buff, int len)
{
   int i;

   for (i = 0; i < len; i = i + 1)
      mySerial.write(buff[i]);
}

void handle_sta_command()
{
   unsigned char simplex[] = { CIV_PREAMBLE, CIV_PREAMBLE, RADIO_ADDRESS, STATION_ADDRESS, 0x0F, 0x10, CIV_EOF};
   unsigned char split_on[] = { CIV_PREAMBLE, CIV_PREAMBLE, RADIO_ADDRESS, STATION_ADDRESS, 0x0F, 0x01, CIV_EOF};
   unsigned char split_off[] = { CIV_PREAMBLE, CIV_PREAMBLE, RADIO_ADDRESS, STATION_ADDRESS, 0x0F, 0x00, CIV_EOF};
   unsigned char set_freq[] = { CIV_PREAMBLE, CIV_PREAMBLE, RADIO_ADDRESS, STATION_ADDRESS, 0x05, 0x00, 0x00, 0x27, 0x37, 0x04, CIV_EOF};
   
   if (staCount == 0)
      Serial.print("> ");
   else if (staInput[0] == '1') {
      Serial.print("Sending simplex command\n");
      send_to_radio(simplex, sizeof(simplex));
      Serial.print("> ");
   }
   else if (staInput[0] == '2') {
     Serial.print("Sending split on command\n");
     send_to_radio(split_on, sizeof(split_on));
      Serial.print("> ");
   }
   else if (staInput[0] == '3') {
      Serial.print("Sending split off command\n");
      send_to_radio(split_off, sizeof(split_off));
      Serial.print("> ");
   }
   else if (staInput[0] == '4') {
     Serial.print("Setting frequency to 437.270MHz\n");
      send_to_radio(set_freq, sizeof(set_freq));
      Serial.print("> ");
   }
   else
      sta_help();
}

// Print the list of commands
void sta_help()
{
   Serial.print("Available commands:\n");
   Serial.print("1:   Enable Simplex mode (no repeater offset)\n");
   Serial.print("2:   Split mode on\n");
   Serial.print("3:   Split mode off\n");
   Serial.print("4:   Set frequency to 437.270\n");
   Serial.print("> ");
}

// run over and over
void loop()
{
   int i;
   char buff[32];
   
   // Handle and input characters from the computer facing serial port
   while (Serial.available()) {
      // Read and buffer one byte at a time
      staInput[staCount] = Serial.read();
      Serial.write(staInput[staCount]);
      if (staInput[staCount] == '\n') {
         handle_sta_command();
         staCount = 0;
      }
      else
         staCount++;
         
      if (staCount > 5) {
         sta_help();
         staCount = 0;
      }
   }
  
   // Handle any input characters from the radio facing serial port
   while (mySerial.available()) {
      // Read and buffer one byte at a time
      radioInput[0] = mySerial.read();
      sprintf(buff, "%02X", radioInput[0]);
      Serial.print(buff);
      if (radioInput[0] == CIV_EOF)
         Serial.print("\n");      
   }
}
