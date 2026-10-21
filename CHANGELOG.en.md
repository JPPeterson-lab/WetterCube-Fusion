# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v0.9.9-alpha18 (2026-10-21)

### Fixed
- **Outdoor history:** Since alpha17 the fetch of the last days from Open-Meteo was always postponed when memory was low and never ran. It now needs hardly any internal memory (the response is split while it is received instead of being held as a whole) and runs even then.
