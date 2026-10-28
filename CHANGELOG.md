# Changelog

*[English version](CHANGELOG.en.md)*

## v1.0.0-rc1 (2026-10-28)

### Neu
- **Release Candidate:** Der Fusion läuft auf **zwei Boards**, dem **ESP32-P4** (empfohlen) und dem **ESP32-S3**. Der Web-Installer hat eine Boardauswahl und installiert die passende Firmware; Updates über WLAN holen automatisch die Firmware des eigenen Boards.
- **Timer und Stoppuhr:** Taste „Timer“ im Menü. Beim Ablauf blinkt das Display; auf dem P4 spielt ein Alarmton aus dem Lautsprecher (fünf Töne zur Auswahl, Lautstärke einstellbar, „Ton probehören“ in der Web-Oberfläche).
- **Warnton (P4):** Bei neuer DWD- oder Pollenwarnung ertönt kurz der gewählte Ton; einstellbar (ein/aus, Ton, Lautstärke), in der Ruhezeit still, ein laufender Timer-Alarm hat Vorrang.

### Geändert
- **P4:** Anzeige und Touch um 180° gedreht (die Anschlüsse liegen im Gehäuse auf der anderen Seite).
- **P4:** Beim Zeichnen der Oberfläche werden keine Bildteile mehr verloren (die Kopie ins Display wird abgewartet).
- Dokumentation: Bildschirmfotos neu (vom P4), Hinweise zu Display (1024×600) und Touch-Ausführung beim Kauf des S3.
