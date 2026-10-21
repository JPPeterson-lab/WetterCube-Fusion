# Changelog

*[English version](CHANGELOG.en.md)*

## v0.9.9-alpha18 (2026-10-21)

### Behoben
- **Verlauf der Außenwerte:** Der Abruf der letzten Tage von Open-Meteo wurde seit alpha17 bei knappem Speicher immer verschoben und lief nie. Er braucht jetzt kaum internen Speicher (die Antwort wird beim Empfang zerlegt statt als Ganzes gehalten) und läuft auch dann.
