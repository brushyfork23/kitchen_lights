/*
Nick’s Undercabinet Lights
Motion sensing lights, also overwritable with a switch.
*/

// Required Libraries:
// Bounce2 - Version: Latest - https://github.com/thomasfredericks/Bounce2
// Chrono - Version: Latest - https://github.com/SofaPirate/Chrono
// FastLED - Version: Latest - https://github.com/FastLED/FastLED

// LED config
#include <FastLED.h>
#define LED_FPS     30
#define LED_PIN     3
#define COLOR_ORDER RGB
#define CHIPSET     WS2811
#define NUM_LEDS    8
#define BRIGHTNESS  90
#define HUE_RED 0
#define HUE_YELLOW 64
#define HUE_GREEN 96
CRGB leds[NUM_LEDS];

// PIR Sensor config
#include <Bounce2.h>
#define PIR_SENSOR_PIN  2
#define PIR_ENABLE_BTN_PIN 6
#define PIR_ENABLED LOW
Bounce pirSensor = Bounce();
Bounce pirEnableBtn = Bounce();
bool pirEnabled = true;

// Photocell config
#define PHOTOCELL_PIN A0
#define PHOTOCELL_READS_PER_MINUTE 60
unsigned long photocellReading = 0;

// Timer conifg
#include <Chrono.h>
#include <LightChrono.h>
LightChrono fillTimer;

// Manual On switch config
#define MANUAL_ON_PIN 4
#define MANUAL_ON LOW
Bounce manualOnSwitch = Bounce();
bool forceOn = false;

// State variables
enum states {
    IDLE_STATE,                     // Everything is off.  Awaiting instructions.
    LIGHTS_TRANSITION_ON_STATE,     // Manually engaged or movement detected.  Animate turning the LEDs on.
    LIGHTS_ON_STATE,                // LEDs on.  Awaiting input or detecting lack of movement.
    LIGHTS_TRANSITION_OFF_STATE,    // Manually disengaged or lack of movement detected.  Animate turning the LEDs off.
};
enum states state = IDLE_STATE;
bool isTransitioning = true;

// Day/Night brightness settings
#define PHOTOCELL_DAY_THRESHOLD 350
#define PHOTOCELL_NIGHT_THRESHOLD 200
uint8_t DAY_BRIGHTNESS = 255;
uint8_t NIGHT_BRIGHTNESS = 120;
enum modes {
  DAY_MODE,
  NIGHT_MODE,
};
enum modes mode = DAY_MODE;
uint8_t brightness = DAY_BRIGHTNESS;


// Misc variables
unsigned long TOTAL_ANIM_MILLIS = 700UL;

// Utility methods
void updateLeds(bool force=false) {
    // Update the LEDs
    static LightChrono ledsTimer;
    if (force || ledsTimer.hasPassed(1000UL / LED_FPS)) {
        ledsTimer.restart();
        FastLED.show();
    }
}

void updatePhotocell(bool force=false) {
  static LightChrono photoTimer;
  if (force || photoTimer.hasPassed(60000UL / PHOTOCELL_READS_PER_MINUTE)) {
    photoTimer.restart();
    photocellReading = analogRead(PHOTOCELL_PIN);
    Serial.print("Photocell reading: ");
    Serial.println(photocellReading);
  }
}

void setup() {
    delay(100UL); // Give things a moment to power up

    // Setup Serial Monitor
    Serial.begin(9600);

    // Setup LEDs
    FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
    FastLED.setBrightness( BRIGHTNESS );

    // Setup PIR Sensor
    pirSensor.attach(PIR_SENSOR_PIN, INPUT);
    pirSensor.interval(50); // Use a debounce interval of 50 milliseconds
    pirEnableBtn.attach(PIR_ENABLE_BTN_PIN, INPUT_PULLUP);
    
    // Setup manual on switch
    manualOnSwitch.attach(MANUAL_ON_PIN, INPUT_PULLUP);
    forceOn = manualOnSwitch.read() == MANUAL_ON;
    
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // Update PIR sensor
    if (pirEnabled && pirSensor.update()) {
      Serial.print("motion change: ");
      if (pirSensor.rose()) {
        Serial.println("detected");
        digitalWrite(LED_BUILTIN, HIGH);
      } else {
        Serial.println("lost");
        digitalWrite(LED_BUILTIN, LOW);
      }
    }


    // Update PIR enabled button
    if (pirEnableBtn.update()) {
        // Toggle PIR sensor reading on or off
        pirEnabled = pirEnableBtn.read() == LOW;
    }
    
    // Update manual on switch
    if (manualOnSwitch.update()) {
      forceOn = manualOnSwitch.read() == MANUAL_ON;
      if (forceOn) {
        Serial.println("Manual On engaged");
      } else {
        Serial.println("Manual On disengaged");
      }
    }

    switch (state) {
        case IDLE_STATE:
            tickIdle();
            break;
        case LIGHTS_TRANSITION_ON_STATE:
            tickLightsTransitionOn();
            break;
        case LIGHTS_ON_STATE:
            tickLightsOn();
            break;
        case LIGHTS_TRANSITION_OFF_STATE:
            tickLightsTransitionOff();
            break;
    }
}


///////////////////////////////////////
// State methods
///////////////////////////////////////

// state: IDLE_STATE
// Everything is off.  Awaiting instructions.
void tickIdle() {
  if (isTransitioning) {
    isTransitioning = false;
    // Log the state transition
    Serial.println("AWAITING INPUT");

    // Clear all LEDs
    for(uint8_t i=0; i<NUM_LEDS; i++) {
      leds[i] = CHSV(0, 0, 0);
    }
    updateLeds(true);
    
    updatePhotocell(true);
  }
  
  // Update ambient light reading
  updatePhotocell();
  
  // When ambient light changes between day and night, change max brightness
  if (mode == NIGHT_MODE && photocellReading > PHOTOCELL_DAY_THRESHOLD) {
    // Transition from night to day mode
    mode = DAY_MODE;
    brightness = DAY_BRIGHTNESS;
    Serial.println("It's getting light. Transitioning to day mode.");
  } else if(mode == DAY_MODE && photocellReading < PHOTOCELL_NIGHT_THRESHOLD) {
    // Transition from day to night mode
    mode = NIGHT_MODE;
    brightness = NIGHT_BRIGHTNESS;
    Serial.println("It's getting dark. Transitioning to night mode.");
  }
  
  // When motion is detected, transition to LIGHTS_TRANSITION_ON_STATE
  if (pirEnabled && pirSensor.rose()) {
    state = LIGHTS_TRANSITION_ON_STATE;
    isTransitioning = true;
    return;
  }
  
  // When manual on switch is engaged, transition to LIGHTS_TRANSITION_ON_STATE
  if (forceOn) {
    state = LIGHTS_TRANSITION_ON_STATE;
    isTransitioning = true;
    return;
  }
}


// state: LIGHTS_TRANSITION_ON_STATE
// Manually engaged or movement detected.  Animate turning the LEDs on.
void tickLightsTransitionOn() {
    if (isTransitioning) {
        isTransitioning = false;
        // Log the state transition
        Serial.println("TURNING LIGHTS ON");

      fillTimer.restart();
    }
    
    if (fillTimer.hasPassed(TOTAL_ANIM_MILLIS)) {
      isTransitioning = true;
      state = LIGHTS_ON_STATE;
      return;
    }
    
    // fade leds in for 1 second
    lightProgressive(brightness, fillTimer.elapsed(), 0, TOTAL_ANIM_MILLIS);
    updateLeds();
    // old scratchpad stuff but maybe useful someday:
    // unsigned long MILLIS_PER_LED = TOTAL_ANIM_MILLIS / NUM_LEDS;
    // uint8_t leds_fully_lit_so_far = NUM_LEDS / (TOTAL_ANIM_MILLIS / fillTimer.elapsed());
    
    
    // if (fillTimer.hasPassed(1000UL / SECONDS_TO_FILL)) {
    //     fillTimer.restart();
    //     FastLED.show();
    // }
    
    // // animation for one pixel
    // // fade from 0 to BRIGHTNESS_TARGET in a percentage of the total animation time millis.
    // // That percent is `0` for the first pixel and `total_time * (scale * (total_time/pixels))` for the last pixel.
    
}


// state: LIGHTS_ON_STATE
// LEDs on.  Awaiting input or detecting lack of movement.
void tickLightsOn() {
    if (isTransitioning) {
        isTransitioning = false;
        // Log the state transition
        Serial.println("LIGHTS ON");
        
        // Enable all LEDs
        for(uint8_t i=0; i<NUM_LEDS; i++) {
          leds[i] = CHSV(0, 0, brightness);
        }
        updateLeds(true);
    }
    
    // if no motion has been detected for a while, turn the lights off
    if (!forceOn && (!pirEnabled || pirSensor.read() == LOW)) {
        state = LIGHTS_TRANSITION_OFF_STATE;
        isTransitioning = true;
        return;
    }
}       


// state: LIGHTS_TRANSITION_OFF_STATE
// Manually disengaged or lack of movement detected.  Animate turning the LEDs off.
void tickLightsTransitionOff() {
    if (isTransitioning) {
        isTransitioning = false;
        // Log the state transition
        Serial.println("TURNING LIGHTS OFF");
    }
    
    // TODO implement
    
    state = IDLE_STATE;
    isTransitioning = true;
}

void lightProgressive(uint8_t max_brightness, unsigned long x, unsigned long min, unsigned long max ) {
  // Multiply the number of LEDs to light by 100 to so the decimal amount can be stored in an integer.
  // For example, if NUM_LEDS is 6, x is 53300, min is 0, and max is 60000,
  // then number of LEDs to light would be 5.33, and we'll store it as the integer 533.
  // This way we don't have to use any floating point math!
  unsigned long centiNumLedsToLight = 100 * NUM_LEDS * (x-min) / (max - min);
  // Dividing this by 100 gets us the number of fully lit LEDs
  uint8_t numFullyLit = centiNumLedsToLight / 100;
  // Using modulo 100 to find the remainder, then dividing that by 100,
  // gets us the percent that the next LED is partially lit.
  uint8_t partialBrightness = max_brightness * (centiNumLedsToLight % 100) / 100;

  // Light the fully bright LEDs
  for (uint8_t i=0; i<numFullyLit; i++) {
    leds[i] = CHSV( 0, 0, max_brightness);
  }
  if (numFullyLit < NUM_LEDS) {
    // Partially light the next LED
    leds[numFullyLit] = CHSV( 0, 0, partialBrightness);
    // Clear any remaining LEDs after the partially-lit LED
    for (uint8_t i = numFullyLit+1; i<NUM_LEDS; i++) {
      leds[i] = CHSV( 0, 0, 0);
    }
  }
}