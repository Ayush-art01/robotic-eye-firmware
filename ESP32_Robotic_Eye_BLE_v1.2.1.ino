/*
  ESP32 Robotic Eye BLE Firmware v1.2.1
  Board: ESP32 Dev Module (ESP32-WROOM-32, 4MB flash)
  OLED: 1.3-inch SH1106 128x64 I2C, address 0x3C
  Touch sensor: OUT -> GPIO27

  Features:
  - SH1106 OLED using U8g2 (confirmed working with this display)
  - Seven moods: Normal, Happy, Sleepy, Curious, Playful, Irritated, Angry
  - Automatic pupil movement and blinking
  - Touch: short tap toggles Eye/Clock view
  - Touch: 1-2 seconds cycles mood
  - Touch: hold 8 seconds resets saved Wi-Fi credentials
  - WiFiManager: open setup portal, no Wi-Fi credentials in source code
  - NTP clock in IST
  - BLE control for Android app (device name: RoboticEye)
  - BLE commands for mood, design, eye dimensions, brightness, speed and view
  - Expanded eye collection: Cute, Angry, Sleepy, Anime, Round, Oval, Cyber, Pixel
  - Optional phone notification overlay received over BLE; returns to previous screen

  NOTE:
  OTA is intentionally not enabled yet:
  the earlier OTA prototype used insecure HTTP handling and must be replaced
  with certificate-validated HTTPS + SHA-256 verification before enabling it.
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- Hardware ----------------
#define OLED_SDA 21
#define OLED_SCL 22
#define TOUCH_PIN 27
#define OLED_I2C_ADDRESS 0x3C

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
WiFiManager wifiManager;

// ---------------- BLE control protocol ----------------
// Android app service UUIDs. Keep these identical in the Android GATT client.
static const char *BLE_DEVICE_NAME = "RoboticEye";
static const char *BLE_SERVICE_UUID = "7b7a0001-7d5a-4f3c-9a2b-6e7f8a9b0001";
static const char *BLE_CONTROL_UUID = "7b7a0002-7d5a-4f3c-9a2b-6e7f8a9b0002";
static const char *BLE_STATUS_UUID  = "7b7a0003-7d5a-4f3c-9a2b-6e7f8a9b0003";

BLECharacteristic *bleControlCharacteristic = nullptr;
BLECharacteristic *bleStatusCharacteristic = nullptr;
bool bleClientConnected = false;
String bleLastCommand = "READY";

// ---------------- Time (India) ----------------
static const char *NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 19800;
static const int DAYLIGHT_OFFSET_SEC = 0;

// ---------------- Touch timings ----------------
static const unsigned long DEBOUNCE_MS = 250;
static const unsigned long MOOD_HOLD_MS = 1000;
static const unsigned long WIFI_RESET_HOLD_MS = 8000;

// ---------------- Mood ----------------
enum Mood : uint8_t {
  MOOD_NORMAL, MOOD_HAPPY, MOOD_SLEEPY, MOOD_CURIOUS,
  MOOD_PLAYFUL, MOOD_IRRITATED, MOOD_ANGRY, MOOD_COUNT
};

const char *MOOD_NAMES[MOOD_COUNT] = {
  "Normal", "Happy", "Sleepy", "Curious", "Playful", "Irritated", "Angry"
};

Mood currentMood = MOOD_NORMAL;
unsigned long moodUntil = 0;

// Eye style settings (changed by Android over BLE).
String eyeDesign = "cute";

// Temporary phone notification overlay. Content is forwarded only by the paired
// Android app while its notification feature is enabled and BLE is connected.
bool notificationActive = false;
String notificationApp = "";
String notificationTitle = "";
String notificationMessage = "";
unsigned long notificationUntil = 0;
int eyeWidth = 38;
int eyeHeight = 38;
int eyeSpacing = 56;       // distance between eye centres
int pupilRadius = 7;
int displayBrightness = 180;
int animationSpeed = 5;    // 1 = slow, 10 = fast

// ---------------- Display / animation state ----------------
enum ViewMode : uint8_t { VIEW_EYES, VIEW_CLOCK };
ViewMode viewMode = VIEW_EYES;

int pupilX = 0, pupilY = 0;
int targetX = 0, targetY = 0;
bool isBlinking = false;
unsigned long blinkStartedAt = 0;
unsigned long nextBlinkAt = 0;
unsigned long nextLookAt = 0;
unsigned long lastFrameAt = 0;

// ---------------- Touch state ----------------
bool touchWasDown = false;
bool moodHoldHandled = false;
bool wifiResetHandled = false;
unsigned long touchStartedAt = 0;
unsigned long lastTouchEventAt = 0;

bool wifiConnected = false;
bool timeValid = false;
unsigned long lastTimeCheckAt = 0;

// ---------------- Helpers ----------------
void showMessage(const char *line1, const char *line2 = "") {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 22, line1);
  if (line2 && line2[0]) display.drawStr(0, 40, line2);
  display.sendBuffer();
}

void setMood(Mood mood, unsigned long durationMs = 45000) {
  currentMood = mood;
  // durationMs == 0 means keep this mood until another command changes it.
  moodUntil = (durationMs == 0) ? 0 : millis() + durationMs;
  Serial.print("Mood: ");
  Serial.println(MOOD_NAMES[currentMood]);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalTimeout(0); // Keep setup portal available until configured
  wifiManager.setConnectTimeout(20);
  wifiManager.setBreakAfterConfig(true);

  showMessage("WiFi setup", "Connect to Robotic Eye Setup");
  Serial.println("[WiFi] Connecting / opening setup portal...");

  // Open, password-free setup AP as requested.
  // First boot or after reset: connect phone to 'Robotic Eye Setup',
  // then open 192.168.4.1 and select the home/hotspot network.
  bool connected = wifiManager.autoConnect("Robotic Eye Setup");
  wifiConnected = connected && WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    Serial.print("[WiFi] Connected. IP: ");
    Serial.println(WiFi.localIP());
    showMessage("WiFi connected", WiFi.localIP().toString().c_str());
    delay(900);
  } else {
    Serial.println("[WiFi] Not connected; eyes will continue offline.");
    showMessage("WiFi unavailable", "Eyes work offline");
    delay(900);
  }
}

bool readValidTime(struct tm &timeInfo) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &timeInfo);
  return true;
}

void startNtp() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("[Time] NTP sync requested.");
}

void updateTimeStatus() {
  if (millis() - lastTimeCheckAt < 5000) return;
  lastTimeCheckAt = millis();
  struct tm ti;
  timeValid = readValidTime(ti);
  wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected && !timeValid) startNtp();
}

void scheduleNextBlink() {
  unsigned long interval = 3000;
  interval = (interval * 5) / constrain(animationSpeed, 1, 10);
  if (currentMood == MOOD_SLEEPY) interval = 5200;
  else if (currentMood == MOOD_PLAYFUL) interval = 1700;
  else if (currentMood == MOOD_ANGRY) interval = 1100;
  else if (currentMood == MOOD_HAPPY) interval = 2200;
  nextBlinkAt = millis() + interval + random(0, 900);
}

void updateAnimation() {
  unsigned long now = millis();

  if (moodUntil && (long)(now - moodUntil) >= 0) {
    setMood(MOOD_NORMAL, 45000);
  }

  if (!isBlinking && (long)(now - nextBlinkAt) >= 0) {
    isBlinking = true;
    blinkStartedAt = now;
  }
  if (isBlinking) {
    unsigned long blinkDuration = (currentMood == MOOD_SLEEPY) ? 420 : 160;
    if (now - blinkStartedAt >= blinkDuration) {
      isBlinking = false;
      scheduleNextBlink();
    }
  }

  if ((long)(now - nextLookAt) >= 0) {
    int rangeX = (currentMood == MOOD_CURIOUS) ? 9 : 7;
    int rangeY = (currentMood == MOOD_SLEEPY) ? 3 : 5;
    targetX = random(-rangeX, rangeX + 1);
    targetY = random(-rangeY, rangeY + 1);
    nextLookAt = now + (currentMood == MOOD_PLAYFUL ? 700 : 1500) + random(0, 900);
  }

  if (pupilX < targetX) pupilX++;
  else if (pupilX > targetX) pupilX--;
  if (pupilY < targetY) pupilY++;
  else if (pupilY > targetY) pupilY--;
}

void drawEye(int cx, int cy) {
  int w = constrain(eyeWidth, 24, 54);
  int h = constrain(eyeHeight, 20, 50);
  int left = cx - w / 2;
  int top = cy - h / 2;

  if (eyeDesign == "angry") {
    display.drawRBox(left, top + 3, w, h - 3, 7);
    display.setDrawColor(0);
    display.drawTriangle(left, top, left + w / 2, top + 8, left + w, top + 2);
    display.setDrawColor(1);
  } else if (eyeDesign == "sleepy" || currentMood == MOOD_SLEEPY) {
    display.drawRFrame(left, top, w, h, 9);
    display.drawBox(left + 2, top + 2, w - 4, max(3, h / 4));
  } else if (eyeDesign == "round") {
    display.drawDisc(cx, cy, min(w, h) / 2);
  } else if (eyeDesign == "oval") {
    display.drawEllipse(cx, cy, w / 2, h / 2);
  } else if (eyeDesign == "cyber") {
    display.drawRFrame(left, top, w, h, 3);
    display.drawHLine(left + 5, top + 4, w - 10);
    display.drawVLine(left + 4, top + 5, h - 10);
    display.drawVLine(left + w - 5, top + 5, h - 10);
  } else if (eyeDesign == "pixel") {
    display.drawBox(left + 4, top + 4, w - 8, h - 8);
  } else if (eyeDesign == "anime") {
    display.drawEllipse(cx, cy, w / 2, h / 2);
    display.drawHLine(left + 5, top + 3, w - 10);
  } else {
    display.drawRBox(left, top, w, h, 10);
  }

  int px = cx + pupilX;
  int py = cy + pupilY;
  int pr = constrain(pupilRadius, 3, 12);
  display.setDrawColor(eyeDesign == "pixel" ? 0 : 0);
  display.drawDisc(px, py, pr);
  display.setDrawColor(1);
  display.drawDisc(px - max(1, pr / 3), py - max(1, pr / 3), max(1, pr / 4));
}

void drawClosedEyes() {
  display.drawLine(17, 32, 55, 32);
  display.drawLine(73, 32, 111, 32);
  if (currentMood == MOOD_ANGRY) {
    display.drawLine(18, 25, 52, 30);
    display.drawLine(110, 25, 76, 30);
  }
}

void renderEyes() {
  display.clearBuffer();
  if (isBlinking) {
    drawClosedEyes();
  } else {
    int leftCx = 64 - eyeSpacing / 2;
    int rightCx = 64 + eyeSpacing / 2;
    drawEye(leftCx, 31);
    drawEye(rightCx, 31);
  }

  // Mood label along bottom edge.
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(0, 63, MOOD_NAMES[currentMood]);
  if (wifiConnected) display.drawStr(94, 63, "WiFi");
  display.sendBuffer();
}


// Draw a short notification card. OLED has only 128x64 pixels, so long
// notification text is clipped to the first few readable lines.
String fitText(String value, uint8_t maxChars) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  while (value.indexOf("  ") >= 0) value.replace("  ", " ");
  value.trim();
  if (value.length() > maxChars) value = value.substring(0, maxChars - 1) + "~";
  return value;
}

void renderNotification() {
  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(0, 8, fitText(notificationApp, 20).c_str());
  display.drawHLine(0, 11, 128);
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 24, fitText(notificationTitle, 20).c_str());

  display.setFont(u8g2_font_5x8_tf);
  String msg = fitText(notificationMessage, 72);
  int y = 37;
  while (msg.length() > 0 && y <= 60) {
    int cut = min((int)msg.length(), 21);
    if (cut < msg.length()) {
      int space = msg.lastIndexOf(' ', cut);
      if (space > 5) cut = space;
    }
    String line = msg.substring(0, cut);
    line.trim();
    display.drawStr(0, y, line.c_str());
    msg = msg.substring(cut);
    msg.trim();
    y += 11;
  }
  display.sendBuffer();
}

void handleBleNotificationCommand(const String &payload) {
  int first = payload.indexOf('|');
  int second = first < 0 ? -1 : payload.indexOf('|', first + 1);
  int third = second < 0 ? -1 : payload.indexOf('|', second + 1);
  if (first < 0 || second < 0 || third < 0) {
    sendBleStatus("ERROR:NOTIFY_FORMAT");
    return;
  }

  notificationApp = fitText(payload.substring(0, first), 20);
  notificationTitle = fitText(payload.substring(first + 1, second), 28);
  notificationMessage = fitText(payload.substring(second + 1, third), 72);
  int seconds = constrain(payload.substring(third + 1).toInt(), 3, 15);
  notificationUntil = millis() + (unsigned long)seconds * 1000UL;
  notificationActive = true;
  sendBleStatus("OK:NOTIFICATION_SHOWN");
}

void renderClock() {
  display.clearBuffer();
  struct tm ti;
  if (readValidTime(ti)) {
    char timeBuf[12];
    char dateBuf[20];
    strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", &ti); // 12-hour format with AM/PM
    strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &ti);
    display.setFont(u8g2_font_helvB14_tf);
    int width = display.getStrWidth(timeBuf);
    display.drawStr((128 - width) / 2, 29, timeBuf);
    display.setFont(u8g2_font_6x12_tf);
    width = display.getStrWidth(dateBuf);
    display.drawStr((128 - width) / 2, 45, dateBuf);
    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    display.drawStr(2, 62, days[ti.tm_wday]);
  } else {
    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(14, 27, "Waiting for time");
    display.drawStr(20, 45, wifiConnected ? "NTP syncing..." : "WiFi offline");
  }
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(94, 63, MOOD_NAMES[currentMood]);
  display.sendBuffer();
}

void handleTouch() {
  unsigned long now = millis();
  bool down = digitalRead(TOUCH_PIN) == HIGH;

  if (down && !touchWasDown) {
    touchWasDown = true;
    touchStartedAt = now;
    moodHoldHandled = false;
    wifiResetHandled = false;
  }

  if (down && touchWasDown) {
    unsigned long held = now - touchStartedAt;
    if (!wifiResetHandled && held >= WIFI_RESET_HOLD_MS) {
      wifiResetHandled = true;
      Serial.println("[WiFi] Reset requested by 8-second touch hold.");
      showMessage("Resetting WiFi", "Restarting...");
      wifiManager.resetSettings();
      delay(500);
      ESP.restart();
    } else if (!moodHoldHandled && held >= MOOD_HOLD_MS) {
      moodHoldHandled = true;
      Mood next = (Mood)((currentMood + 1) % MOOD_COUNT);
      setMood(next, 45000);
    }
  }

  if (!down && touchWasDown) {
    unsigned long held = now - touchStartedAt;
    touchWasDown = false;

    if (held < MOOD_HOLD_MS && held < WIFI_RESET_HOLD_MS &&
        now - lastTouchEventAt >= DEBOUNCE_MS) {
      lastTouchEventAt = now;
      viewMode = (viewMode == VIEW_EYES) ? VIEW_CLOCK : VIEW_EYES;
      Serial.println(viewMode == VIEW_EYES ? "[Touch] Eye view" : "[Touch] Clock view");
    }
  }
}


// ---------------- BLE command handling ----------------
void sendBleStatus(const String &message) {
  bleLastCommand = message;
  if (bleStatusCharacteristic) {
    bleStatusCharacteristic->setValue(message.c_str());
    if (bleClientConnected) bleStatusCharacteristic->notify();
  }
  Serial.print("[BLE] ");
  Serial.println(message);
}

Mood moodFromName(String name, bool &valid) {
  name.trim();
  name.toLowerCase();
  valid = true;
  if (name == "normal") return MOOD_NORMAL;
  if (name == "happy") return MOOD_HAPPY;
  if (name == "sleepy") return MOOD_SLEEPY;
  if (name == "curious") return MOOD_CURIOUS;
  if (name == "playful") return MOOD_PLAYFUL;
  if (name == "irritated") return MOOD_IRRITATED;
  if (name == "angry") return MOOD_ANGRY;
  valid = false;
  return MOOD_NORMAL;
}

// Commands are UTF-8 text, one per write:
// PING
// MOOD:Happy
// DESIGN:Cute|Angry|Sleepy|Anime
// WIDTH:42  HEIGHT:38  SPACING:56  PUPIL:8
// BRIGHTNESS:180  SPEED:5  VIEW:EYES|CLOCK
// NOTIFY:App|Sender|Message|6
class EyeBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
    sendBleStatus("CONNECTED");
  }
  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Client disconnected; advertising restarted.");
  }
};

class EyeBleControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String command = String(characteristic->getValue().c_str());
    command.trim();
    if (!command.length()) return;

    String upper = command;
    upper.toUpperCase();

    if (upper == "PING") {
      sendBleStatus("PONG;VERSION=1.2.1");
      return;
    }

    int separator = command.indexOf(':');
    if (separator < 1) {
      sendBleStatus("ERROR:Use KEY:VALUE");
      return;
    }

    String key = command.substring(0, separator);
    String value = command.substring(separator + 1);
    key.trim();
    value.trim();
    key.toUpperCase();

    if (key == "MOOD") {
      bool valid = false;
      Mood requested = moodFromName(value, valid);
      if (!valid) { sendBleStatus("ERROR:UNKNOWN_MOOD"); return; }
      setMood(requested, 0);
      sendBleStatus("OK:MOOD=" + String(MOOD_NAMES[currentMood]));
    } else if (key == "DESIGN") {
      value.toLowerCase();
      if (value == "cute" || value == "angry" || value == "sleepy" || value == "anime" || value == "round" || value == "oval" || value == "cyber" || value == "pixel") {
        eyeDesign = value;
        if (value == "angry") setMood(MOOD_ANGRY, 0);
        else if (value == "sleepy") setMood(MOOD_SLEEPY, 0);
        sendBleStatus("OK:DESIGN=" + eyeDesign);
      } else {
        sendBleStatus("ERROR:UNKNOWN_DESIGN");
      }
    } else if (key == "NOTIFY") {
      handleBleNotificationCommand(value);
    } else if (key == "WIDTH") {
      eyeWidth = constrain(value.toInt(), 24, 54);
      sendBleStatus("OK:WIDTH=" + String(eyeWidth));
    } else if (key == "HEIGHT") {
      eyeHeight = constrain(value.toInt(), 20, 50);
      sendBleStatus("OK:HEIGHT=" + String(eyeHeight));
    } else if (key == "SPACING") {
      eyeSpacing = constrain(value.toInt(), 42, 68);
      sendBleStatus("OK:SPACING=" + String(eyeSpacing));
    } else if (key == "PUPIL") {
      pupilRadius = constrain(value.toInt(), 3, 12);
      sendBleStatus("OK:PUPIL=" + String(pupilRadius));
    } else if (key == "BRIGHTNESS") {
      displayBrightness = constrain(value.toInt(), 10, 255);
      display.setContrast(displayBrightness);
      sendBleStatus("OK:BRIGHTNESS=" + String(displayBrightness));
    } else if (key == "SPEED") {
      animationSpeed = constrain(value.toInt(), 1, 10);
      sendBleStatus("OK:SPEED=" + String(animationSpeed));
    } else if (key == "VIEW") {
      value.toUpperCase();
      if (value == "EYES") viewMode = VIEW_EYES;
      else if (value == "CLOCK") viewMode = VIEW_CLOCK;
      else { sendBleStatus("ERROR:UNKNOWN_VIEW"); return; }
      sendBleStatus("OK:VIEW=" + value);
    } else {
      sendBleStatus("ERROR:UNKNOWN_COMMAND");
    }
  }
};

void startBle() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new EyeBleServerCallbacks());

  BLEService *service = server->createService(BLE_SERVICE_UUID);

  bleControlCharacteristic = service->createCharacteristic(
    BLE_CONTROL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  bleControlCharacteristic->setCallbacks(new EyeBleControlCallbacks());
  bleControlCharacteristic->setValue("READY");

  bleStatusCharacteristic = service->createCharacteristic(
    BLE_STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  bleStatusCharacteristic->addDescriptor(new BLE2902());
  bleStatusCharacteristic->setValue("READY");

  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as RoboticEye");
  sendBleStatus("READY;VERSION=1.2.1");
}


void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed((uint32_t)esp_random());

  pinMode(TOUCH_PIN, INPUT);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.setI2CAddress(OLED_I2C_ADDRESS * 2); // U8g2 expects 8-bit address
  display.begin();
  display.setPowerSave(0);
  display.setContrast(displayBrightness);

  showMessage("Robotic Eye", "Starting...");
  Serial.println("\n=== ESP32 Robotic Eye BLE v1.2.0 ===");
  Serial.println("OLED: SH1106 128x64 @ 0x3C");
  Serial.println("Touch: GPIO27");

  scheduleNextBlink();
  nextLookAt = millis() + 500;

  // Start BLE before Wi-Fi provisioning so the app can discover the eye
  // even while the open Wi-Fi setup portal is waiting for credentials.
  startBle();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) startNtp();

  showMessage("Ready!", "Touch to switch view");
  delay(700);
}

void loop() {
  handleTouch();
  updateTimeStatus();
  updateAnimation();

  if (millis() - lastFrameAt >= 33) {
    lastFrameAt = millis();
    if (notificationActive) {
      if ((long)(millis() - notificationUntil) >= 0) {
        notificationActive = false;
      } else {
        renderNotification();
      }
    }
    if (!notificationActive) {
      if (viewMode == VIEW_EYES) renderEyes();
      else renderClock();
    }
  }
  delay(2);
}
