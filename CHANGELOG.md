# Changelog

*[English version](CHANGELOG.en.md)*

## v1.0.0-rc3 (2026-10-28)

### Behoben
- **P4: Update über WLAN scheiterte** mit „Die Firmware-Datei ist beschädigt“. Ursache: Der zweite Firmware-Slot lag teilweise hinter 16 MB; dort kann das System die Firmware nicht prüfen und nicht starten (24-Bit-Flashadressen). Die Partitionstabelle des P4 ist neu: alles liegt unter 16 MB, die Slots sind je knapp 8 MB groß (die Firmware ist 7,2 MB). Updates (und Rollback) laufen jetzt in beide Richtungen. **Einmalig nötig:** den P4 mit rc3 per USB über den Web-Installer neu installieren (Haken „Gerät löschen“ NICHT setzen, die Einstellungen bleiben). Ein P4 mit rc1 oder rc2 kann per WLAN nicht auf rc3 aktualisieren und meldet dabei denselben Fehler. Der S3 ist nicht betroffen.
- **P4: Update-Hinweis** sagt nicht mehr, das Display flackere (das tut der P4 nicht); er zeigt „Gerät nicht vom Strom trennen“ und den Fortschrittsbalken.

### Neu
- **Fehlersuche beim Update:** Scheitert die Prüfung der Firmware, steht im Diagnose-Log (`/log`) die Meldung des Systems und ein Vergleich des Flash-Inhalts mit den empfangenen Daten.
