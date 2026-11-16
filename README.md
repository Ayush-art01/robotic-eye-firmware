# Robotic Eye Firmware v1.2.1

## What's new
- OLED clock now uses 12-hour format: `11:34 PM` / `08:30 AM`.
- Android app is unchanged; this is an ESP32 firmware-only update.
- Existing BLE eye designs, controls, and phone-notification overlay are retained from v1.2.0.

## Upload
1. Open `ESP32_Robotic_Eye_BLE_v1.2.1.ino` in Arduino IDE.
2. Select **ESP32 Dev Module** and the **No FS 4MB (2MB APP x2)** partition scheme.
3. Click **Verify** first. If compilation succeeds, upload to the ESP32.
4. Keep the working v1.0.1 firmware as a fallback.

## Notes
- Android app is not modified for this change.
- OTA is not enabled in this firmware.
- Verify the sketch in your Arduino IDE before flashing.
