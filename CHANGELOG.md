# Changelog

*[English version](CHANGELOG.en.md)*

## v0.9.9-alpha17 (2026-10-21)

### Behoben
- **Update bei knappem Speicher:** Das Firmware-Update scheiterte mit „zu wenig Speicher“, wenn viel gleichzeitig lief (Sensoren per MQTT, Home Assistant). Der MQTT-Client pausiert jetzt während des Updates und verbindet danach (oder nach einem Fehler) wieder; das Log nennt den freien Speicher vor und nach der Pause. Die Verlaufsdaten der Diagramme liegen nun im großen Speicher (PSRAM), und der Abruf der Außenwerte wartet, wenn der interne Speicher knapp ist.
