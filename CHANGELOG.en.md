# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v0.9.9-beta13 (2026-10-23)

### Improved
- **Switching location:** weather and the 7-day forecast now appear immediately, the rest is loaded afterwards. Warnings, warning map, DWD pollen and Biowetter are only fetched again when the region or zone changes; with two places in the same region only the marker on the map moves. Before each request the device briefly waits until enough internal memory is free (otherwise requests failed with "HTTP -1" and the web interface hardly opened). The log lists every switch ("[Ort] ...").
- **Screenshot:** The button in the web interface reads the screen content straight from the display memory and needs no extra memory (before, it failed with "not enough memory" once the warning map was loaded).
