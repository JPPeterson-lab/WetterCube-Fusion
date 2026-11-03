# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0-rc15 (2026-11-05)

### New
- **Recovery starts by itself after crash loops:** If the firmware crashes five times in a row (even so early that no user interface appears yet), the device starts the recovery app instead of staying dark in a loop. The recovery app changes nothing without your choice on the screen. New recovery app 1.1.0 with its own notice text; it is only installed by a full installation (web installer or the installer in WetterCube Control), while the automatic start itself comes with the normal update and also works with recovery app 1.0.0.
