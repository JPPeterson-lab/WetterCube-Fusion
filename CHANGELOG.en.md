# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0-rc32 (2026-11-14)

### New (P4 only)
- **Sensor silent:** If an MQTT sensor delivers no value for an adjustable time (default 2 hours, also if it never had one since start), it appears in red in the notice tile, is logged and can be sent to the phone via ntfy.
- **Frost guard:** Frost and ground frost for the coming nights with advice for garden and balcony, in the extra field of the start screen and in the evening via ntfy. With your own outdoor sensor (can be switched off) the night is corrected by the measurement; if it measures 0.5 °C or less, the notice tile shows “Pflanzen rein” (bring plants in).
- **Forecast check:** How good was the forecast? The Fusion stores it daily on the memory card and compares it with the actual values, with your own outdoor sensor (can be switched off) once it has three days of values, otherwise with the weather data. Shown in the extra field.
