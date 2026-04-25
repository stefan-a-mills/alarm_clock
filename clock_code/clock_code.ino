// BUG LIST:
// Buzzer humming (isolate electrically with relay? Try capacitor smoothing thing?)
// When buzz & flash occur simultaneously, there can be a strange interaction where the buzzer makes a small noise. Too much current draw? Could just be printing to serial


#include "pitches.h"


#include <IRremote.h>
#include <NewTone.h> // Using 'NewTone' because it uses Timer1, so no conflict with IRremote





const int latch = 9;  // 74HC595 STCP
const int clock = 10; // 74HC595 SHCP
const int data = 8;   // 74HC595 DS
const int receiver = 11; // Data pin on IR receiver
const int buzzer = 13;
const int alarmStop = 7; // Pin for digital input to turn off alarm

int digits[4] = {2, 3, 4, 5}; // Common pins for each digit

// Variables used for timing 'simultaneous' processes (i.e. avoid delay())
// Note: 'long' type needed to avoid overflows
unsigned long currentMillis = 0;
unsigned long clockMillis = 0;
unsigned long multiplexMillis = 0;
unsigned long alarmMillis = 0;
unsigned long flashMillis = 0;

int activeDigit = 0;
unsigned char num = 0;
// Track what the time is in minutes - initialise at 0 (midnight)
unsigned int timeMinutes = 0;
// Track when the alarm should go off - initialise at 540 (9AM)
unsigned int alarmTimeMinutes = 20;
// Whether there is an alarm set
bool alarmOn = true;
// Whether the alarm is currently activated
bool alarmActive = false;
// Whether the alarm should be making a noise right now (note it alternates on/off when active)
bool alarmBuzzing = false;
// How long (in milliseconds) the each buzz of the alarm should last
unsigned long buzzLength = 1000;
// The note (from 'pitches' library) for the alarm to play
int buzzPitch = NOTE_C6;
// Whether the display should be flashing (e.g. during time set)
bool flashing = false;
// Length of flash half-cycle
unsigned int flashLength = 500;
// Whether the display should be on or not (part of the flashing cycle)
bool displayOn = true;
// The time, in terms of digits to display on the clock
int timeDisplay[4] = {0, 0, 0, 0};
// A multiplier if you want the clock to go faster - useful for testing
int clockSpeed = 200;


// Table of numbers from 0 to 9, with letters afterwards
unsigned char table[] = {
  0x3f, 0x06, 0x5b, 0x4f,
  0x66, 0x6d, 0x7d, 0x07,
  0x7f, 0x6f, 0x77, 0x7c,
  0x39, 0x5e, 0x79, 0x71, 0x00
};

void setup() {
  pinMode(latch, OUTPUT);
  pinMode(clock, OUTPUT);
  pinMode(data, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(alarmStop, INPUT_PULLUP); 
  Serial.begin(9600);
  while (! Serial); // Wait untilSerial is ready - Leonardo
  Serial.println("Serial monitor running");
  for (int i = 0; i < 4; i++) {
    pinMode(digits[i], OUTPUT);
    // Assumes active-LOW digit control (common cathode via transistors)
    digitalWrite(digits[i], HIGH); // Turn off all digits initially
  }

  //pinMode(receiver, INPUT);
  IrReceiver.begin(receiver, ENABLE_LED_FEEDBACK);
}

// Updates the shift register with what to show on the screen. Note this is for one digit at a time
void UpdateShiftReg(int activeDigit){

      // The number to be displayed is based on the current time, just need to choose the right digit
      // @@@@@@@@ This is where I probably want to have an if/else to decide which array activeDigit is being drawn from
      num = timeDisplay[activeDigit];

      digitalWrite(latch, LOW);
      shiftOut(data, clock, MSBFIRST, table[num]);
      digitalWrite(latch, HIGH);
  
}

void DisplayDigit() {
  
  if (flashing && currentMillis - flashMillis >= flashLength) {
    flashMillis = currentMillis;
    displayOn = !displayOn;
    Serial.println("Flash switch");
  }

  if (currentMillis - multiplexMillis >= 1) {
    // Updating to currentMillis rather than +1 seems to reduce flicker, probably because it ensures each digit gets at least 1ms
    multiplexMillis = currentMillis;

    // Update which segment is active
    if (activeDigit < 3) {
      activeDigit += 1;
    } else {
      activeDigit = 0;
    }

    // Turn off all digits first
    for (int i = 0; i < 4; i++) digitalWrite(digits[i], HIGH);

    // Update the shift register with the appropriate number, given the time and active digit
    UpdateShiftReg(activeDigit);

    // Enable the selected digit, if the screen is meant to be on currently
    if (displayOn == true){
      digitalWrite(digits[activeDigit], LOW);
    }
  }
}

// Update the clock each minute
// Also see if the new minute means the alarm should be turned on
void UpdateClock(){
  if (currentMillis - clockMillis >= 60000/clockSpeed) {
      clockMillis += 60000/clockSpeed;
      // If it's not about to be midnight, add a minute. Otherwise, go back to midnight
      if (timeMinutes < 1439) {
       timeMinutes += 1;
      } else {
        timeMinutes = 0;
      }

      // Integer division automatically floors (to give an integer result)
      int hours = timeMinutes / 60;
      int mins = timeMinutes % 60;

      // Set the individual digits, locations 0-3 in the array timeDisplay
      if (hours < 10) {
        timeDisplay[0] = 0;
        timeDisplay[1] = hours;
      } else {
        timeDisplay[0] = hours / 10;
        timeDisplay[1] = hours % 10;
      }
      if (mins < 10) {
        timeDisplay[2] = 0;
        timeDisplay[3] = mins;
      } else {
        timeDisplay[2] = mins / 10;
        timeDisplay[3] = mins % 10;
      }

      // Set the alarm active if it is time. Note this has no practical effect if alarmOn == false
      if (timeMinutes == alarmTimeMinutes) {
        alarmActive = true;
        alarmMillis = currentMillis;
        alarmBuzzing = true;
      }
  }
}

// Code to play the alarm
// Note that the alarm is set active in the UpdateClock function
void PlayAlarm (){
  // If the length of a buzz/silence has played out, switch whether it's buzzing or silent
  // Note that the '(New)tone' function is toggled, so it doesn't need calling every loop cycle, just when
  // its state is being changed (i.e. on/off)
  if (currentMillis-alarmMillis >= buzzLength) {

    alarmMillis = currentMillis;
    alarmBuzzing = !alarmBuzzing;

    //Serial.println("Changing buzz state");

    if (alarmBuzzing) {
      // Can optionally inset a length as the third param, nice if you want it to be shorter than
      // the full half-cycle
      NewTone(buzzer, buzzPitch, buzzLength/4);
    } else {

      noNewTone(buzzer);
    }
  }

}

// A list of actions to take for each button that could be pressed
void RemoteActions() {
    Serial.print("IR code: 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    switch (IrReceiver.decodedIRData.decodedRawData)
  {
    case 0xBA45FF00: Serial.println("POWER"); break;
    case 0xB847FF00: Serial.println("FUNC/STOP"); flashing = !flashing; break;
    case 0xB946FF00: Serial.println("VOL+"); break;
    case 0xBB44FF00: Serial.println("FAST BACK");    break;
    case 0xBF40FF00: Serial.println("PAUSE");    break;
    case 0xBC43FF00: Serial.println("FAST FORWARD");   break;
    case 0xF807FF00: Serial.println("DOWN");    break;
    case 0xEA15FF00: Serial.println("VOL-");    break;
    case 0xF609FF00: Serial.println("UP");    break;
    case 0xE619FF00: Serial.println("EQ");    break;
    case 0xF20DFF00: Serial.println("ST/REPT");    break;
    case 0xE916FF00: Serial.println("0");    break;
    case 0xF30CFF00: Serial.println("1");    break;
    case 0xE718FF00: Serial.println("2");    break;
    case 0xA15EFF00: Serial.println("3");    break;
    case 0xF708FF00: Serial.println("4");    break;
    case 0xE31CFF00: Serial.println("5");    break;
    case 0xA55AFF00: Serial.println("6");    break;
    case 0xBD42FF00: Serial.println("7");    break;
    case 0xAD52FF00: Serial.println("8");    break;
    case 0xB54AFF00: Serial.println("9");    break;
    default:
      Serial.println(" other button   ");
  }// End Case

  IrReceiver.resume();
}

void loop() {

  currentMillis = millis();
  UpdateClock();
  DisplayDigit();

  // Checks if the button to switch off the alarm is pressed
  // Could put this in a function but seems overkill, just a simple check & act
  if (digitalRead(alarmStop) == LOW) {
    //Serial.println("Switching off alarm");
    alarmActive = false;
    noNewTone(buzzer);
  }

  if (alarmOn && alarmActive) {
    PlayAlarm();
  }

  // IR receiver stuff
  if (IrReceiver.decode())
  {
    RemoteActions();
  }
  
}
