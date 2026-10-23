# Changelog

## Unreleased
- Added project setup instructions and hardware assumptions.
- Corrected SSD1306 `begin()` argument order and moved the I²C address macro before use.
- Pointed the manifest URL to this repository.
- Corrected irritation timestamp type/initialization and rate-limited irritation decay.
- Changed NTP status handling so `configTime()` alone does not mark time as synchronized.
- Clearly marked OTA as a prototype requiring security hardening.
