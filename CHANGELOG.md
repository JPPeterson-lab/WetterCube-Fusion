# Changelog

*[English version](CHANGELOG.en.md)*

## v1.0.0-rc33 (2026-11-16)

### Neu (nur P4)
- **WetterCube Node ohne Home Assistant und ohne MQTT:** Auf der Seite Sensoren findet „Nodes im Heimnetz suchen“ die WetterCube Nodes im WLAN (ab Node 0.4.0). Ihre Werte (Temperatur, Luftfeuchte, Luftdruck, Taupunkt) lassen sich wie andere Sensoren auswählen und werden alle 30 Sekunden direkt vom Node gelesen. Findet die Suche nichts, lässt sich ein Node mit seiner IP-Adresse eintragen. Node-Werte zählen für Lüften, Warngrenzen und „Sensor still“.

### Behoben
- **ntfy:** Bereits gemeldete DWD-Warnungen werden nach einem Neustart oder Update nicht noch einmal gemeldet.
