#include <SoftwareSerial.h>

// If set to 1 human-readable diagnostic information will be printed via the radio-facing serial port.  Set to 0 for normal operation.
#define DEBUG 0

// Send the CAT commands to trigger PTT in addition to driving the PTT output pin.  This is untested and not recommended.
#define CAT_PTT 1

// Serial interface to co
SoftwareSerial mySerial(8, 9); // RX, TX

// Arduino pin number for the PTT input.  This is connected to your TNC.
int pttInputPin = 7;

// Arduino pin number for the PTT output.  This is connected to your radio.
int pttOutputPin = 6;

// Mode constants
#define MODE_RX 1
#define MODE_TX 2

// CAT commands
#define CAT_PTT_ON 0x08
#define CAT_PTT_OFF 0x88
#define CAT_SAT_MODE_ON 0x4E
#define CAT_SAT_MODE_OFF 0x8E
#define CAT_VFO_SET_FREQ 0x01
#define CAT_SAT_RX_SET_FREQ 0x11
#define CAT_SAT_TX_SET_FREQ 0x21
#define CAT_VFO_SET_MODE 0x07
#define CAT_SAT_RX_SET_MODE 0x17
#define CAT_SAT_TX_SET_MODE 0x27
#define CAT_RX_GET_STATUS 0xE7
#define CAT_TX_GET_STATUS 0xF7
#define CAT_VFO_GET_FREQ 0x03
#define CAT_SAT_RX_GET_FREQ 0x13
#define CAT_SAT_TX_GET_FREQ 0x23

// Globals
#define CAT_CMD_LEN 5
// State for serial RX from controlling computer
int staCount = 0;
unsigned char staInput[CAT_CMD_LEN];

// State for serial RX from radio
unsigned char radioInput[CAT_CMD_LEN];
int radioExpLen = 0;
int radioCount = 0;
int queryMode = 0;

// Bufferend commands for setting RX/TX frequency
unsigned char rxCommand[CAT_CMD_LEN];
unsigned char rxMode[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, CAT_SAT_RX_SET_MODE };
unsigned char txCommand[CAT_CMD_LEN];
unsigned char txMode[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, CAT_SAT_TX_SET_MODE };

// Bufferend commands for responding with "cached" RX/TX radio status
unsigned char txDummyStatus[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
unsigned char rxDummyStatus[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
int rxStatusValid = 0, txStatusValid = 0;

// CAT commands for controlling PTT
unsigned char pttOn[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, CAT_PTT_ON };
unsigned char pttOff[CAT_CMD_LEN] = { 0x00, 0x00, 0x00, 0x00, CAT_PTT_OFF };

// Current radio mode (RX or TX)
int mode = MODE_RX;
// Global switch to disable our sat mode simulator
int satMode = 0;

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

   // Stop here if the PTT state hasn't changed.
   if (mode == newMode)
      return;
      
   mode = newMode;

   // Configure for RX mode.  Note that PTT is released before setting RX frequency
   if (mode == MODE_RX) {
      digitalWrite(pttOutputPin, 0);
#if CAT_PTT
      for (i = 0; i < CAT_CMD_LEN; i++)
         mySerial.write(pttOff[i]);
#endif
       if (satMode)
          send_rx_switch_cmds();
    }
    
    // Configure for TX mode.  Note that TX frequency is set before triggering PTT
    else if (mode == MODE_TX) {
       if (satMode)
          send_tx_switch_cmds();
          
#if CAT_PTT       
       for (i = 0; i < CAT_CMD_LEN; i++)
          mySerial.write(pttOn[i]);
#endif
       digitalWrite(pttOutputPin, 1);
    }
}

#if DEBUG
// Pretty prints the CAT command in ASCII to the radio serial port.  For debugging use only, as the radio does't speak ASCII.
void print_input(String label, unsigned char buff[CAT_CMD_LEN])
{
   int i;

   mySerial.println(label);
   for (i = 0; i < CAT_CMD_LEN; i = i + 1) {
      mySerial.print(String(buff[i], HEX));
      mySerial.print("!");
   }
   mySerial.println();
}
#endif

// Sends a CAT command to the radio
void send_to_radio(unsigned char buff[CAT_CMD_LEN])
{
   int i;

   for (i = 0; i < CAT_CMD_LEN; i = i + 1)
      mySerial.write(buff[i]);
}

// Sends the CAT command to set up TX frequency
void send_tx_switch_cmds()
{
   // Ensure the command is directed to the VFO, not SAT TX, if we are in sat mode
   if (satMode) {
      txCommand[4] = CAT_VFO_SET_FREQ;
      txMode[4] = CAT_VFO_SET_MODE;
   }
   else {
      txCommand[4] = CAT_SAT_TX_SET_FREQ;
      txCommand[4] = CAT_SAT_TX_SET_MODE;
   }

#if DEBUG
   print_input("TX", txCommand);
#else
   send_to_radio(txCommand);
#endif

   if (txMode[0] != rxMode[0]) {
#if DEBUG
      print_input("TXM", txMode);
#else
      send_to_radio(txMode);
#endif
   }

   // This command doesn't generate a response from the radio, so reset the radio serial input state
   reset_radio_rx(0);
}

// Sends the CAT command to set up RX frequency
void send_rx_switch_cmds()
{
   // Ensure the command is directed to the VFO, not SAT TX, if we are in sat mode
   if (satMode) {
      rxCommand[4] = CAT_VFO_SET_FREQ;
      rxCommand[4] = CAT_VFO_SET_MODE;
   }
   else {
      rxCommand[4] = CAT_SAT_RX_SET_FREQ;
      rxCommand[4] = CAT_SAT_RX_SET_MODE;
   }

#if DEBUG
    print_input("RX", rxCommand);
#else
    send_to_radio(rxCommand);
#endif

   if (txMode[0] != rxMode[0]) {
#if DEBUG
      print_input("RXM", rxMode);
#else
      send_to_radio(rxMode);
#endif
   }

   // This command doesn't generate a response from the radio, so reset the radio serial input state
   reset_radio_rx(0);
}

// Send dummy RX status (e.g., frequency and mode) to the host
void dummy_rx_status()
{
   int i;
   
   for (i = 0; i < CAT_CMD_LEN; i++)
      Serial.write(rxDummyStatus[i]);

   reset_radio_rx(0);
}

// Send dummy TX status (e.g., frequency and mode) to the host
void dummy_tx_status()
{
   int i;
   
   for (i = 0; i < CAT_CMD_LEN; i++)
      Serial.write(txDummyStatus[i]);

   reset_radio_rx(0);
}

// Sets up globals to receive a new input command from the radio
void reset_radio_rx(int expLen)
{
   radioExpLen = expLen;
   radioCount = 0;
}

// Process a full command received from the computer
void handle_sta_command()
{
   int i;
   int forwardCmd = 1;  // Flag indicating if this command will be forwarded to the radio
  
   // Cache and mode changes in TX, then actually apply them to the main VFO
   if (staInput[4] == CAT_SAT_TX_SET_MODE) {
      txDummyStatus[4] = staInput[0];
      txMode[0] = staInput[0];
      if (satMode) {
         if (mode == MODE_TX)
            staInput[4] = CAT_VFO_SET_MODE;
         else
            forwardCmd = 0;
      }
   }
   
   // Cache and mode changes in RX, then actually apply them to the main VFO
   else if (staInput[4] == CAT_SAT_RX_SET_MODE) {
      rxDummyStatus[4] = staInput[0];
      rxMode[0] = staInput[0];
      if (satMode) {
         if (mode == MODE_RX)
            staInput[4] = CAT_VFO_SET_MODE;
         else
            forwardCmd = 0;
      }
   }
   
   // Cache new TX frequency.  If radio is in TX mode then apply the frequency change to the main VFO
   else if (staInput[4] == CAT_SAT_TX_SET_FREQ) {
      // Save the frequency for use in later TX command
      for (i = 0; i < CAT_CMD_LEN; i++)
         txCommand[i] = staInput[i];

      // Save the frequency for later status responses, avoiding the mode byte in the status response
      for (i = 0; i < CAT_CMD_LEN - 1; i++)
         txDummyStatus[i] = staInput[i];
     
      if (satMode) {    
         if (mode == MODE_TX)
            staInput[4] = CAT_VFO_SET_FREQ;
         else
            forwardCmd = 0;
      }
   }

   // Cache new RX frequency.  If radio is in RX mode then apply the frequency change to the main VFO   
   else if (staInput[4] == CAT_SAT_RX_SET_FREQ) {
      // Save the frequency for use in later RX command
      for (i = 0; i < CAT_CMD_LEN; i++)
          rxCommand[i] = staInput[i];

      // Save the frequency for later status responses, avoiding the mode byte in the status response
      for (i = 0; i < CAT_CMD_LEN - 1; i++)
          rxDummyStatus[i] = staInput[i];

      if (satMode) {
         if (mode == MODE_RX)
            staInput[4] = CAT_VFO_SET_FREQ;
         else
            forwardCmd = 0;
      }
   }
   
   // Enable command translation when radio switched to SAT_MODE.  We look for this
   //  command, but instead force SAT_MODE on the radio off
   else if (staInput[4] == CAT_SAT_MODE_ON) {
      satMode = 1;
      staInput[4] = CAT_SAT_MODE_OFF;
      reset_radio_rx(0);
   }
   else if (staInput[4] == CAT_SAT_MODE_OFF) {
      satMode = 0;
      reset_radio_rx(0);
   }
   
   // Set up to receive a status response when we see a status request sent
   else if (staInput[4] == CAT_RX_GET_STATUS || staInput[4] == CAT_TX_GET_STATUS) {
      reset_radio_rx(1);
   }
   
   // Handle request for SAT frequencies.  Send the command if in the correct mode,
   //  otherwise reply with dummy data
   else if (staInput[4] == CAT_SAT_RX_GET_FREQ) {
      // Only snoop the response if not in sat mode
      if (!satMode)
         reset_radio_rx(CAT_CMD_LEN);
      else {
         if (mode == MODE_RX || !rxStatusValid) {
            reset_radio_rx(CAT_CMD_LEN);
            staInput[4] = CAT_VFO_GET_FREQ;
            queryMode = MODE_RX;
         }
         else {
            // Dummy RX response
            forwardCmd = 0;
            dummy_rx_status();
         }
      }
   }
   
   else if (staInput[4] == CAT_SAT_TX_GET_FREQ) {
      // Only snoop the response if not in sat mode
      if (!satMode)
         reset_radio_rx(CAT_CMD_LEN);
      else {
         if (mode == MODE_TX || !txStatusValid) {
            reset_radio_rx(CAT_CMD_LEN);
            staInput[4] = CAT_VFO_GET_FREQ;
            queryMode = MODE_TX;
         }
         else {
            // Dummy TX response
            forwardCmd = 0;
            dummy_tx_status();
         }
      }
   }
   
   else
      reset_radio_rx(0);
   
   // Finally forward the command along to the radio     
   if (forwardCmd) {
#if DEBUG        
         print_input("Other", staInput);
#else
         send_to_radio(staInput);
#endif
   }
}

// Parses and handles serial input from the radio once an entire input command has been received
void handle_radio_input()
{
   int i;
  
   // There are two types of commands.  The 1 byte status command which is always forwarded
   // back to the computer.  
   if (satMode && radioCount == 1) {
      Serial.write(radioInput[0]);
   }
   // A full response contains frequency information
   else if (radioCount == CAT_CMD_LEN) {
      // Note the use of queryMode instead of mode.  This covers the case where the PTT mode
      // was changed before the response was received by the Arduino
      if (queryMode == MODE_RX) {
         // Cache this RX response and mark it as valid
         for (i = 0; i < CAT_CMD_LEN; i++)
            rxDummyStatus[i] = radioInput[i];
         rxStatusValid = 1;
      }
      else if (queryMode == MODE_TX) {
         // Cache this RX response and mark it as valid
         for (i = 0; i < CAT_CMD_LEN; i++)
            txDummyStatus[i] = radioInput[i];
         txStatusValid = 1;
      }
      
      // Send the frequency response to the computer if the query mode and PTT mode are the same
      if (satMode && mode == queryMode) {
         for (i = 0; i < CAT_CMD_LEN; i++)
            Serial.write(radioInput[i]);
      }
   }
}

// run over and over
void loop()
{
   int i;
   
   check_ptt();
   
   // Handle and input characters from the computer facing serial port
   while (Serial.available()) {
      // Read and buffer one byte at a time
      staInput[staCount++] = Serial.read();
      
      // Process a command from the computer once an entire command has been received
      if (staCount == CAT_CMD_LEN) {
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
      
      // Ignore the character if we aren't expecting input from the radio
      if (radioExpLen > 0) {
          // Handle the radio input once we have received an entire command
          if (radioExpLen == radioCount) {
             handle_radio_input();
             if (!satMode) {
                for (i = 0; i < radioCount; i++)
                   Serial.write(radioInput[i]);
             }
             reset_radio_rx(0);
          }
      }
 
      // Forward the radio back to the computer if not in sat mode
      if (!satMode) {
         for (i = 0; i < radioCount; i++)
            Serial.write(radioInput[i]);
      }
       
      // Clear the radio serial RX snooping state if we aren't expecting radio input
      if (radioCount > radioExpLen)
          reset_radio_rx(0);
          
      // We check PTT frequently to be very responsive without interrupts
      check_ptt();
   }  
}
