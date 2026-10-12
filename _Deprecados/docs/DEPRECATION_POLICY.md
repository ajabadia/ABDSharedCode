# Deprecation Policy

## Version 1.3.0 (2024)
- `convertSysExToCutoffHz(float)` marked deprecated
- Use `conversion::normToCutoffHz(entry, t)` instead
- Will be removed in v2.0.0 (Q1 2025)

## Version 2.0.0 (Q1 2025)
- `convertSysExToCutoffHz` removed
- All callers must use `normToCutoffHz`

### C++ (`[[deprecated]]`) Pattern
```cpp
[[deprecated("Use conversion::normToCutoffHz(entry, t) instead - will be removed in v2.0.0")]]
float convertSysExToCutoffHz(float sysexValue) {
    // Mantener funcionalidad para compatibilidad backward
    return conversion::normToCutoffHz(sysexValue, nullptr);
}
```

### JS Pattern
```javascript
// Deprecated - will be removed in v2.0.0
export function convertSysExToCutoffHz(sysexValue) {
    console.warn(
        'convertSysExToCutoffHz is deprecated. ',
        'Use conversion.normToCutoffHz() instead. '
        'Will be removed in v2.0.0 (Q1 2025).'
    );
    return conversion.normToCutoffHz(sysexValue);
}
```

### Policy Details
- **Deprecation cycle**: Marked in minor version (1.3.0), removed in next major (2.0.0)
- **Timeline**: 
  - v1.3.0: Function marked `[[deprecated]]` / `console.warn`
  - v1.4.0-v1.9.0: Function still works but emits warning
  - v2.0.0: Function removed, compiler/error if used
- **Alternative**: Always use `conversion::normToCutoffHz(entry, t)` from day one
- **Migration path**: 
  1. Add `[[deprecated]]` attribute and warning
  2. Update all callers to use new function
  3. Remove deprecated function in major version bump