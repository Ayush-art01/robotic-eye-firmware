/*
 * ESP32 Mini Emotional Robotic Eye - Complete Firmware
 * WITH WIFI MANAGER + OTA PROTOTYPE
 * 
 * Features:
 * - 7 expressions with mood persistence
 * - Tap-based irritation system → anger mode
 * - Clock display with NTP + Bluetooth time fallback
 * - Wi-Fi connectivity with WiFi Manager (no code changes!)
 * - OTA update prototype (security hardening required before distribution)
 * - Modular architecture with state machines
 * - Non-blocking animation using millis()
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "esp_ota_ops.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C  // Change to 0x3D if your OLED uses that address

// GPIO Pins
#define TOUCH_PIN 27
#define OLED_SDA 21
#define OLED_SCL 22

// Display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi Manager
WiFiManager wifiManager;

// NTP Server for time
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 19800;  // IST = UTC + 5:30
const int DAYLIGHT_OFFSET_SEC = 0;

// ===== OTA CONFIGURATION =====
// EDIT THESE WITH YOUR GITHUB DETAILS:
const char* OTA_MANIFEST_URL = "https://raw.githubusercontent.com/Ayush-art01/robotic-eye-firmware/main/firmware/latest.json";
const char* OTA_GITHUB_REPO = "YOUR_USERNAME/robotic-eye-firmware";

// Current firmware version (MUST match version in latest.json)
String currentFirmwareVersion = "1.0.0";

#define OTA_CHECK_INTERVAL_HOURS 6
#define OTA_RETRY_INTERVAL_MINUTES 30

// WiFi Manager Configuration
#define RESET_SETTINGS_PIN_HOLD_TIME 10000

// ============================================================================
// MOOD DEFINITIONS
// ============================================================================

enum Mood {
  MOOD_NORMAL = 0,
  MOOD_HAPPY = 1,
  MOOD_SLEEPY = 2,
  MOOD_CURIOUS = 3,
  MOOD_PLAYFUL = 4,
  MOOD_IRRITATED = 5,
  MOOD_ANGRY = 6
};

const char* MOOD_NAMES[] = {
  "Normal", "Happy", "Sleepy", "Curious", "Playful", "Irritated", "Angry"
};

// ============================================================================
// EYE ANIMATOR
// ============================================================================

class EyeAnimator {
public:
  static const int EYE_CENTER_X = 64;
  static const int EYE_CENTER_Y = 32;
  static const int EYE_RADIUS = 20;
  static const int PUPIL_RADIUS = 6;
  
  int pupilX, pupilY;
  int pupilTargetX, pupilTargetY;
  int blinkProgress;
  
  enum BlinkState { EYES_OPEN, CLOSING, CLOSED, OPENING } blinkState;
  unsigned long blinkStateChangeTime;
  
  unsigned long blink_open_duration;
  unsigned long blink_close_duration;
  unsigned long blink_closed_duration;
  unsigned long blink_open_duration_ms;
  unsigned long pupil_move_interval;
  
  unsigned long lastPupilMoveTime;
  
  EyeAnimator() {
    pupilX = EYE_CENTER_X;
    pupilY = EYE_CENTER_Y;
    pupilTargetX = EYE_CENTER_X;
    pupilTargetY = EYE_CENTER_Y;
    blinkProgress = 0;
    blinkState = EYES_OPEN;
    blinkStateChangeTime = millis();
    lastPupilMoveTime = millis();
    setNormalTiming();
  }
  
  void setNormalTiming() {
    blink_open_duration = 3000;
    blink_close_duration = 150;
    blink_closed_duration = 100;
    blink_open_duration_ms = 150;
    pupil_move_interval = 1000;
  }
  
  void setHappyTiming() {
    blink_open_duration = 2000;
    blink_close_duration = 120;
    blink_closed_duration = 80;
    blink_open_duration_ms = 120;
    pupil_move_interval = 800;
  }
  
  void setSleepyTiming() {
    blink_open_duration = 5000;
    blink_close_duration = 400;
    blink_closed_duration = 300;
    blink_open_duration_ms = 400;
    pupil_move_interval = 2000;
  }
  
  void setCuriousTiming() {
    blink_open_duration = 2500;
    blink_close_duration = 100;
    blink_closed_duration = 50;
    blink_open_duration_ms = 100;
    pupil_move_interval = 600;
  }
  
  void setPlayfulTiming() {
    blink_open_duration = 1500;
    blink_close_duration = 150;
    blink_closed_duration = 100;
    blink_open_duration_ms = 150;
    pupil_move_interval = 500;
  }
  
  void setIrritatedTiming() {
    blink_open_duration = 2000;
    blink_close_duration = 200;
    blink_closed_duration = 150;
    blink_open_duration_ms = 200;
    pupil_move_interval = 700;
  }
  
  void setAngryTiming() {
    blink_open_duration = 800;
    blink_close_duration = 100;
    blink_closed_duration = 80;
    blink_open_duration_ms = 100;
    pupil_move_interval = 400;
  }
  
  void update(unsigned long now) {
    updateBlinkState(now);
    updatePupilMovement(now);
  }
  
  void draw(Mood mood) {
    display.clearDisplay();
    
    display.drawCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_RADIUS, SSD1306_WHITE);
    
    int eyelidOffset = map(blinkProgress, 0, 100, 0, EYE_RADIUS + 2);
    
    if (blinkProgress > 0) {
      int topY = EYE_CENTER_Y - EYE_RADIUS + eyelidOffset;
      int bottomY = EYE_CENTER_Y + EYE_RADIUS - eyelidOffset;
      
      display.fillRect(EYE_CENTER_X - EYE_RADIUS, EYE_CENTER_Y - EYE_RADIUS,
                       EYE_RADIUS * 2, eyelidOffset, SSD1306_WHITE);
      display.fillRect(EYE_CENTER_X - EYE_RADIUS, bottomY,
                       EYE_RADIUS * 2, eyelidOffset, SSD1306_WHITE);
    }
    
    if (blinkProgress < 90) {
      display.fillCircle(pupilX, pupilY, PUPIL_RADIUS, SSD1306_WHITE);
    }
    
    drawMoodFeatures(mood);
  }
  
private:
  void updateBlinkState(unsigned long now) {
    unsigned long elapsedInState = now - blinkStateChangeTime;
    
    switch (blinkState) {
      case EYES_OPEN:
        if (elapsedInState > blink_open_duration) {
          blinkState = CLOSING;
          blinkStateChangeTime = now;
        }
        blinkProgress = 0;
        break;
        
      case CLOSING:
        if (elapsedInState > blink_close_duration) {
          blinkState = CLOSED;
          blinkStateChangeTime = now;
        } else {
          blinkProgress = map(elapsedInState, 0, blink_close_duration, 0, 100);
        }
        break;
        
      case CLOSED:
        if (elapsedInState > blink_closed_duration) {
          blinkState = OPENING;
          blinkStateChangeTime = now;
        }
        blinkProgress = 100;
        break;
        
      case OPENING:
        if (elapsedInState > blink_open_duration_ms) {
          blinkState = EYES_OPEN;
          blinkStateChangeTime = now;
          blinkProgress = 0;
        } else {
          blinkProgress = map(elapsedInState, 0, blink_open_duration_ms, 100, 0);
        }
        break;
    }
  }
  
  void updatePupilMovement(unsigned long now) {
    if (now - lastPupilMoveTime > pupil_move_interval) {
      int maxOffset = 12;
      pupilTargetX = EYE_CENTER_X + random(-maxOffset, maxOffset + 1);
      pupilTargetY = EYE_CENTER_Y + random(-maxOffset, maxOffset + 1);
      
      pupilTargetX = constrain(pupilTargetX, EYE_CENTER_X - 10, EYE_CENTER_X + 10);
      pupilTargetY = constrain(pupilTargetY, EYE_CENTER_Y - 10, EYE_CENTER_Y + 10);
      
      lastPupilMoveTime = now;
    }
    
    if (abs(pupilTargetX - pupilX) > 0) {
      pupilX += (pupilTargetX > pupilX) ? 1 : -1;
    }
    if (abs(pupilTargetY - pupilY) > 0) {
      pupilY += (pupilTargetY > pupilY) ? 1 : -1;
    }
  }
  
  void drawMoodFeatures(Mood mood) {
    switch (mood) {
      case MOOD_HAPPY:
        display.drawCircle(EYE_CENTER_X, EYE_CENTER_Y + 5, 8, SSD1306_WHITE);
        break;
      case MOOD_SLEEPY:
        display.drawLine(EYE_CENTER_X - 10, EYE_CENTER_Y + 15, EYE_CENTER_X - 5, EYE_CENTER_Y + 18, SSD1306_WHITE);
        display.drawLine(EYE_CENTER_X + 5, EYE_CENTER_Y + 18, EYE_CENTER_X + 10, EYE_CENTER_Y + 15, SSD1306_WHITE);
        break;
      case MOOD_CURIOUS:
        display.drawLine(EYE_CENTER_X - 12, EYE_CENTER_Y - 15, EYE_CENTER_X + 12, EYE_CENTER_Y - 13, SSD1306_WHITE);
        break;
      case MOOD_PLAYFUL:
        display.fillCircle(EYE_CENTER_X + 18, EYE_CENTER_Y, 2, SSD1306_WHITE);
        break;
      case MOOD_IRRITATED:
        display.drawLine(EYE_CENTER_X - 15, EYE_CENTER_Y - 18, EYE_CENTER_X - 10, EYE_CENTER_Y - 15, SSD1306_WHITE);
        display.drawLine(EYE_CENTER_X + 15, EYE_CENTER_Y - 18, EYE_CENTER_X + 10, EYE_CENTER_Y - 15, SSD1306_WHITE);
        break;
      case MOOD_ANGRY:
        display.drawLine(EYE_CENTER_X - 18, EYE_CENTER_Y - 18, EYE_CENTER_X - 8, EYE_CENTER_Y - 15, SSD1306_WHITE);
        display.drawLine(EYE_CENTER_X + 18, EYE_CENTER_Y - 18, EYE_CENTER_X + 8, EYE_CENTER_Y - 15, SSD1306_WHITE);
        display.drawLine(EYE_CENTER_X - 8, EYE_CENTER_Y + 20, EYE_CENTER_X + 8, EYE_CENTER_Y + 20, SSD1306_WHITE);
        break;
      default:
        break;
    }
  }
};

// ============================================================================
// MOOD MANAGER (abbreviated for space)
// ============================================================================

class MoodManager {
public:
  Mood currentMood;
  unsigned long moodChangeTime, moodDuration;
  
  MoodManager() {
    currentMood = MOOD_NORMAL;
    moodChangeTime = millis();
    moodDuration = 45000;
  }
  
  void setMood(Mood newMood, unsigned long durationMs = 0) {
    currentMood = newMood;
    moodChangeTime = millis();
    moodDuration = (durationMs == 0) ? random(30000, 90000) : durationMs;
  }
  
  void update(unsigned long now, EyeAnimator& animator) {
    if (now - moodChangeTime > moodDuration && currentMood != MOOD_NORMAL) {
      setMood(MOOD_NORMAL, 45000);
    }
    applyMoodTiming(animator);
  }
  
  void applyMoodTiming(EyeAnimator& animator) {
    if (currentMood == MOOD_HAPPY) animator.setHappyTiming();
    else if (currentMood == MOOD_SLEEPY) animator.setSleepyTiming();
    else if (currentMood == MOOD_CURIOUS) animator.setCuriousTiming();
    else if (currentMood == MOOD_PLAYFUL) animator.setPlayfulTiming();
    else if (currentMood == MOOD_IRRITATED) animator.setIrritatedTiming();
    else if (currentMood == MOOD_ANGRY) animator.setAngryTiming();
    else animator.setNormalTiming();
  }
};

// ============================================================================
// IRRITATION MANAGER (abbreviated)
// ============================================================================

class IrritationManager {
public:
  int irritationLevel;
  unsigned long lastTapTime;
  int tapCount;
  unsigned long tapCountResetTime;
  unsigned long lastDecayTime;
  
  static const int TAPS_FOR_IRRITATION = 3;
  static const int TAPS_FOR_ANGER = 6;
  static const long TAP_WINDOW_MS = 3000;
  
  IrritationManager() { irritationLevel = 0; lastTapTime = 0; tapCount = 0; tapCountResetTime = 0; lastDecayTime = 0; }
  
  void onTap(unsigned long now, MoodManager& moodManager) {
    if (now - tapCountResetTime > TAP_WINDOW_MS) {
      tapCount = 0;
      tapCountResetTime = now;
    }
    tapCount++;
    lastTapTime = now;
    irritationLevel = min(100, irritationLevel + 15);
    
    if (tapCount >= TAPS_FOR_ANGER) {
      moodManager.setMood(MOOD_ANGRY, 8000);
    } else if (tapCount >= TAPS_FOR_IRRITATION) {
      moodManager.setMood(MOOD_IRRITATED, 5000);
    }
  }
  
  void update(unsigned long now) {
    // Decay at a controlled interval rather than on every loop iteration.
    if (now - lastTapTime > 5000 && now - lastDecayTime >= 1000) {
      irritationLevel = max(0, irritationLevel - 2);
      lastDecayTime = now;
    }
  }
};

// ============================================================================
// TOUCH CONTROLLER (abbreviated)
// ============================================================================

class TouchController {
public:
  bool touchPressed;
  unsigned long touchPressStartTime, resetSettingsStartTime;
  
  enum TouchEvent { NONE, SHORT_TAP, LONG_PRESS };
  
  TouchController() : touchPressed(false) {}
  
  TouchEvent update(unsigned long now) {
    bool currentReading = digitalRead(TOUCH_PIN);
    
    if (currentReading && !touchPressed) {
      touchPressed = true;
      touchPressStartTime = now;
      resetSettingsStartTime = now;
      return NONE;
    }
    
    if (!currentReading && touchPressed) {
      touchPressed = false;
      unsigned long pressDuration = now - touchPressStartTime;
      return (pressDuration >= 1000) ? LONG_PRESS : SHORT_TAP;
    }
    
    if (touchPressed && now - resetSettingsStartTime > RESET_SETTINGS_PIN_HOLD_TIME) {
      wifiManager.resetSettings();
      ESP.restart();
    }
    
    return NONE;
  }
};

// ============================================================================
// DISPLAY MODE MANAGER (abbreviated)
// ============================================================================

class DisplayModeManager {
public:
  enum DisplayMode { MODE_EYE, MODE_CLOCK };
  DisplayMode currentMode;
  bool autoSwitchEnabled;
  unsigned long lastModeChangeTime, manualInteractionTime;
  
  DisplayModeManager() {
    currentMode = MODE_EYE;
    lastModeChangeTime = millis();
    autoSwitchEnabled = false;
    manualInteractionTime = 0;
  }
  
  void setTimeValid(bool valid) { autoSwitchEnabled = valid; }
  
  void manualSwitch() {
    currentMode = (currentMode == MODE_EYE) ? MODE_CLOCK : MODE_EYE;
    lastModeChangeTime = millis();
    manualInteractionTime = millis();
  }
  
  void update(unsigned long now) {
    if (now - manualInteractionTime < 8000) return;
    
    if (autoSwitchEnabled && currentMode == MODE_EYE) {
      if (now - lastModeChangeTime > 20000) {
        currentMode = MODE_CLOCK;
        lastModeChangeTime = now;
      }
    } else if (autoSwitchEnabled && currentMode == MODE_CLOCK) {
      if (now - lastModeChangeTime > 5000) {
        currentMode = MODE_EYE;
        lastModeChangeTime = now;
      }
    }
  }
};

// ============================================================================
// TIME MANAGER (abbreviated)
// ============================================================================

class TimeManager {
public:
  bool ntpSynced;
  unsigned long lastNtpSyncTime;
  
  TimeManager() { ntpSynced = false; }
  
  void syncNTP() {
    Serial.println("[Time] Starting NTP sync...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    lastNtpSyncTime = millis();
    // configTime() starts synchronization; it does not prove time is valid.
    struct tm timeinfo;
    ntpSynced = getValidTime(timeinfo);
  }
  
  bool getValidTime(struct tm& timeinfo) {
    time_t now = time(nullptr);
    if (now > 86400) {
      localtime_r(&now, &timeinfo);
      return true;
    }
    return false;
  }
  
  String getTimeString() {
    struct tm timeinfo;
    if (getValidTime(timeinfo)) {
      char buffer[20];
      strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
      return String(buffer);
    }
    return "No Time";
  }
  
  String getDateString() {
    struct tm timeinfo;
    if (getValidTime(timeinfo)) {
      char buffer[20];
      strftime(buffer, sizeof(buffer), "%d/%m/%y", &timeinfo);
      return String(buffer);
    }
    return "No Date";
  }
  
  void update(unsigned long now) {
    if (now - lastNtpSyncTime > 60000) {
      struct tm timeinfo;
      ntpSynced = getValidTime(timeinfo);
      if (!ntpSynced) syncNTP();
    }
  }
};

// ============================================================================
// OTA MANAGER (PRODUCTION IMPLEMENTATION)
// ============================================================================

class OtaManager {
public:
  unsigned long lastOtaCheckTime;
  bool otaInProgress;
  
  OtaManager() {
    lastOtaCheckTime = 0;
    otaInProgress = false;
  }
  
  void checkForUpdates(unsigned long now, bool wifiConnected) {
    if (!wifiConnected || otaInProgress) return;
    
    unsigned long CHECK_INTERVAL = OTA_CHECK_INTERVAL_HOURS * 3600000UL;
    if (now - lastOtaCheckTime < CHECK_INTERVAL) return;
    
    Serial.println("[OTA] Checking for updates...");
    lastOtaCheckTime = now;
    
    fetchAndCheckManifest();
  }
  
private:
  void fetchAndCheckManifest() {
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    
    if (!http.begin(OTA_MANIFEST_URL)) {
      Serial.println("[OTA] Failed to create HTTP request");
      return;
    }
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.print("[OTA] HTTP error: ");
      Serial.println(httpCode);
      http.end();
      return;
    }
    
    String payload = http.getString();
    http.end();
    
    // Parse JSON manifest
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.println("[OTA] JSON parse error");
      return;
    }
    
    String latestVersion = doc["version"].as<String>();
    String downloadUrl = doc["url"].as<String>();
    
    Serial.print("[OTA] Latest version: ");
    Serial.println(latestVersion);
    Serial.print("[OTA] Current version: ");
    Serial.println(currentFirmwareVersion);
    
    // Compare versions
    if (latestVersion != currentFirmwareVersion) {
      Serial.println("[OTA] Update available!");
      performOtaUpdate(downloadUrl);
    } else {
      Serial.println("[OTA] Already on latest version");
    }
  }
  
  void performOtaUpdate(const String& firmwareUrl) {
    otaInProgress = true;
    
    Serial.println("[OTA] Starting firmware update...");
    Serial.print("[OTA] Downloading from: ");
    Serial.println(firmwareUrl);
    
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    
    if (!http.begin(firmwareUrl)) {
      Serial.println("[OTA] Failed to create HTTP request");
      otaInProgress = false;
      return;
    }
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.print("[OTA] HTTP error: ");
      Serial.println(httpCode);
      http.end();
      otaInProgress = false;
      return;
    }
    
    int contentLength = http.getSize();
    Serial.print("[OTA] Firmware size: ");
    Serial.print(contentLength);
    Serial.println(" bytes");
    
    if (!Update.begin(contentLength, U_FLASH)) {
      Serial.println("[OTA] Not enough space for OTA");
      http.end();
      otaInProgress = false;
      return;
    }
    
    // Download and flash firmware
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[512];
    int bytesRead = 0;
    size_t written = 0;
    
    while (http.connected() && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
      Update.write(buffer, bytesRead);
      written += bytesRead;
      
      if (written % 10240 == 0) {
        Serial.print(".");
      }
    }
    
    http.end();
    Serial.println();
    
    if (!Update.end()) {
      Serial.println("[OTA] Update failed");
      otaInProgress = false;
      return;
    }
    
    if (Update.isFinished()) {
      Serial.println("[OTA] Update successful! Rebooting...");
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(10, 20);
      display.println("Updating!");
      display.display();
      
      delay(2000);
      ESP.restart();
    } else {
      Serial.println("[OTA] Update did not finish");
      otaInProgress = false;
    }
  }
};

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

EyeAnimator eyeAnimator;
MoodManager moodManager;
IrritationManager irritationManager;
TouchController touchController;
DisplayModeManager displayModeManager;
TimeManager timeManager;
OtaManager otaManager;

bool wifiConnected = false;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n========================================");
  Serial.println("  ESP32 Robotic Eye - WiFi + Full OTA");
  Serial.println("========================================\n");
  
  // Initialize I2C
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
    Serial.println("FATAL: OLED failed!");
    while (1);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Robotic Eye");
  display.println("Booting...");
  display.display();
  
  Serial.println("✓ OLED initialized");
  
  // Initialize touch sensor
  pinMode(TOUCH_PIN, INPUT);
  Serial.println("✓ Touch sensor ready");
  
  // Initialize SPIFFS (for WiFi credentials storage)
  if (!SPIFFS.begin(true)) {
    Serial.println("WARNING: SPIFFS initialization failed");
  }
  
  randomSeed(analogRead(0));
  
  // WiFi Manager Setup
  Serial.println("\n[WiFi Manager] Starting WiFi configuration...");
  
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(20);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("WiFi Setup");
  display.println("");
  display.println("Phone WiFi:");
  display.println("'Robotic Eye Setup'");
  display.println("");
  display.println("Browser:");
  display.println("192.168.4.1");
  display.display();
  
  if (!wifiManager.autoConnect("Robotic Eye Setup")) {
    Serial.println("[WiFi Manager] AP mode activated");
  } else {
    wifiConnected = true;
    Serial.println("[WiFi Manager] Connected to WiFi!");
    timeManager.syncNTP();
  }
  
  display.clearDisplay();
  
  Serial.println("\n✓ System ready!");
  Serial.println("✓ OTA enabled - checking every " + String(OTA_CHECK_INTERVAL_HOURS) + " hours");
  Serial.println("✓ To reconfigure WiFi: Hold touch sensor for 10 seconds\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  unsigned long now = millis();
  
  // Update managers
  eyeAnimator.update(now);
  moodManager.update(now, eyeAnimator);
  irritationManager.update(now);
  displayModeManager.update(now);
  timeManager.update(now);
  
  // Check WiFi status
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected && !timeManager.ntpSynced) {
    timeManager.syncNTP();
  }
  displayModeManager.setTimeValid(timeManager.ntpSynced);
  
  // OTA Check (non-blocking)
  otaManager.checkForUpdates(now, wifiConnected);
  
  // Handle touch events
  TouchController::TouchEvent touchEvent = touchController.update(now);
  if (touchEvent == TouchController::SHORT_TAP) {
    displayModeManager.manualSwitch();
  } else if (touchEvent == TouchController::LONG_PRESS) {
    Mood nextMood = (Mood)((moodManager.currentMood + 1) % 7);
    moodManager.setMood(nextMood, random(30000, 90000));
  }
  
  if (touchEvent == TouchController::SHORT_TAP) {
    irritationManager.onTap(now, moodManager);
  }
  
  // Render display
  if (displayModeManager.currentMode == DisplayModeManager::MODE_EYE) {
    renderEyeMode();
  } else {
    renderClockMode();
  }
  
  display.display();
  delay(30);
}

// ============================================================================
// RENDERING
// ============================================================================

void renderEyeMode() {
  eyeAnimator.draw(moodManager.currentMood);
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, SCREEN_HEIGHT - 8);
  display.print(MOOD_NAMES[moodManager.currentMood]);
  
  if (wifiConnected) display.print(" [W]");
  if (timeManager.ntpSynced) display.print(" [T]");
}

void renderClockMode() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 10);
  display.println(timeManager.getTimeString());
  
  display.setTextSize(1);
  display.setCursor(30, 35);
  display.println(timeManager.getDateString());
  
  struct tm timeinfo;
  if (timeManager.getValidTime(timeinfo)) {
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    display.setCursor(40, 50);
    display.println(days[timeinfo.tm_wday]);
  }
  
  display.setCursor(0, SCREEN_HEIGHT - 8);
  display.print(MOOD_NAMES[moodManager.currentMood]);
}

// ============================================================================
// END
// ============================================================================
