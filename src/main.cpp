/* Updated by Todd Meyer in March 2024 to add more expressiveness to the light and add support for the sustain pedal
 *
 * Original code is copyright (c) 2022 Dave Dribin
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Arduino.h>
#include <FastLED.h>
#include <MIDI.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// wifi creds
const char *ssid = "your_ssid";
const char *password = "password";

static const int NUM_LEDS = 64;
static const int START_LED = 0;

// Specify LED pins being used
#define DATA_PIN D7
#define CLOCK_PIN D5

// Set up led array & define constants
static const int saturation = 255; //code below does not change this
int brightness = 255; //this is default value but will change for each keypress
CRGB leds[NUM_LEDS];
int hueshift = 0;


// Set up keys array & pedal based on how many keys on the keyboard
static const int NUM_KEYS = 76; // update to match how many keys in your keyboard
bool keys[NUM_KEYS]; // Declaraion of keys array, will store t/f for each key to track which keys are current pressed
byte velocities[NUM_KEYS]; // Declaration of velocities array, how hard the keys are being pressed
byte pedal = 0;
bool sustain = false;  // will keep track of whether the sustain pedal is being pressed or not

// this created the MIDI instance as required by the library
MIDI_CREATE_DEFAULT_INSTANCE();

// declare the MIDI functions
static void handleNoteOn(byte channel, byte note, byte velocity);
static void handleNoteOff(byte channel, byte note, byte velocity);
static void handleControlChange(byte channel, byte number, byte value);

// Integer division with rounding using only integer operations
// https://stackoverflow.com/questions/2422712/rounding-integer-division-instead-of-truncating/2422723#2422723
// https://blog.pkh.me/p/36-figuring-out-round%2C-floor-and-ceil-with-integer-division.html
long div_round_closest(long dividend, long divisor)
{
    return (dividend + (divisor / 2)) / divisor;
}

// Custom version of map() that rounds fractional values instead of truncating.
long my_map(long x, long in_min, long in_max, long out_min, long out_max)
{
    long numerator = (x - in_min) * (out_max - out_min);
    long denominator = (in_max - in_min);
    long result = div_round_closest(numerator, denominator) + out_min;
    return result;
}

void setup()
{
    // initialize the wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // initialize the leds
    FastLED.addLeds<SK9822, DATA_PIN, CLOCK_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(180); // 1-255, this is the max brightness limiter for all other operations in the loop

    // initialize midi listening
    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI.setHandleControlChange(handleControlChange);

    // initialize piano keys all to 'off' (false)
    for (int i = 0; i < NUM_KEYS; i++)
    {
        keys[i] = false;
    }

    // Initialize the Arduino OTA library for wireless updates
    ArduinoOTA.onStart([]() {});
    ArduinoOTA.onEnd([]() {});
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
    ArduinoOTA.onError([](ota_error_t error)
                       {
    if (error == OTA_AUTH_ERROR) ;
    else if (error == OTA_BEGIN_ERROR);
    else if (error == OTA_CONNECT_ERROR);
    else if (error == OTA_RECEIVE_ERROR);
    else if (error == OTA_END_ERROR); });

    ArduinoOTA.begin();
}

void loop()
{
    //listen for OTA requests and handle as needed
    ArduinoOTA.handle();

    //listen for MIDI inputs and handle as needed
    MIDI.read();
    
    // Spread/blur the light out to more leds than just 1 per key. This uses the blur1d() function from fastLED library
    // If the sustain pedal is being pressed, fade the leds more slowly and spread the light more, otherwise fade them more quickly
    // This makes the light more closely match the duration and fade of the audible sound
    if (sustain == true)
    {
        EVERY_N_MILLISECONDS(100){blur1d(leds, NUM_LEDS, 85);} //fade them slowly and also blur them more - blur1d also fades leds
        EVERY_N_MILLISECONDS(500){hueshift = (hueshift + 1) % 256;} //shift the hues gradually as sustain is used (for fun)

    }
    else
    {
        EVERY_N_MILLISECONDS(6){blur1d(leds, NUM_LEDS, 25);} //fade them quickly
    }

    // Now, light up any keys that are currently pressed (as determined by handleNoteOn and handleNoteOff)
    for (int i = 0; i < NUM_KEYS; i++)
    {
        if (keys[i])
        {
            int led = my_map(i, 0, NUM_KEYS - 1, START_LED, NUM_LEDS - 1);
            int hue = my_map(i, 0, NUM_KEYS - 1, 0, 255);
            hue = (hue + hueshift) % 256;

            // Calculate brightness based on velocity
            byte velocity = velocities[i];

            // Calculate brightness based on velocity
            brightness = constrain(exp(velocity / 127.0 * log(255.0 / 40.0)) * 40.0, 40.0, 255.0);

            // Set the color and brightness for all leds corresponding to the keys currently being pressed
            leds[led] = CHSV(hue, saturation, brightness);
        }
    }

    FastLED.show();
}

/// Note A0 MIDI value (not sure exactly how to get this, just used trial and error)
static const byte MIN_PIANO_MIDI_NOTE = 28;
/// Note C8 MIDI value (not sure exactly how to get this, just used trial and error)
static const byte MAX_PIANO_MIDI_NOTE = 116;

// Do things when a key is pressed
static void handleNoteOn(byte channel, byte note, byte velocity)
{
    //update the keys array with which keys are pressed/on
    keys[note - MIN_PIANO_MIDI_NOTE] = true;

    //update the velocities array with the velocity of the key press
    velocities[note - MIN_PIANO_MIDI_NOTE] = velocity;
}

// Do things when a key is released
static void handleNoteOff(byte channel, byte note, byte velocity)
{
    // if the note is between the range of notes
    if ((note >= MIN_PIANO_MIDI_NOTE) && (note <= MAX_PIANO_MIDI_NOTE))
    {
        // update the corresponding value in keys array to 'false' which means they are not being pressed anymore
        keys[note - MIN_PIANO_MIDI_NOTE] = false;
    }
}

static void handleControlChange(byte channel, byte number, byte value)
{
    // channel 64 = damper / sustain pedal - set sustain to true if pressed, back to false when not
    if( number == 64 ){
        if( value >= 64 ){
            sustain = true;
        } else {
            sustain = false;
        }
    }
}
