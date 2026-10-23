# Changelog

*[English version](CHANGELOG.en.md)*

## v0.9.9-beta14 (2026-10-23)

### Geändert
- **Zweiter Standort nur auf dem P4:** Auf dem S3 ist der zweite Standort ausgeschaltet (keine Pille, keine Karte in der Web-Oberfläche). Der S3 hat nur 10 bis 35 KB internen Speicher frei, und ein Ortswechsel holt viele Daten nacheinander; dabei scheiterten Abrufe, und die Web-Oberfläche war kaum erreichbar. Auf dem P4 (viel mehr Speicher) bleibt die Funktion. Wer sie auf dem S3 selbst ausprobieren will, baut mit `-DFUSION_ZWEITER_STANDORT=1`.
- Die Standort-Pille sitzt oben rechts und verdeckt nicht mehr das Update-Symbol.
