
#define POSTTOSLACK = true;

//#define DEBUG = true;
//#define VERBOSE = true;
#ifdef DEBUG
  bool debugging = true;
#endif 
// For the OLED Screen. Currently not being used at all
// This actually may be needed to turn the screen off during low power mode. We'll see.
//#include "SSD1306.h"
//SSD1306  display(0x3c, 4, 15);

// Used for figuring out time, both related to when to post and when to sleep
//#include <Time.h>
#include <TimeLib.h>
#include "mwwifi.h"
const String foundMessage = "I see foos people, I think (I'm still figuring out this new room)";
const String goneMessage = "Foos people have left me (I think?)";
const String timeError = "I can't figure out what time it is";

// Enables the ESP32 to connect to wifi and SSL (slack).
//#include <WiFi.h>
#include <WiFiClientSecure.h>

// tripped is a variable that is used to check whether or not
// presence has been detected. True indicates that someone has 
// been detected. False indicates that no one has been detected.
// This should really be passed between functions. 
bool tripped;

// This gets incremented when the US sensor finds something in range.
// it's bound to 0 on the low side and whatever is declared in numChecks
// This should really be passed between functions. 
int somethingInRange=0;

// this is used to light the led when something is being detected
// doesn't indicate that someone is found, but just detected.
// for UNO this is 13. For Heltec ESP32 it's 25.
const int led_pin = 25;

// Define the pins used for the UltraSonic Distance sensor
// These are the pins on the UNO. Will have to see what's right on 
// the ESP32. 2 and 17 are what I'm using on the Heltec
const int trig_pin = 2;
const int echo_pin = 17;

// Define the Maximum and Minimum durations that you want to listen for
// These are "unitless" and are just measuring raw time. As I go through
// the LLS they will need to be adjusted.

const int minDuration = 300;
const int maxDuration = 6000;
const int numChecks = 25;

// Used to measure the time from the ping to the echo.
// This should really be passed between functions. 
int duration;

// The constrol how often the ping happens (and whether the status is currently high or low)
const int pingDuration = 500;

// Check to see if the trigger is active or not. Used by the trigger function to swap from high
// to low and vice versa
bool trig_status = false;

// Generic for checking time loops.
// These should probably get replaced now that there is actually a time library
// TODO - Later
unsigned long timer = 0;
unsigned long oldtimer;
unsigned long flashtimer;


//Variables related to get time stuff
const char* ntpServer = "pool.ntp.org";
// I'm not really using these correctly. I'm not sure how they work.
const long gmtOffset_sec = -300;
const long daylightOffset_sec = -240;
// There's a hackish implementation that uses this as the offset. It'll need to change to 
// -5 when Daylight Savings Time ends.
const int zuluOffset = -4;

// Still unclear how this works, but it's working.
struct tm timeinfo;

// Used by the sleep function 
// scales from microseconds to seconds 
const int usToSeconds = 1000000;
// length of sleep cycle, short for testing. Probably like 1/2 in prod.
// This is in seconds (because the sleep function uses microseconds
// This maxes somewhere between 1/2 and 1 hour (max int in microseconds)
const int sleepTime = 1800;

// tells the application whether it's in "first start" mode or continuing mode. 
// Get's set to true in Setup, then false the first time it detects the door
// after startup.
bool coldBoot;

// Setup runs once as the board comes on. Used to init stuff
void setup() {
  coldBoot = true;
  oldtimer = millis();
  timer = oldtimer;
  // Set the led_pin as an output so that it can blink
  pinMode(led_pin, OUTPUT);

  // Set the echo pins to input and out put accordingly
  pinMode(trig_pin, OUTPUT);
  pinMode(echo_pin, INPUT);

  // Sets the "Detected" to 0 to start things off
  tripped = false;

  flashtimer = millis();

  #ifdef DEBUG
  Serial.begin(115200);
  #endif
  
  //Need to add a watchdog loop for this, for now the function will handle retrying
  bool wifiConnected = connectToWifi();

  // For checking the time... Don't know how this workses.
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // This appears to actually get the time from the NTP server.
  setupTime();
  // This is hacky. I need a more generic way to correct the time
  fixTime();
  
  // This is a very basic error check. Default year for this board is like 1970. If it's less than 2018
  // the time wasn't set properly, so try again.
  // There should probably be a break out of this loop, but TODO.
  // I've seen the time check fail a couple times. I just added this and don't know if it actually will work yet.
  int myYear = year();
  int timeCount = 0;
  while (myYear < 2018) {
    timeCount++;
    setupTime();
    fixTime();
    if (timeCount > 20) {
      post("Does anyone know what time it is?");
    }
  }


  
}

// This generally just checks if the current time is withing working hours.
// It's currently being used to control when the thing posts as well as when
// the unit sleeps.
bool workTime() {
  int myWeekDay = weekday();
  int myHour = hour();
  #ifdef VERBOSE
  Serial.print("Weekday: ");
  Serial.println(myWeekDay);
  Serial.print("myHour: ");
  Serial.println(myHour);
  #endif
  if (weekday() > 1) {
    if (weekday() < 7) {
      if (myHour > 7) {
        if (myHour < 17) {
          return true;
        }
      }
    }
  }
  return false;
}


// Puts the ESP32 into some sort of sleep mode
// I need to measure the current to understand
// how well this is actually working and possibly 
// turn of the screen manually as well.
void goToSleep () {
  #ifdef DEBUG
  Serial.println("Going to sleep now");
  #endif
  esp_sleep_enable_timer_wakeup(sleepTime * usToSeconds);
  esp_deep_sleep_start();
}

// This apparently calls the ntp server decleared to get the time???
// Still unclear on how this works.
void setupTime() {
  if(!getLocalTime(&timeinfo)) {
    
    #ifdef DEBUG
    Serial.println("Failed to obtain time");
    #endif
    return;
  }
  time_t myTime = mktime ( &timeinfo );
  setTime(myTime);
}


bool connectToWifi() {
  const char* ssid     =  NETWORK;
  const char* password = SECRET;


  #ifdef DEBUG
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  //Serial.println(password);
  #endif
  WiFi.begin(ssid, password);
 
  int connectionTimeout = 0;
  // IF the wifi connection fails (happens quite a bit, especially on startup)
  while (WiFi.status() != WL_CONNECTED) {
      delay(1500);
      #ifdef DEBUG
      Serial.print(".");
      #endif
      connectionTimeout++;
      //Keep trying for 10 seconds. If it hasn't connected after 10 seconds re-init the wifi
      if(connectionTimeout > 20) {  
        Serial.println(WiFi.status());
        Serial.println("Hmmm, resetting");
        connectionTimeout = 0;
        WiFi.begin(ssid, password);        
        }
    }
    #ifdef DEBUG
    Serial.print("Connected to: ");
    Serial.println(ssid);
    #endif
}

void post(String message) {
  // Web hook address.
  const char* HOST = "hooks.slack.com";

  // This points at the slack-jira-test-pub slack channel. It'll need to point to
  // the nh foos room eventually
  // TODO
  const String URL = SLACKURL;
  
  // These are the messages that the bot posts to slack.
  // Some day it'd be nice to have a buch of meesages and pick one randomly
  // TODO

  #ifdef DEBUG
  Serial.println("Connecting to host...");
  #endif
  WiFiClientSecure client;
  if (!client.connect(HOST, 443)) {
    #ifdef DEBUG
    Serial.println("Connection failed");
    #endif
    client.stop();
    // If it fails to post, try again?
    post(message);
    return;
  }
  #ifdef DEBUG
  Serial.println("Connected to host");
  #endif

  String request = "";
  request += "POST ";
  request += URL;
  request += " HTTP/1.1\r\n";

  request += "Host: ";
  request += HOST;
  request += "\r\n";

  int len = message.length() + 12;  // JSON wrapper length
  request += "Content-Length: ";
  request += len;
  request += "\r\n";

  request += "Accept: application/json\r\n";
  request += "Connection: close\r\n";
  request += "Content-Type: application/json\r\n";

  request += "\r\n";
  
  request += "{\"text\": \"";
  request += message;
  request += "\"}";

  #ifdef DEBUG
  Serial.print(request);
  Serial.println();
  #endif
  #ifdef POSTTOSLACK
  Serial.println("Posting to Slack");
  client.print(request);
  #endif
  long timeout = millis() + 5000;
  while (!client.available()) {
    if (millis() > timeout) {
      #ifdef DEBUG
      Serial.println("Request timed out");
      #endif
      client.stop();
      return;
    }
  }
  #ifdef DEBUG
  Serial.println("Response:");
  #endif
  while (client.available()) {
    Serial.write(client.read());
  }
  #ifdef DEBUG
  Serial.println();
  Serial.println("Request complete");
  #endif
}





// Trigger basically does the active part of the ping
// it toggles the trig pin from high to low every time it is called
// It's really the same concept as a submarine. Send a "ping" and listen
// for the reflection to get range to target. This is the send the ping 
// part.
void trigger() {
    if (trig_status) {
      trig_status = false;
      digitalWrite(trig_pin, LOW);
    } else {
      trig_status = true;
      digitalWrite(trig_pin, HIGH);
    }
}




// This is basically the "listen" part of the ping
// If the return is more than the minimum time and
// less than the maximum time (which will be set based
// on the room dimensions) it activates.
// (I was having some sort of dumb trouble with bools and 
// just implemented this as an int. It should be a bool.
int listen() {
  duration = pulseIn(echo_pin, HIGH, 50000);
      #ifdef DEBUG
      Serial.print("Duration: ");
      Serial.println(duration);
      #endif
  int counter = 0;
  while (duration == 0) {
    oldtimer = timer;
    return 0;
    duration = pulseIn(echo_pin, HIGH, 50000);
    #ifdef DEBUG
    Serial.print("Catch 0 error");
    #endif
    if (counter > 100) {
      duration = 1;
      
      
    }
    counter++;
  }
      
  if (duration >= minDuration) {
    if (duration <= maxDuration) {
     
      return 1;   
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

// At this point this is a dumb abstraction. The only purpose it really serves
// is to turn the LED on / off and convert the int to a bool.
// Either just put listen in here and return a bool or put this in listen
bool presence() {

  int value = listen();
  if (value == 1) {
    digitalWrite(led_pin, HIGH);
    #ifdef DEBUG
    Serial.println("I see a person");
    #endif
    return true;
  } else {
    digitalWrite(led_pin, LOW);
    #ifdef DEBUG
    Serial.println("No person Seen");
    #endif
    return false;
  }
}


#ifdef DEBUG
void debugTimer (){
  Serial.print("timer: ");
  Serial.print(timer);
  Serial.print(" oldtimer: ");
  Serial.print(oldtimer);
  Serial.print(" pingDuration ");
  Serial.print(pingDuration);
}

void printDebugInfo () {
  Serial.print("SomethingInRange: ");
  Serial.print(somethingInRange);
  Serial.print(" trig_status: ");
  Serial.print(trig_status);
  Serial.print(" tripped: ");
  Serial.println(tripped);
}
#endif
void fixTime() {
  // This is hacky and doesn't adjust for Daylight Savings Time.
  // Should be fixed.
  // TODO
    Serial.println("Fixing the time offset");
    int myHour = hour();
    int myMinute = minute();
    int mySecond = second();
    int myDay = day();
    int myMonth = month();
    int myYear = year();
    int myWeekDay = weekday();
    int correctHour;

    Serial.print("TIME AS READ: ");
    Serial.print(myHour);
    Serial.print(":");
    Serial.print(myMinute);
    Serial.print(":");
    Serial.print(mySecond);
    Serial.print(" ");
    Serial.print(myMonth);
    Serial.print(" ");
    Serial.print(myDay);
    Serial.print(" ");
    Serial.print(myWeekDay);
    Serial.print(" ");
    Serial.println(myYear);
    if (myHour < -zuluOffset) {
      myDay = myDay - 1;
      myWeekDay = myWeekDay - 1;
      correctHour = 24 + myHour + zuluOffset;
    } else {
      correctHour = myHour + zuluOffset;
    }
    setTime(correctHour,myMinute,mySecond,myDay,myMonth,myYear);
    Serial.print("CORRECTED TIME: ");
    Serial.print(correctHour);
    Serial.print(":");
    Serial.print(myMinute);
    Serial.print(":");
    Serial.print(mySecond);
    Serial.print(" ");
    Serial.print(myMonth);
    Serial.print(" ");
    Serial.print(myDay);
    Serial.print(" ");
    Serial.print(myWeekDay);
    Serial.print(" ");
    Serial.println(myYear);

}
void printTime() {
    int myHour = hour();
    int myMinute = minute();
    int mySecond = second();
    int myDay = day();
    int myMonth = month();
    int myYear = year();
    
    Serial.print("Current TIME: ");
    Serial.print(myHour);
    Serial.print(":");
    Serial.print(myMinute);
    Serial.print(":");
    Serial.print(mySecond);
    Serial.print(" ");
    Serial.print(myMonth);
    Serial.print(" ");
    Serial.print(myDay);
    Serial.print(" ");
    Serial.println(myYear);
}

// Loop runs as fast as it can.
void loop() {
  bool workhours = workTime();
  #ifdef VERBOSE
  Serial.println("In the main loop");
  Serial.print("Working Time: ");
  Serial.println(workhours);
  #endif
  if (workhours) {
    #ifdef VERBOSE
    Serial.println("Passed work hours");
    #endif
    
    timer = millis();
    if (timer - flashtimer > 5000 ){
      digitalWrite(led_pin, HIGH);
      delay(50);
      digitalWrite(led_pin, LOW);
      flashtimer = timer;
    }
    // This didn't seem to get set when I had it in setup. Not sure why
    // Init the oldtimer variable.
    if (oldtimer == 0) {
      oldtimer = timer;
    }
  
    // In this case I don't want to check as fast as possible, it's silly.
    // Wait for whatever the duration is set to before checking.
    // If I were more clever I'd have the unit sleep between checks. I'm not
    // that clever yet.
    if (timer - oldtimer > pingDuration) {
      oldtimer = timer;
    
      if (coldBoot) {
        coldBoot = false;
        #ifdef DEBUG
        Serial.println("Cold Boot");
        #endif
        if (hour() >= 9) {
          post("Something strange... Was my power out? It's prime foosing hours and I just started up. :cry:");
        } else {
          //I could post a good morning wakey message here.
          #ifdef DEBUG
          Serial.println("Good morning, time for foosing");
          #endif
        }
        
      }
     
      
      // Send the ping
      //Serial.print(now());
      #ifdef DEBUG
      printTime();
      #endif
      trigger();
      // If the ping isn't high
      if (!trig_status) {
        // Check for an echo
        #ifdef DEBUG
        Serial.print("Detection Counter: ");
        Serial.println(somethingInRange);
        #endif
        if (presence()) {
          // If an echo is recieved and the counter is less than the 
          // max (where it triggers) increment the counter.
          
          if (somethingInRange < numChecks) {
            somethingInRange = somethingInRange + 1;
          }
        } else {
          // If there isn't an echo received, decrement the 
          // counter (as long as the counter isn't less than 0
          if (somethingInRange > 0) {
            somethingInRange = somethingInRange - 1;
          }
        }
        //This makes sure that it falls back below half the range
        if (somethingInRange >= (numChecks / 2)) {
          // Check to see if a person has been detected and a message sent
          if (!tripped) {
            // If a message hasn't been set and the counter is at the max, send the message
            // and set the flag indicating as much.
            if (somethingInRange >= (numChecks)) {
              #ifdef DEBUG
              Serial.println("foundMessage");
              #endif
              post(foundMessage);
              tripped = true; 
   
            }
          } 
        }  else {
          // If tripped has been set and the counter gets to 0
          // Send a message indicating so, and set the flag.
          if (somethingInRange == 0) {
            if (tripped) {
              #ifdef DEBUG
              Serial.println("goneMessage");
              #endif
              post(goneMessage);
              tripped = false;

            }
          }
        } 
      }
    } 
  } else {
    #ifdef VERBOSE
    Serial.println("Non-working hours");
    #endif
    goToSleep();
  }
}
