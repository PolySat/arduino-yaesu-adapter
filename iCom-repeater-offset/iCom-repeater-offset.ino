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

// Global to keep track of the last time we received a byte from the host computer
unsigned long lastRx = 0;


void setup() {
   // Open serial communications and wait for port to open:
   Serial.begin(9600);
   while (!Serial) {
      // wait for serial port to connect. Needed for native USB port only
   }

   // set the data rate for the SoftwareSerial port
   mySerial.begin(9600);
  
   // Set up PTT input / output pins
   pinMode(pttInputPin, INPUT_PULLUP);
   pinMode(pttOutputPin, OUTPUT);  
}

// Checks the state of the PTT line, switching between RX and TX as needed
void check_ptt()
{
   int val =  digitalRead(pttInputPin);
   int newMode = MODE_RX;
   int i;
  
   if (!val)
      newMode = MODE_TX;
      
   if (newMode == MODE_TX && deferTx)
      newMode = MODE_DEFERRED_TX;

   // Stop here if the PTT state hasn't changed.
   if (mode == newMode)
      return;
 
   // XXX Delay move to TX if in mid-frequency update
   mode = newMode;

   // Configure for RX mode.  Note that PTT is released before setting RX frequency
   if (mode == MODE_RX) {
      digitalWrite(pttOutputPin, 0);
      if (TX_2_RX_DELAY > 0)
         delay(TX_2_RX_DELAY);

       send_rx_switch_cmds();
    }
    
    // Configure for TX mode.  Note that TX frequency is set before triggering PTT
    else if (mode == MODE_TX) {
       send_tx_switch_cmds();

       if (RX_2_TX_DELAY > 0)
          delay(RX_2_TX_DELAY);
          
       digitalWrite(pttOutputPin, 1);
    }
    else if (mode == MODE_DEFERRED_TX) {
       // Nothing to do here.  Just wait for the deferral to end
    }
}

// Sends a CIV command to the radio
void send_to_radio(unsigned char *buff, int len)
{
   int i;

   for (i = 0; i < len; i = i + 1)
      mySerial.write(buff[i]);
}

// Sends a CIV command to the station
void send_to_station(unsigned char *buff, int len)
{
   int i;

   for (i = 0; i < len; i = i + 1)
      Serial.write(buff[i]);
}

// Sends the CIV commands to set up TX frequency
void send_tx_switch_cmds()
{
//   send_to_radio(txCommand);
}

// Sends the CIV command to set up RX frequency
void send_rx_switch_cmds()
{
   if (staBufferCount > 0)
      send_to_radio(staBufferInput, staBufferCount);
      
   staBufferCount = 0;
}

void handle_sta_command()
{
   int i, forwardCmd = 1;
   
   if (staInput[DST_ADDR] == RADIO_ADDRESS && staInput[SRC_ADDR] == STATION_ADDRESS) {
      if (staInput[CIV_CMD] == CIV_SET_FREQ_CMD && vfoMode != VFO_MODE_SIMPLEX)
         deferTx = 1;
      
      if (staInput[CIV_CMD] == CIV_VFO_MODE_CMD) {
         vfoMode = staInput[CIV_SUBCMD];
         if (vfoMode == VFO_MODE_SIMPLEX)
            deferTx = 0;
      }
    
      if (staInput[CIV_CMD] == CIV_SET_DUP_OFFSET_CMD)
         deferTx = 0;
         
      // If TX, buffer freq + offset commands until RX
      if (mode == MODE_TX && (staInput[CIV_CMD] == CIV_VFO_MODE_CMD
           || staInput[CIV_CMD] == CIV_SET_FREQ_CMD || staInput[CIV_CMD] == CIV_SET_DUP_OFFSET_CMD)) {
         if (staCount + staBufferCount < CIV_BUFF_LEN) {
            for (i = 0; i < staCount; i++)
               staBufferInput[staBufferCount + i] = staInput[i];
            staBufferCount += staCount;
         }
         forwardCmd = 0;
      }
   }
   
   if (forwardCmd)
      send_to_radio(staInput, staCount);
}

void handle_radio_input()
{
   if (staInput[DST_ADDR] != STATION_ADDRESS || staInput[SRC_ADDR] != RADIO_ADDRESS)
      return;
}

// Checks to see if we have seen a complete CIV command by
//  verifying the CIV framing bytes.  Will restart CIV RX state
//  if new start bytes are seen in the middle of a command stream.
int validate_civ_command(unsigned char *buff, int *len)
{
   // If the first byte isn't a preamble, this is a bad command.  Reset RX counters.
   if (*len == 1 && buff[0] != CIV_PREAMBLE) {
      *len = 0;
      return 0;
   }
   
   // If the second byte isn't a preamble, this is a bad command.  Reset RX counters.
   if (*len == 2 && buff[1] != CIV_PREAMBLE) {
      *len = 0;
      return 0;
   }
   
   // If any other byte is a preamble, this is a bad command.  Reset RX counters.
   if (*len > 2 && buff[*len - 1] == CIV_PREAMBLE) {
      buff[0] = CIV_PREAMBLE;
      *len = 1;
   }

   // Return true after we have a valid frame
   if (*len >= 6 && buff[0] == CIV_PREAMBLE && buff[1] == CIV_PREAMBLE && buff[*len - 1] == CIV_EOF)
      return 1;      

   // Prevent buffer overflows by bounds checking our buffer
   if (*len >= CIV_MAX_LEN)
      *len = 0;

   return 0;
}

// run over and over
void loop()
{
   int i;
   
   check_ptt();

   // Reset our buffer if we haven't seen a character for more than 250ms
   if (lastRx && (millis() - lastRx) > 250)
      staCount = 0;

   // Handle and input characters from the computer facing serial port
   while (Serial.available()) {
      // Read and buffer one byte at a time
      staInput[staCount++] = Serial.read();
      lastRx = millis();
      
      // Process a command from the computer once an entire command has been received
      if (validate_civ_command(staInput, &staCount)) {
         handle_sta_command();
         staCount = 0;
      }
    
      // We check PTT frequently to be very responsive without interrupts
      check_ptt();
   }
  
   // Handle any input characters from the radio facing serial port
   while (mySerial.available()) {
      // Read and buffer one byte at a time
      radioInput[radioCount++] = mySerial.read();
      
      // Process a command from the computer once an entire command has been received
      if (validate_civ_command(radioInput, &radioCount)) {
         handle_radio_input();
         send_to_station(radioInput, radioCount);
         radioCount = 0;
      }
 
      // We check PTT frequently to be very responsive without interrupts
      check_ptt();
   }
}
