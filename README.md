<h1>FoosBall Presence Detector</h1>

<h2>Hardware</h2>
The hardware we're currently using for this is pretty simple. 

<h3>Power Supply</h3>
There's a 3.3 / 5V power supply similar to the ones at https://smile.amazon.com/gp/product/B01LCL6K0O/ref=oh_aui_detailpage_o09_s01?ie=UTF8&psc=1. It will work with pretty much any thing that supplies between 6 - 12 V to the input.

<h3>Distance Sensor</h3>
The distance sensor we're using is an Ultrasonic one. Similar to the one at https://www.mouser.com/datasheet/2/813/HCSR04-1022824.pdf . 

<h3>Microcontroller</h3>
The microcontroller we're using is a specific variant of an ESP32. It's the Heltec WifiKit 32. You can find details about the controller at http://www.heltec.cn/project/wifi-kit-32/?lang=en. I'm _mostly_ using the Arduino IDE for development of this, and you can find instrucitons for setting up the Aurdio IDE to support this board at https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series#instructions.

<h2>Sofware</h2>
You'll need an additional file that contains some of the secrets to actually make this work. Talk to Jason for a copy.

<h3>Concept</h3>
The foos detector just looks for an object in a certain range. If it detects an object there, after some time, it'll post to slack indicating so. If, after an object is detected in the range, and then the object is no longer there, it'll post to slack indicating that no one is there anymore.

Pretty simple really.

There's also some logic for the device to get the time from some NTP servers, so that it sleeps during non-working time (and won't post). Some of the stuff around time checks is pretty hacky. I don't really know how some of that works.

<h3>Important vairables for tuning</h3>

led_pin - This is the "onboard" pin on the Heltec. It's a white LED and is used to "show" when the device is detecting an object in the "detection range".
trig_pin - This is the Heltec pin that is used to send the "ping".
echo_pin - This is the Heltec pin that listens for the "ping" to be returned.
minDuration - This is used to compare the duration (lenth the echo pin is high) to see if the obect it too close to be considered "in range". A lower number means an object is closer than a high number. minDuration sets the minimum value for the "detection range". Conceptually this could be 0, but the door also covers where we put the unit, so we need to ignore the door with a "minumum distance" in the "detection range".
maxDuration - Similar to minDuration, but on the high end. The wall on the far side of the room is within range and we need to set this value to something less than that wall (in testing the wall has been ~17300).
numChecks - The number of times the unit need to detect a presence before posting a notification. It also has to not detect a presence this number of times to post.
pingDuration - This is how frequently the presence check happens (in millis).

<h3>General Operation</h3>
Just in general, if you're new to Arduino, the way this works is when the microprocessor boots (or wakes from sleep) it runs the setup function.
After than, as fast as possible it runs the loop function over and over.
In the case of this, I'm doing a time check early and if it's outside of work hours I have the processor go back to sleep for 30 minutes. After that it wakes again and checks the time.

If it's during working hours it pings at the frequency of pingDuration to see if it sees people. There are some errors in how often people are detected. To account for this I make the device "accumulate" a number of detections, which is controlled by nuChecks. It's a bit of a tradeoff about being right vs. being quick.
If it finds people enough times, it'll then post to slack that it found someone. Once it posts that it found someone, if it doesn't find anyone it'll start decrementing the counter until it gets to 0, at which point it will post that there isn't anyone there anymore.
