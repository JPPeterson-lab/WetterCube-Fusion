# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0-rc1 (2026-10-28)

### New
- **Release candidate:** Fusion runs on **two boards**, the **ESP32-P4** (recommended) and the **ESP32-S3**. The web installer has a board selection and installs the matching firmware; over-the-air updates fetch the firmware of the own board automatically.
- **Timer and stopwatch:** "Timer" button in the menu. When it expires the display flashes; on the P4 an alarm tone plays from the speaker (five tones to choose from, adjustable volume, "listen to tone" in the web interface).
- **Warning tone (P4):** For a new DWD or pollen warning the selected tone plays briefly; adjustable (on/off, tone, volume), silent during quiet hours, a running timer alarm takes priority.

### Changed
- **P4:** display and touch rotated by 180° (the connectors sit on the other side in the case).
- **P4:** no more lost image parts while drawing the interface (the copy into the display is awaited).
- Documentation: new screenshots (from the P4), notes about display (1024×600) and touch version when buying the S3.
