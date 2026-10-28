# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0-rc3 (2026-10-28)

### Fixed
- **P4: Wi-Fi update failed** with "Die Firmware-Datei ist beschädigt" (firmware file is corrupt). Cause: part of the second firmware slot lay beyond 16 MB, where the system can neither verify nor boot the firmware (24-bit flash addresses). The P4 partition table is new: everything is below 16 MB, the slots are almost 8 MB each (the firmware is 7.2 MB). Updates (and rollback) now work in both directions. **Needed once:** reinstall the P4 with rc3 over USB using the web installer (do NOT tick "Erase device", your settings stay). A P4 running rc1 or rc2 cannot update to rc3 over Wi-Fi and reports the same error. The S3 is not affected.
- **P4: update notice** no longer says the display flickers (it does not on the P4); it shows "do not disconnect power" and the progress bar.

### New
- **Update troubleshooting:** if the firmware check fails, the diagnostic log (`/log`) shows the system's message and a comparison of the flash contents with the received data.
