# Credits & third-party licences / Credits & Drittanbieter-Lizenzen

> **English summary.** This project uses third-party resources. The sections below are in German; licence texts are quoted in their original language. In short:
>
> - **Weather data / Wetterdaten:** "Weather data by [Open-Meteo.com](https://open-meteo.com)", licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) ([open-meteo.com/en/licence](https://open-meteo.com/en/licence)). Values are partly converted and summarised in the WetterCube (for example pollen levels and daily maxima). The free API is for **non-commercial use only**; commercial use needs a licence from Open-Meteo.
> - **Location search:** Location data by [GeoNames](https://www.geonames.org) via Open-Meteo (CC BY 4.0).
> - **Pollen and air quality:** "Contains modified Copernicus Atmosphere Monitoring Service information", provided via Open-Meteo. Neither the European Commission nor ECMWF is responsible for any use of the data.
> - **German Weather Service (DWD):** Warnings, pollen forecast, bio-weather and warning map from [DWD Opendata](https://opendata.dwd.de), licence CC BY 4.0, credited as "Quelle: Deutscher Wetterdienst" next to the displayed data.
> - **Weather icons:** [Meteocons](https://github.com/basmilius/weather-icons) by Bas Milius (MIT); very light outlines were recoloured for the light theme.
> - **UI icons:** [Icons8 Flat Color Icons](https://github.com/icons8/flat-color-icons) (MIT or Good Boy License).
> - **Font:** [Montserrat](https://github.com/JulietaUla/Montserrat) (SIL OFL 1.1).
> - **Drivers and libraries:** Waveshare example code and Espressif `esp_lcd_touch` (Apache 2.0), LVGL 8 incl. LodePNG (MIT / zlib), ArduinoJson (MIT), ESP-IDF (Apache 2.0), Espressif mDNS, esp_hosted and esp_wifi_remote (Apache 2.0).
> - **Optional notifications:** [ntfy](https://ntfy.sh) (no code shipped).
> - **UI design and export:** [PicoPixel](https://picopixel.io)
>
> **Project licence:** source code Apache License 2.0 ([LICENSE](LICENSE), [NOTICE](NOTICE)); texts, images and documentation CC BY-NC 4.0 ([LICENSE-Dokumentation.txt](LICENSE-Dokumentation.txt)). Third-party resources keep their own licences.

Dieses Projekt verwendet Ressourcen Dritter. Die jeweiligen Lizenzbedingungen
sind nachfolgend aufgeführt.

---

## Wetter-Icons und Sensor-Icons

**Quelle:** [Meteocons](https://github.com/basmilius/weather-icons) von Bas Milius

**Lizenz:** MIT License (Copyright (c) 2020-present Bas Milius)

**Wetter-Icons** (Stil „Fill“, 32 Symbole mit Tag- und Nachtvariante, als RGB565A8-Arrays für LVGL eingebettet; Dateien unter `assets/icons/wetter/`; die früheren 16 Symbole unter `assets/icons/weather/` sind nur noch Platzhalter, `alert` steht auf den Warnbildschirmen):
- `day_clear`, `day_partial_cloud`, `day_rain`, `day_snow` – Tag: klar, teilweise bewölkt, Regenschauer, Schneeschauer
- `night_full_moon_clear`, `night_full_moon_partial_cloud`, `night_full_moon_rain`, `night_full_moon_snow` – Nacht (Mond) mit denselben Wetterlagen
- `overcast` – bedeckt, `fog` – Nebel, `mist` – Dunst
- `rain` – Regen und Nieselregen, `rain_thunder` – Regen mit Gewitter, `thunder` – Gewitter
- `sleet` – Schneeregen, `snow` – Schnee
- `alert` – Warn-Icon (Pollenwarnung)

**Sensor-Icons** (Stil „Fill“, statisch, als PNG mit 128×128 Pixeln für die Oberfläche in PicoPixel; Dateien unter `assets/icons/meteocons/`): `thermometer`, `thermometer-celsius`, `humidity`, `wind`, `wind-direction-n`, `barometer`, `compass`, `pollen`, `pollen-flower`, `pollen-tree`, `pollen-grass`, `pollen-weed`, `uv-index`, `sunrise`, `sunset`, `raindrop`.

**Animierte Symbole** (nur ESP32-P4): Die animierten Meteocons (Paket `@meteocons/svg`, Stil „Fill“, SMIL-Animationen) werden mit `tools/anim_render/` Bild für Bild in
128×128-PNG gerendert (12 Bilder pro Sekunde) und mit `tools/anim_pack.py` zu `assets/icons/anim/wetteranim.bin` gepackt. **Mondphasen-Symbole** (`moon-new`,
`moon-waxing-crescent`, `moon-first-quarter`, `moon-waxing-gibbous`, `moon-full`, `moon-waning-gibbous`, `moon-last-quarter`, `moon-waning-crescent`) und `uv-index` stammen
ebenfalls von den Meteocons. Die Animationsdatei liegt nicht im öffentlichen Repository, nur in den Firmware-Dateien.

Änderung gegenüber dem Original: Sehr helle Umrandungen (z. B. der Wolken, des Nebels, des Windes) wurden auf ein mittleres Blaugrau (`#94A3B8`) gesetzt, damit sie auch auf dem hellen Farbthema sichtbar sind.

Lizenztext der Meteocons (MIT):

```
MIT License

Copyright (c) 2020-present Bas Milius

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Frühere Versionen dieses Projekts nutzten stattdessen Wetter-Icons von
[Dovora](https://www.dovora.com/resources/weather-icons/) (CC BY-SA 4.0). Sie sind in der Fusion nicht mehr enthalten.

---

## Bedienelemente-Icons

**Quelle:** [Icons8 Flat Color Icons](https://github.com/icons8/flat-color-icons) – © Icons8

**Lizenz:** Die Icons stehen unter der MIT-Lizenz **oder** der [Good Boy License](https://icons8.com/good-boy-license/) von Icons8 (laut [LICENSE.md](https://github.com/icons8/flat-color-icons/blob/master/LICENSE.md)). Erlaubt sind Herunterladen, Ändern und Weitergeben, auch kommerziell; eine Namensnennung ist nicht vorgeschrieben, wird hier aber gern gegeben.

Verwendete Icons (als PNG in PicoPixel eingebunden, als Arrays für LVGL eingebettet; Dateien unter `src/ui/images/`):
- `home` – Home-Taste (`fc_home`)
- `settings` – Menü-Zahnrad (`fc_settings`)
- `left` und `right` – Vor- und Zurück-Pfeile (`fc_left_60`, `fc_right_60`)
- `left_down2` und `right_up2` – diagonale Pfeile (`fc_left_down2_38`, `fc_right_up2_38`)
- `calculator` – Taschenrechner (`fc_calculator`)

---

## Wetterdaten

**Quelle:** [Open-Meteo](https://open-meteo.com) – Wetter, Pollen, Luftqualität, Standortsuche

„Weather data by [Open-Meteo.com](https://open-meteo.com)“. Lizenz und Quellen der Daten: [open-meteo.com/en/licence](https://open-meteo.com/en/licence). Die Werte werden im WetterCube umgerechnet und zusammengefasst (zum Beispiel Pollenstufen und Tageshöchstwerte). Orte werden über die Geocoding-Schnittstelle gesucht, deren Daten von [GeoNames](https://www.geonames.org) stammen (CC BY 4.0).

**Lizenz der Daten:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (Namensnennung, Open-Meteo.com). Die kostenlose API ist nur für **nicht-kommerzielle** Nutzung vorgesehen ([Nutzungsbedingungen](https://open-meteo.com/en/terms)), ein API-Key ist nicht erforderlich. Die Vorhersagen beruhen auf Modellen nationaler Wetterdienste wie DWD, NOAA, ECMWF und Météo-France (Einzelheiten: [open-meteo.com/en/docs](https://open-meteo.com/en/docs)).

**Pollen und Luftqualität:** Daten des **Copernicus Atmosphere Monitoring Service (CAMS)** des ECMWF, bereitgestellt über Open-Meteo. „Contains modified Copernicus Atmosphere Monitoring Service information.“ Weder die Europäische Kommission noch das ECMWF haften für die Verwendung der Daten.

---

## DWD-Daten

**Quelle:** [Deutscher Wetterdienst (DWD) – Opendata](https://opendata.dwd.de)

Wetterwarnungen, Pollenflug-Vorhersagen, Biowetter und Warnkarte stammen vom DWD (frei zugängliche Geodaten). Sie dürfen nach den
[Rechtlichen Hinweisen des DWD](https://www.dwd.de/DE/service/rechtliche_hinweise/rechtliche_hinweise_node.html) unter der Lizenz
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) mit Quellenvermerk weiterverwendet werden. Der Vermerk lautet
**„Quelle: Deutscher Wetterdienst“** und steht unmittelbar an den angezeigten DWD-Informationen; bei weitergehend bearbeiteten Daten
nennt der DWD Hinweise wie „Datenbasis: Deutscher Wetterdienst, …“ in zentralen Quellenverzeichnissen.

---

## Treiber

**Quelle:** Waveshare-Beispielcode für das ESP32-S3-Touch-LCD-7B (RGB-Panel, I²C, IO-Expander) sowie der GT911-Treiber auf Basis von Espressif `esp_lcd_touch`
(Apache License 2.0, © Espressif Systems (Shanghai) CO LTD). Siehe Dateikopf in `src/hardware/` und `src/board/s3/`. Die Pinbelegung und der Ablauf der
Display-Initialisierung des ESP32-P4-Boards (`src/board/p4/`) folgen den Beispielen von Waveshare (Komponente `waveshare/esp32_p4_wifi6_touch_lcd_7b`, Apache 2.0).

---

## Schrift

**Quelle:** [Montserrat](https://github.com/JulietaUla/Montserrat) – © The Montserrat Project Authors

**Lizenz:** [SIL Open Font License 1.1](https://openfontlicense.org)

Die Montserrat-Schrift ist in mehreren Größen als umgewandelte C-Dateien (`src/ui/fonts/`) im Projekt enthalten. Die OFL erlaubt das Einbetten in Software;
die Schrift selbst darf nicht einzeln verkauft werden.

---

## Bibliotheken und Framework

Diese Komponenten werden beim Bauen über den ESP-IDF-Komponentenmanager geladen und stecken kompiliert in den Firmware-Dateien.
Ihre Quellen liegen nicht in diesem Repository.

| Bibliothek | Lizenz |
|---|---|
| [LVGL](https://lvgl.io) 8.x (Light and Versatile Graphics Library), enthält [LodePNG](https://lodev.org/lodepng/) | MIT; LodePNG: zlib |
| [ArduinoJson](https://arduinojson.org) | MIT |
| [ESP-IDF](https://github.com/espressif/esp-idf) (einschließlich `esp-mqtt`, mbedTLS, lwIP, FreeRTOS) | Apache 2.0 (mbedTLS: Apache 2.0 oder GPL 2.0+) |
| [mDNS](https://components.espressif.com/components/espressif/mdns) (Espressif, `wettercube-fusion.local`) | Apache 2.0 |
| ESP32-P4-Board: [esp_lcd_ek79007](https://components.espressif.com/components/espressif/esp_lcd_ek79007) (Display-Controller), [esp_wifi_remote](https://components.espressif.com/components/espressif/esp_wifi_remote) und [esp_hosted](https://components.espressif.com/components/espressif/esp_hosted) (WLAN über den ESP32-C6) – Espressif | Apache 2.0 |

---

## Handy-Meldungen (optional)

**Dienst:** [ntfy](https://ntfy.sh) – Benachrichtigungen per HTTP an das Handy. Die Fusion sendet nur an den vom Nutzer eingetragenen Server und das
Thema (Standard `https://ntfy.sh`, auch selbst gehostet möglich). Es wird kein Code von ntfy mitgeliefert. Software und Dienst stehen unter Apache 2.0 bzw. GPLv2.

---

## Oberfläche, Logo und Grafiken

- UI-Design und Code-Export: [PicoPixel](https://picopixel.io)
- Logo `assets/wettercube-fusion-logo*.svg`, Roadmap `assets/roadmap.svg`, Teaser-Video: eigene Werke des Projekts

---

## Vorgänger

WetterCube Fusion baut auf dem [WetterCubePlus](https://github.com/JPPeterson-lab/WetterCubePlus) (ESP32-S3 N16R8, 3,5"-Display) auf;
Anfang der Familie ist der WetterCube (ESP32-C3).

---

## Projektlizenz

Der Quellcode dieses Projekts steht unter der **Apache License 2.0** – siehe [LICENSE](LICENSE) und [NOTICE](NOTICE). Texte, Bilder und Dokumentation stehen unter **CC BY-NC 4.0** ([LICENSE-Dokumentation.txt](LICENSE-Dokumentation.txt)).

Die oben genannten Drittanbieter-Ressourcen unterliegen ihren jeweiligen Lizenzen (Meteocons: MIT, Icons8: MIT oder Good Boy License, Montserrat: SIL OFL 1.1, Bibliotheken siehe oben) und sind von der Lizenz dieses Projekts nicht erfasst.
