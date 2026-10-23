# Changelog

*[English version](CHANGELOG.en.md)*

## v0.9.9-beta13 (2026-10-23)

### Verbessert
- **Standortwechsel:** Wetter und 7-Tage-Vorschau erscheinen jetzt sofort, der Rest wird danach nachgeladen. Warnungen, Warnkarte, DWD-Pollen und Biowetter werden nur neu geholt, wenn sich Region oder Zone ändern; bei zwei Orten in derselben Region wandert nur der Marker auf der Karte. Vor jedem Abruf wartet das Gerät kurz, bis genug interner Speicher frei ist (sonst scheiterten Abrufe mit „HTTP -1“ und die Web-Oberfläche öffnete sich kaum noch). Das Log nennt jeden Wechsel („[Ort] …“).
- **Bildschirmfoto:** Der Knopf in der Web-Oberfläche liest den Bildschirminhalt direkt aus dem Display-Speicher und braucht keinen Zusatzspeicher mehr (vorher scheiterte er mit „zu wenig Speicher“, sobald die Warnkarte geladen war).
