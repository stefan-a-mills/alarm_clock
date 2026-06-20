// BUG LIST:
// Buzzer humming (isolate electrically with relay? Try capacitor smoothing thing?)
// When buzz & flash occur simultaneously, there can be a strange interaction where the buzzer makes a small noise. Too much current draw? Could just be printing to serial
// Buzzer can make a noise when IR receiver is triggered
// Perhaps (IR) receiver pin should be pulled up (or down)? ChatGPT says probably unnecessary

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
unsigned int timeMinutes = 1284;
// Track when the alarm should go off - initialise at 540 (9AM)
unsigned int alarmTimeMinutes = 362;
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
// The time in minutes for the display to switch off
unsigned int sleepStartMinutes = 1320;
// The time in minutes for the display to switch back on
unsigned int sleepEndMinutes = 360;
// Whether the display should be flashing (e.g. during time set)
bool flashing = false;
// Length of flash half-cycle
unsigned int flashLength = 500;
// Whether the display should be on or not (part of the flashing cycle)
bool displayOn = true;
// The time, in terms of digits to display on the clock
int timeDisplay[4] = {0, 0, 0, 0};
// A multiplier if you want the clock to go faster - useful for testing
int clockSpeed = 1;
// A bool to determine if a 'menu' screen should be showing instead of the actual time
// e.g. when setting the alarm time
bool menuOn = false;
// The time to display in the menu screen
int menuDisplay[4] = {0, 0, 0, 0};
// Which digit in the menu is being set currently?
int menuDigit = 0;
// Determines whether the alarm menu is active
bool alarmMenu = false;
// Clock menu active state
bool clockMenu = false;
// Menu for setting the start time for the display to switch off
bool sleepStartMenu = false;
// Menu for setting the end time for the display to switch off
bool sleepEndMenu = false;


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

      // Choose from the actual time or the menu, depending on which is active
      if (!menuOn){
        // The number to be displayed is based on the current time, just need to choose the right digit
        num = timeDisplay[activeDigit];
      } else {
        num = menuDisplay[activeDigit];
      }

      digitalWrite(latch, LOW);
      shiftOut(data, clock, MSBFIRST, table[num]);
      digitalWrite(latch, HIGH);
  
}

void DisplayDigit() {
  
  // If flashing is enabled, check whether it's time to switch the display between on/off
  if (flashing && currentMillis - flashMillis >= flashLength) {
    flashMillis = currentMillis;
    displayOn = !displayOn;
    //Serial.println("Flash switch");
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
      // Switch the display on/off if it's sleep time. Deliberately putting sleepEnd
      // in second place so if the times are the same the display ends up on
      if (timeMinutes == sleepStartMinutes) {
        displayOn = false;
      }
      if (timeMinutes == sleepEndMinutes) {
        displayOn = true;
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

// For converting 4 digit time to minutes
int DisplayToMins(int display[4]){
  int mins = 0;
  mins += display[3];
  mins += display[2]*10;
  mins += display[1]*60;
  mins += display[0]*600;
  return(mins);
}

// When a menu button is triggered, switch the menu state and enable/disable flashing correspondingly 
void TriggerMenu() {
  // Toggle the menu state
  menuOn = !menuOn;
  // If the menu is now turned off, make sure you disable all menu bools and flashing
  if (!menuOn) {
    alarmMenu = false;
    clockMenu = false;
    sleepStartMenu = false;
    sleepEndMenu = false;

    flashing = false;
    // Make sure the display is on, e.g. in case menu exits while it's flashed off
    displayOn = true;
    // Reset menu display so next time it is activated it will be 0000
    for (int i = 0; i < 4; i++) {
    menuDisplay[i] = 0;
    }
  // If the menu is now on, make the screen flash and set the selected digit to the first one
  } else {
    flashing = true;
    // Make sure we're starting with the first menuDigit being the active one to set
    menuDigit = 0;
  }
}

// Setting digits in the menu, and when all four are set, updating whichever setting was being set, e.g. alarm or time
void SetDigit (int digit){
  // Only do something if we're in the menu screen
  if (menuOn) {
    // Update the menu with whatever digit triggered the function, e.g. '2'
    menuDisplay[menuDigit] = digit;
    // If we've not just set the last digit, update which digit we're setting
    // Otherwise, update whatever we were setting and then close out the menu
    if (menuDigit < 3) {
      menuDigit += 1;
    } else {
      // Set whichever feature was being set (e.g. clock, alarm, sleep)
      if (alarmMenu) {
        alarmTimeMinutes = DisplayToMins(menuDisplay);
      } else if (clockMenu){
        timeMinutes = DisplayToMins(menuDisplay);
        // Reset the timer so there's a whole minute until the clock updates with a new minute
        clockMillis = currentMillis;
        // Update the display
        for (int i = 0; i < 4; i++) {
          timeDisplay[i] = menuDisplay[i];
        }
      } else if (sleepStartMenu){
        sleepStartMinutes = DisplayToMins(menuDisplay);
      } else if (sleepEndMenu){
        sleepEndMinutes = DisplayToMins(menuDisplay);
      }
      
      // This function will make sure flashing gets disabled and the menuDisplay is reset for next time
      TriggerMenu();
    }
    
  }
}

// A list of actions to take for each button that could be pressed
void RemoteActions() {
    Serial.print("IR code: 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    switch (IrReceiver.decodedIRData.decodedRawData)
  {
    // Power button can be used to stop the alarm (not the same as toggling it as per further down)
    case 0xBA45FF00: Serial.println("POWER");  alarmActive = false; noNewTone(buzzer);break;
    // This will be the 'set alarm' button
    case 0xB847FF00: Serial.println("FUNC/STOP"); alarmMenu = true; TriggerMenu(); break;
    // This will be the 'set time' button
    case 0xB946FF00: Serial.println("VOL+"); clockMenu = true; TriggerMenu(); break;
    // Button for setting (display) sleep start time
    case 0xBB44FF00: Serial.println("FAST BACK"); sleepStartMenu = true; TriggerMenu();   break;
    // Button for setting (display) sleep end time
    case 0xBF40FF00: Serial.println("PAUSE");  sleepEndMenu = true; TriggerMenu();  break;
    // Button toggles whether the alarm should activate
    case 0xBC43FF00: Serial.println("FAST FORWARD"); alarmOn = !alarmOn;  break;
    // Button toggles whether the screen is currently on/off
    case 0xF807FF00: Serial.println("DOWN");  displayOn = !displayOn;  break;
    case 0xEA15FF00: Serial.println("VOL-");    break;
    case 0xF609FF00: Serial.println("UP");    break;
    case 0xE619FF00: Serial.println("EQ");    break;
    case 0xF20DFF00: Serial.println("ST/REPT");    break;
    // Try to set a digit if a number is pressed: function only does something if a menu is currently enabled
    case 0xE916FF00: Serial.println("0"); SetDigit(0);    break;
    case 0xF30CFF00: Serial.println("1");  SetDigit(1);   break;
    case 0xE718FF00: Serial.println("2");  SetDigit(2);   break;
    case 0xA15EFF00: Serial.println("3");  SetDigit(3);   break;
    case 0xF708FF00: Serial.println("4"); SetDigit(4);    break;
    case 0xE31CFF00: Serial.println("5"); SetDigit(5);    break;
    case 0xA55AFF00: Serial.println("6"); SetDigit(6);    break;
    case 0xBD42FF00: Serial.println("7"); SetDigit(7);    break;
    case 0xAD52FF00: Serial.println("8");  SetDigit(8);   break;
    case 0xB54AFF00: Serial.println("9");  SetDigit(9);   break;
    default:
      Serial.println(" other button   ");
  }// End Case

  IrReceiver.resume();
}

void loop() {

  currentMillis = millis();
  // Check if a minute has passed, if it has, update the time
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
