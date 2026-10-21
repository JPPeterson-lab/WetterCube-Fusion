# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v0.9.9-alpha17 (2026-10-21)

### Fixed
- **Update with little memory:** The firmware update failed with "not enough memory" when a lot ran at the same time (sensors via MQTT, Home Assistant). The MQTT client now pauses during the update and reconnects afterwards (or after a failure); the log shows the free memory before and after the pause. The chart data now lives in the large memory (PSRAM), and the fetch of outdoor values waits when internal memory is low.
