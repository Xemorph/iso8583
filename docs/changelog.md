# Changelog

## 0.1.1-alpha

- Initial public release.
- YAML preprocessor: `!use`, `!template`, `!merge`, `!include_files`.
- Thread-safe `ISOMessage` with `std::shared_mutex`.
- `ISOUtils`: `getOrThrow`, `getOrDefault`, `ifPresent`, `flatten`.
- Dot-notation `set("48.72.1", value)` for arbitrary nesting depth.
- `QuillBridge` header-only logger for DLL-safe Quill integration.
- Wire-position tracking: `wire_offset()` / `wire_length()` on every field.
