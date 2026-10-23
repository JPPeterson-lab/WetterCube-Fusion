# Changelog

*[Deutsche Version](CHANGELOG.md)*

## v0.9.9-beta14 (2026-10-23)

### Changed
- **Second location only on the P4:** On the S3 the second location is switched off (no pill, no card in the web interface). The S3 has only 10 to 35 KB of internal memory free, and switching places fetches a lot of data in a row; requests failed and the web interface was hardly reachable. On the P4 (much more memory) the feature stays. If you want to try it on the S3 yourself, build with `-DFUSION_ZWEITER_STANDORT=1`.
- The location pill now sits at the top right and no longer covers the update icon.
