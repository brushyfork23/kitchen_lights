/**
 * Kitchen under-cabinet motion sensing lights.
 * This controller drives two strips of SK6812 RGBW LEDs.
 * When motion is detected from the PIR sensor, the lights turn on.
 * A photocell detects when the ambient room light has dimmed and
 * dims the LEDs accordingly.
 */

////////////////////////////////////////////
// Hardware
////////////////////////////////////////////
// Upload to HiLetgo ESP-32.
// Manufacturer website: http://www.hiletgo.com/ProductDetail/1906566.html
// Purchase link: https://www.amazon.com/HiLetgo-ESP-WROOM-32-Development-Microcontroller-Integrated/dp/B0718T232Z


/////////////////////////////////////////////
// Required Libraries
/////////////////////////////////////////////
// Bounce2 - https://github.com/thomasfredericks/Bounce2
// Chrono - https://github.com/SofaPirate/Chrono
// NeoPixelBus - https://github.com/Makuna/NeoPixelBus
// WiFiManager - https://github.com/tzapu/WiFiManager/tree/development as of 7/12/2020 (non-blocking feature not yet in master)


//////////////////////////////////////////////
// Config
//////////////////////////////////////////////
// WiFi
#define WIFI_RESET_BTN_PIN 27
#define WIFI_SSID "Under-Cabinet Lights"
#define WIFI_PASSWORD "wifipassword"
// DNS
#define HOSTNAME "esp32" // hostname is http://esp32.local
// OTA reprogramming
#define OTA_PASSWORD "otapassword"
// LEDs
#define LEDS_LEFT_PIN 33
#define LEDS_RIGHT_PIN 25
#define NUM_LEDS_LEFT 10
#define NUM_LEDS_RIGHT 93
#define NUM_LEDS (NUM_LEDS_LEFT + NUM_LEDS_RIGHT)
#define DAY_BRIGHTNESS 255    // 0 to 255
#define NIGHT_BRIGHTNESS 80  // 0 to 255
#define LED_FPS     60
// Animations
#define FADE_IN_MILLIS 150
#define FADE_OUT_MILLIS 350
// Manual ON switch
#define MANUAL_ON_PIN 26
// PIR
#define PIR_SENSOR_PIN  12
#define PIR_ENABLE_BTN_PIN 14
#define MOTION_ACTIVATION_SECONDS 10    // Length of time to keep LEDs on after motion is detected
// Photocell
#define PHOTOCELL_PIN 32
#define PHOTOCELL_DAY_THRESHOLD 350     // Brightness at which to switch to Day Mode (0 to 4095)
#define PHOTOCELL_NIGHT_THRESHOLD 200   // Brightness at which to switch to Night Mode (0 to 4095)
// Misc button
#define MISC_BTN_PIN 13


///////////////////////////////////////////////
// Definitions and Initialization
///////////////////////////////////////////////
// State machine
enum states {
    IDLE_STATE,                     // Watching for changes in ambient light and awaiting button presses or motion detection.
    LIGHTS_TRANSITION_ON_STATE,     // Manually engaged or movement detected.  Animate turning the LEDs on.
    LIGHTS_ON_STATE,                // LEDs on.  Awaiting input or detecting lack of movement.
    LIGHTS_TRANSITION_OFF_STATE,    // Manually disengaged or lack of movement detected.  Animate turning the LEDs off.
};
enum states state = IDLE_STATE;
bool isTransitioning = true;

// Timer
#include <Chrono.h>
#include <LightChrono.h>

// WiFi reset button
#include <Bounce2.h>
Bounce wifiResetBtn = Bounce();

// WiFi
#include <WiFiManager.h>
WiFiManager wifiManager;

// DNS
#include <ESPmDNS.h>

// OTA Reprogramming
#include <ArduinoOTA.h>

// Manual ON switch
Bounce manualOnSwitch = Bounce();
bool forceOn = false;

// PIR sensor
Bounce pirSensor = Bounce();
Bounce pirEnableBtn = Bounce();
bool pirEnabled = false;
Chrono motionActivationTimer = Chrono(Chrono::SECONDS);

// Misc button
Bounce miscBtn = Bounce();

// LEDs
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
NeoPixelBus<NeoRgbwFeature, NeoEsp32Rmt2800KbpsMethod> leftLeds(NUM_LEDS_LEFT, LEDS_LEFT_PIN);
NeoPixelBus<NeoRgbwFeature, NeoEsp32Rmt3800KbpsMethod> rightLeds(NUM_LEDS_RIGHT, LEDS_RIGHT_PIN);
NeoGamma<NeoGammaTableMethod> colorGamma; // for human eye color correction
LightChrono nextFrameTimer;
uint16_t brightness = DAY_BRIGHTNESS;
RgbwColor red = colorGamma.Correct(RgbwColor(brightness,0,0,0));
RgbwColor green = colorGamma.Correct(RgbwColor(0,brightness,0,0));
RgbwColor blue = colorGamma.Correct(RgbwColor(0,0,brightness,0));
RgbwColor white = colorGamma.Correct(RgbwColor(0,0,0,brightness));
RgbwColor BLACK(0);

// Day/Night mode
enum modes {
  DAY_MODE,
  NIGHT_MODE,
};
enum modes mode = DAY_MODE;

/////////////////////////////////////
// Utility Methods
/////////////////////////////////////
// Read photocell, and adjust LED brightness mode
// when ambient light changes between day and night
void updateDayNightMode() {
    uint16_t photocellReading = analogRead(PHOTOCELL_PIN);
    Serial.print("Photocell reading: ");
    Serial.println(photocellReading);
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
}

void drawLeds() {
    leftLeds.Show();
    rightLeds.Show();
}

void fillLeds(RgbwColor color) {
    leftLeds.ClearTo(color);
    rightLeds.ClearTo(color);
}

RgbwColor GetPixelColor(uint16_t index) {
 if (index < NUM_LEDS_LEFT) {
     return leftLeds.GetPixelColor(NUM_LEDS_LEFT-1-index);
 } else {
   return rightLeds.GetPixelColor(index - NUM_LEDS_LEFT);
 }
}

void SetPixelColor(uint16_t index, RgbwColor color) {
  if (index < NUM_LEDS_LEFT) {
    leftLeds.SetPixelColor(NUM_LEDS_LEFT-1-index, color);
  } else {
    rightLeds.SetPixelColor(index - NUM_LEDS_LEFT, color);
  }
}

////////////////////////////////
// Animations
////////////////////////////////
NeoPixelAnimator animations(2); // Total number of animations

// Fade in by filling from one side to the other
void FadeInAcrossAnimUpdate(const AnimationParam& param) {
  // start the fill slow, end quick
  float progress = NeoEase::QuadraticIn(param.progress);
  // fade from partial brightness to full brightness.
  RgbwColor color = RgbwColor::LinearBlend(colorGamma.Correct(RgbwColor(0,0,0,brightness/3)), colorGamma.Correct(RgbwColor(0,0,0,brightness)), progress);
  for(uint16_t i=0; i< NUM_LEDS*progress; i++) {
    SetPixelColor(i, color);
  }
}

// fade out all evenly
void FadeOutAnimUpdate(const AnimationParam& param) {
  // start the fade quickly, end slowly
  float progress = NeoEase::QuinticOut(param.progress);
  // fade from partial brightness to full brightness.
  RgbwColor color = RgbwColor::LinearBlend(colorGamma.Correct(RgbwColor(0,0,0,brightness)), colorGamma.Correct(RgbwColor(0,0,0,0)), progress);
  fillLeds(color);
}

////////////////////////////////////////
// Setup
////////////////////////////////////////
void setup()
{
    delay(100);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);
    while (!Serial); // wait for serial attach

    Serial.println();
    Serial.println("Initializing...");
    Serial.flush();

  
    // Init WiFi
    // Automatically connect using saved credentials if they exist.
    // If connection fails it starts an access point with the specified name.
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    wifiManager.setConfigPortalBlocking(false);
    if (wifiManager.autoConnect(WIFI_SSID, WIFI_PASSWORD)) {
      Serial.println("Wifi connected.");
    } else {
      Serial.println("Wifi failed to connect; launching access point.");
      Serial.print("ssid: ");
      Serial.println(WIFI_SSID);
    }

    // Init DNS
    if(!MDNS.begin(HOSTNAME)) {
        Serial.println("Error starting mDNS");
    }

    // Init OTA Reprogramming
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else { // U_SPIFFS
          type = "filesystem";
        }
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
          Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
          Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
          Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
          Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
          Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();
    
    // Init LEDs
    leftLeds.Begin();
    rightLeds.Begin();
    fillLeds(BLACK);
    drawLeds();

    // Init WiFi reset button
    wifiResetBtn.attach(WIFI_RESET_BTN_PIN, INPUT_PULLUP);

    // Init Manual ON switch
    manualOnSwitch.attach(MANUAL_ON_PIN, INPUT_PULLUP);
    forceOn = manualOnSwitch.read() == LOW;

    // Init PIR Sensor
    pirSensor.attach(PIR_SENSOR_PIN, INPUT);
    pirEnableBtn.attach(PIR_ENABLE_BTN_PIN, INPUT_PULLUP);
    pirEnableBtn.update();
    pirEnabled = pirEnableBtn.read() == LOW;
    pirSensor.update(); // flush first reading so motion change is not immediately detected in loop

    // Init Misc button
    miscBtn.attach(MISC_BTN_PIN, INPUT_PULLUP);

    // Init Photocell
    pinMode(PHOTOCELL_PIN, INPUT);

    Serial.println();
    Serial.println("Running...");
    digitalWrite(LED_BUILTIN, LOW);
}


/////////////////////////////////////
// Loop
/////////////////////////////////////
void loop()
{
  // Handle WiFi connections
  wifiManager.process();

  // Handle OTA reprogramming
  ArduinoOTA.handle();

  // Handle WiFi reset button
  wifiResetBtn.update();
  if (wifiResetBtn.fell()) {
    wifiManager.resetSettings();
    wifiManager.reboot();
  }

  // Handle PIR enabled button
  if (pirEnableBtn.update()) {
      // Enable or disable PIR sensor reading
    pirEnabled = pirEnableBtn.fell();
  }

  // Update PIR sensor
  if (pirSensor.update()) {
    Serial.print("motion change: ");
    if (pirSensor.read() == HIGH) {
      motionActivationTimer.restart();
      Serial.println("detected");
    } else {
      Serial.println("lost");
    }
  }

  // Update manual on switch
  if (manualOnSwitch.update()) {
    forceOn = manualOnSwitch.fell();
    if (forceOn) {
      Serial.println("Manual On engaged");
    } else {
      Serial.println("Manual On disengaged");
    }
  }

  // Update the misc button
  if (miscBtn.update()) {
    if (miscBtn.fell()) {
      Serial.println("Misc button pressed");
    } else {
      Serial.println("Misc button released");
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
// Watching for changes in ambient light and awaiting button presses or motion detection.
void tickIdle() {
  if (isTransitioning) {
    isTransitioning = false;
    // Log the state transition
    Serial.println("AWAITING INPUT");

    fillLeds(BLACK);
    drawLeds();
  }
  
  // When motion is detected, transition to LIGHTS_TRANSITION_ON_STATE
  if (pirEnabled && pirSensor.read() == HIGH) {
    updateDayNightMode();
    state = LIGHTS_TRANSITION_ON_STATE;
    isTransitioning = true;
    return;
  }
  
  // When manual on switch is engaged, transition to LIGHTS_TRANSITION_ON_STATE
  if (forceOn) {
    updateDayNightMode();
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

        // Begin animation to fade all LEDs in
        animations.StartAnimation(0, FADE_IN_MILLIS, FadeInAcrossAnimUpdate);
        nextFrameTimer.restart();
    }
    
    if (!animations.IsAnimating()) {
      isTransitioning = true;
      state = LIGHTS_ON_STATE;
      return;
    }
    
    if (nextFrameTimer.hasPassed(1000UL / LED_FPS, true)) {
      animations.UpdateAnimations();
      drawLeds();
    }
}


// state: LIGHTS_ON_STATE
// LEDs on.  Awaiting input or detecting lack of movement.
void tickLightsOn() {
    if (isTransitioning) {
        isTransitioning = false;
        // Log the state transition
        Serial.println("LIGHTS ON");
        
        // Enable all LEDs
        fillLeds(RgbwColor(brightness));
        drawLeds();
    }
    
    // if no motion has been detected for a while, turn the lights off
    bool motionLost = pirSensor.read() == LOW && motionActivationTimer.hasPassed(MOTION_ACTIVATION_SECONDS);
    if (!forceOn && (!pirEnabled || motionLost)) {
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

        // Begin animation to fade all LEDs out
        animations.StartAnimation(1, FADE_OUT_MILLIS, FadeOutAnimUpdate);
        nextFrameTimer.restart();
    }
    
    if (!animations.IsAnimating()) {
      isTransitioning = true;
      state = IDLE_STATE;
      return;
    }
    
    if (nextFrameTimer.hasPassed(1000UL / LED_FPS, true)) {
      animations.UpdateAnimations();
      drawLeds();
    }
}
