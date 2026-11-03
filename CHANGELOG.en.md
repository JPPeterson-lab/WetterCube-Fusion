# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0 (2026-11-22)

**First stable version.** After 34 release candidates the Fusion is finished: weather, DWD warnings with map, pollen and allergens, air quality, biometeorology, environment sensors (internal, MQTT, Home Assistant and WetterCube Node), airing traffic light, phone messages via ntfy, Next displays, updates with rollback and recovery app. New features now only come to the ESP32-P4; the ESP32-S3 keeps getting bug fixes.

### Fixed (P4 only)
- **Forecast check in the extra field:** instead of an empty box (the ± sign was missing from the font) it now reads “morgen 1,3 °C daneben”.
