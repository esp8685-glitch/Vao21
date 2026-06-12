# Changelog

## [v3] - 2026-05-22

### Added
- **Smoke Detector Address Tracking**: Automatically reads and stores detector addresses from USB serial data
- **Detector Manager Module**: New `detector_manager.h/cpp` for managing detector inventory
- **Persistent SD Card Storage**: Detector list saved to `/detectors.txt` on SD card
- **Numeric ID Support**: Recognizes detector addresses in format `X.XXX` (e.g., 3.031, 3.036, 10.0042)
- **Duplicate Prevention**: Automatic deduplication using in-memory cache
- **Discovery Logging**: New detectors are logged with `"Discovered detector: X.XXX"`

### Modified
- `usb_task.cpp`: Enhanced to extract numeric detector IDs from incoming serial data
- `vao22.ino`: Added detector manager initialization during startup

### Technical Details
- Uses `std::set<String>` for fast in-memory caching
- Thread-safe SD card operations with mutex protection
- Detector list persists across reboots
- Non-blocking initialization (continues if detector storage fails)

---

## [v2] - 2026-05-22
- without detector list

## [v1] - 2026-05-22
- Initial release
