# Changelog

*[English version](CHANGELOG.en.md)*

## v1.0.0 (2026-11-22)

**Erste stabile Version.** Nach 34 Release Candidates ist der Fusion fertig: Wetter, DWD-Warnungen mit Karte, Pollen und Allergene, Luftqualität, Biowetter, Umweltsensoren (intern, MQTT, Home Assistant und WetterCube Node), Lüften-Ampel, Handy-Nachrichten per ntfy, Next-Displays, Updates mit Rollback und Notfall-App. Neue Funktionen kommen nur noch auf den ESP32-P4, der ESP32-S3 bekommt weiter Fehlerbehebungen.

### Behoben (nur P4)
- **Prognose-Check im Zusatzfeld:** Statt eines leeren Kästchens (das Zeichen ± fehlte in der Schrift) steht jetzt „morgen 1,3 °C daneben“.
