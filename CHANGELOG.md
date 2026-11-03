# Changelog

*[English version](CHANGELOG.en.md)*

## v1.0.0-rc32 (2026-11-14)

### Neu (nur P4)
- **Sensor still:** Liefert ein MQTT-Sensor eine einstellbare Zeit lang keinen Wert (Standard 2 Stunden, auch wenn er seit dem Start noch nie einen hatte), erscheint er rot in der Hinweis-Kachel, steht im Log und kommt auf Wunsch per ntfy aufs Handy.
- **Frost-Wächter:** Frost und Bodenfrost der kommenden Nächte mit Rat für Garten und Balkon, im Zusatzfeld des Startscreens und abends per ntfy. Mit eigenem Außensensor (abschaltbar) wird die Nacht nach der Messung korrigiert; misst er 0,5 °C oder weniger, zeigt die Hinweis-Kachel „Pflanzen rein“.
- **Prognose-Check:** Wie gut war die Vorhersage? Der Fusion legt sie täglich auf der Speicherkarte ab und vergleicht sie mit den tatsächlichen Werten, mit dem eigenen Außensensor (abschaltbar), sobald er drei Tage Werte hat, sonst mit den Wetterdaten. Anzeige im Zusatzfeld.
