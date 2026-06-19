#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// I2S Pins for MAX98357A
#define PIN_I2S_BCLK 26
#define PIN_I2S_LRC  25
#define PIN_I2S_DIN  23 

// Hardware Pins (Adjust if necessary)
#define PIN_BTN_BASS 12
#define PIN_BTN_MID  13
#define PIN_BTN_HIGH 14
#define PIN_POT      34

// Audio components
I2SStream i2s;
Equalizer3Bands eq(i2s);
BluetoothA2DPSink a2dp_sink(eq);

// State
int activeBand = 0; // 0=Bass, 1=Mid, 2=High
float currentGains[3] = {1.0, 1.0, 1.0};
int lastPotRaw = -1;
unsigned long lastPotCheckTime = 0;
bool isBluetoothConnected = false;

// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void updateDisplay() {
  display.clearDisplay();
  
  // Header: Bluetooth Status
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (isBluetoothConnected) {
    display.print("BT: CONNECTED");
  } else {
    display.print("BT: DISCONNECTED");
  }

  // Draw Separator
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Draw EQ Bands
  const char* bandNames[] = {"BASS", "MID", "HIGH"};
  
  for (int i = 0; i < 3; i++) {
    int yPos = 20 + (i * 15);
    
    // Highlight active band
    if (i == activeBand) {
      display.fillRect(0, yPos - 2, 128, 13, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    display.setCursor(2, yPos);
    display.print(bandNames[i]);
    display.print(": ");
    display.print(currentGains[i], 1);
    display.print("x");
  }
  
  display.display();
}

// Manual Debounce Button Struct
struct Button {
  uint8_t pin;
  bool state;
  bool lastState;
  unsigned long lastDebounceTime;
  unsigned long debounceDelay = 50;
  const char* name;

  void init(uint8_t p, const char* n) {
    pin = p;
    name = n;
    pinMode(pin, INPUT_PULLUP);
    state = HIGH;
    lastState = HIGH;
  }

  bool fell() {
    bool result = false;
    bool reading = digitalRead(pin);
    if (reading != lastState) {
      lastDebounceTime = millis();
    }
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != state) {
        state = reading;
        if (state == LOW) {
          result = true;
          Serial.printf("Button '%s' pressed!\n", name);
        }
      }
    }
    lastState = reading;
    return result;
  }
};

Button btnBass;
Button btnMid;
Button btnHigh;

// Bluetooth Connection Callback
void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    Serial.println("\n>>> BLUETOOTH CONNECTED! <<<\n");
    isBluetoothConnected = true;
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    Serial.println("\n>>> BLUETOOTH DISCONNECTED! <<<\n");
    isBluetoothConnected = false;
  }
  updateDisplay();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 Bluetooth Speaker with Hardware EQ...");

  // Setup Buttons
  btnBass.init(PIN_BTN_BASS, "Bass");
  btnMid.init(PIN_BTN_MID, "Mid");
  btnHigh.init(PIN_BTN_HIGH, "High");

  // Setup OLED
  // Wire.begin() defaults to SDA=21, SCL=22 on standard ESP32
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  updateDisplay();

  // Setup Potentiometer
  analogReadResolution(12); // ADC reads 0-4095

  // Setup I2S Stream
  auto cfg_i2s = i2s.defaultConfig();
  cfg_i2s.pin_bck = PIN_I2S_BCLK;
  cfg_i2s.pin_ws = PIN_I2S_LRC;
  cfg_i2s.pin_data = PIN_I2S_DIN;
  i2s.begin(cfg_i2s);

  // Setup Equalizer (do NOT call begin() here, A2DP initializes it when connected)
  // Calling begin() with 0 sample rate causes float math exceptions!
  auto& cfg_eq = eq.config();
  cfg_eq.gain_low = 1.0;
  cfg_eq.gain_medium = 1.0;
  cfg_eq.gain_high = 1.0;

  // Set Bluetooth Callback
  a2dp_sink.set_on_connection_state_changed(connection_state_changed);

  // Start Bluetooth A2DP Sink
  a2dp_sink.start("ESP32_BT_Speaker_EQ");
  Serial.println("Waiting for Bluetooth connection...");
}

void loop() {
  bool bandChanged = false;

  // Check if all 3 buttons are pressed simultaneously to reset
  if (digitalRead(PIN_BTN_BASS) == LOW && 
      digitalRead(PIN_BTN_MID) == LOW && 
      digitalRead(PIN_BTN_HIGH) == LOW) {
    
    currentGains[0] = 1.0;
    currentGains[1] = 1.0;
    currentGains[2] = 1.0;
    
    auto& eq_cfg = eq.config();
    eq_cfg.gain_low = 1.0;
    eq_cfg.gain_medium = 1.0;
    eq_cfg.gain_high = 1.0;
    
    Serial.println("\n*** ALL BANDS RESET TO 1.0x ***\n");
    updateDisplay();
    
    // Prevent immediate re-triggering and let user release buttons
    delay(500); 
    
    // Reset our button states so they don't trigger fell() upon release
    btnBass.state = digitalRead(PIN_BTN_BASS);
    btnMid.state = digitalRead(PIN_BTN_MID);
    btnHigh.state = digitalRead(PIN_BTN_HIGH);
    lastPotRaw = analogRead(PIN_POT); // reset pot tracking
  }

  if (btnBass.fell()) {
    activeBand = 0;
    bandChanged = true;
    Serial.println("-> Active Band Switched to: BASS");
  } else if (btnMid.fell()) {
    activeBand = 1;
    bandChanged = true;
    Serial.println("-> Active Band Switched to: MID");
  } else if (btnHigh.fell()) {
    activeBand = 2;
    bandChanged = true;
    Serial.println("-> Active Band Switched to: HIGH");
  }

  if (bandChanged) {
    updateDisplay();
  }

  // Read Potentiometer every 1 second
  if (millis() - lastPotCheckTime >= 1000) {
    lastPotCheckTime = millis();
    int currentPotRaw = analogRead(PIN_POT);
    
    if (lastPotRaw == -1) {
      lastPotRaw = currentPotRaw;
    }

    // Check if potentiometer moved significantly
    if (abs(currentPotRaw - lastPotRaw) > 50) {
      lastPotRaw = currentPotRaw;
      
      // Map 0-4095 to 1.0 - 3.0 (1x to 3x gain)
      float mappedGain = 1.0 + (((float)currentPotRaw / 4095.0) * 2.0);
      
      if (currentGains[activeBand] != mappedGain) {
        currentGains[activeBand] = mappedGain;
        
        auto& eq_cfg = eq.config();
        eq_cfg.gain_low = currentGains[0];
        eq_cfg.gain_medium = currentGains[1];
        eq_cfg.gain_high = currentGains[2];
        
        Serial.printf("EQ Updated -> Bass: %.2f, Mid: %.2f, High: %.2f\n", 
                      currentGains[0], currentGains[1], currentGains[2]);
        updateDisplay();
      }
    }
  }

  if (bandChanged) {
    lastPotRaw = analogRead(PIN_POT);
  }

  // Feed the watchdog and prevent 100% CPU usage
  delay(100);
}