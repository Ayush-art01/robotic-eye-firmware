# ESP32 Mini Emotional Robotic Eye

Firmware project for an ESP32 + 128×64 monochrome I²C OLED + TTP223 touch sensor.

## Status

**Prototype — not yet approved for unattended/customer OTA deployment.** The sketch needs a physical-device test and OTA security work (HTTPS certificate validation, cryptographic signature verification, anti-downgrade policy, OTA partition/rollback validation). Do not treat the current OTA code as production-secure.

## Hardware assumptions

- Classic ESP32 DevKit / ESP32-WROOM-32 (confirm your exact board)
- 128×64 SSD1306 I²C OLED, default address configured as `0x3C`
- TTP223 OUT → GPIO 27
- OLED SDA → GPIO 21, SCL → GPIO 22, VCC → 3.3V, GND → GND

If the display address is `0x3D`, change `SSD1306_I2C_ADDRESS` in the sketch. SH1106 displays need a compatible SH1106 library and code adaptation.

## Arduino IDE setup

1. Install Arduino IDE 2.x and add Espressif ESP32 board support.
2. Select the exact ESP32 board and a suitable flash/partition configuration.
3. Install these libraries through Library Manager:
   - Adafruit GFX Library
   - Adafruit SSD1306
   - WiFiManager (tzapu)
   - ArduinoJson (version compatible with the sketch)
4. Open `firmware/ESP32_Robotic_Eye_Complete_OTA/ESP32_Robotic_Eye_Complete_OTA.ino`.
5. Select the correct COM port and compile. Do not upload to a recipient's device before testing on your own board.

## OTA manifest

`firmware/latest.json.example` is a schema example only. It is not a valid release until you publish a real firmware binary, use its exact HTTPS URL, calculate its hash, and implement verification in the firmware. The current sketch's OTA implementation is not production-secure; a hash alone does not authenticate who published the firmware.

## Roadmap

1. Confirm board and OLED controller/address.
2. Compile and test OLED + touch on hardware.
3. Fix and test animation, touch debounce, mood/irritation timing.
4. Verify NTP validity and offline startup.
5. Add Bluetooth time fallback for the selected ESP32 variant.
6. Harden OTA (trusted TLS, signature verification, anti-downgrade, partition/rollback/recovery tests).
7. Test interrupted downloads and failed boots on your own device.

## Release workflow

Only publish a firmware `.bin` after it compiles and has been tested on your own ESP32. Never put Wi-Fi passwords, private signing keys, or personal credentials in this public repository. The firmware signing **public** key may be embedded in firmware; keep the signing **private** key offline and secret.
