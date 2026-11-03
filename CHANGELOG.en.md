# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v1.0.0-rc33 (2026-11-16)

### New (P4 only)
- **WetterCube Node without Home Assistant or MQTT:** On the Sensors page, “Search for Nodes on the home network” finds the WetterCube Nodes on the Wi-Fi (Node 0.4.0 or later). Their values (temperature, humidity, air pressure, dew point) can be selected like other sensors and are read directly from the Node every 30 seconds. If the search finds nothing, a Node can be entered by its IP address. Node values count for airing, limits and “Sensor silent”.

### Fixed
- **ntfy:** DWD warnings already reported are not reported again after a restart or update.
