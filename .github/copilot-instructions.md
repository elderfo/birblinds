# GitHub Copilot Instructions - Bird Blinds Controller

## Project Context

This is an ESP32-based stepper motor controller for automated window blinds used in bird observation. The system uses a TMC2209 stepper driver in standalone mode (STEP/DIR control, no UART) with limit switch calibration.

## Architecture

- **Platform:** ESP32 (espressif32)
- **Framework:** Arduino
- **Build System:** PlatformIO
- **Motor Driver:** TMC2209 in standalone mode
- **Control Method:** STEP/DIR pulses (not UART communication)

## Code Guidelines

### Pin Definitions

- Always use defined constants for pins (EN_PIN, STEP_PIN, DIR_PIN, etc.)
- Limit switches on GPIO 32 (retracted) and GPIO 33 (deployed)
- All limit switches use INPUT_PULLUP mode

### Motor Control

- Use `moveSteps()` for low-level stepping with limit checking
- Use `moveToPosition()` for position-based movements
- Always check `isCalibrated` flag before position-based movements
- STEP_DELAY controls speed (microseconds between pulses)

### Calibration System

- Calibration finds both endpoints using limit switches
- Position 0 is always the retracted position
- Position tracking prevents over-travel
- Limit switches are checked during all movements

### Safety Patterns

```cpp
// Always check calibration before position moves
if (!isCalibrated) {
  Serial.println("Error: Not calibrated");
  return;
}

// Always check limits during movement
if (checkLimits) {
  if (forward && isDeployedLimitHit()) {
    // Stop and update position
    return;
  }
}
```

### Serial Communication

- Baud rate: 115200
- Single character commands (case-insensitive)
- Always provide user feedback for commands
- Use diagnostic output to help troubleshooting

## Common Patterns

### Adding New Movement Commands

```cpp
case 'x':
case 'X':
  Serial.println("Starting action...");
  // Perform action
  Serial.println("Action complete");
  break;
```

### Stepping Pattern

```cpp
digitalWrite(DIR_PIN, direction ? HIGH : LOW);
delayMicroseconds(10); // Direction setup time

for (int i = 0; i < steps; i++) {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(SPEED_DELAY);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(SPEED_DELAY);
}
```

### Limit Switch Checking

```cpp
// Normally-open switches (active LOW)
bool isRetractedLimitHit() {
  return digitalRead(LIMIT_RETRACTED) == LOW;
}

// If switches are normally-closed, use == HIGH instead
```

## Important Constraints

### What NOT to do

- ❌ Don't include TMCStepper library (we use standalone mode)
- ❌ Don't try UART communication with TMC2209
- ❌ Don't use blocking delays in loop() (affects command responsiveness)
- ❌ Don't move motor without checking isCalibrated flag
- ❌ Don't modify position tracking without updating currentPosition

### What TO do

- ✅ Use standalone STEP/DIR control
- ✅ Always check limit switches during movement
- ✅ Provide serial feedback for all operations
- ✅ Use non-blocking code patterns where possible
- ✅ Add diagnostic commands for troubleshooting
- ✅ Keep position tracking accurate

## Testing Patterns

### When adding new features:

1. Add a test command (single character)
2. Provide clear serial output
3. Check limit switches if movement involved
4. Update position tracking if position changes
5. Add documentation in comments

### Example test command:

```cpp
case 't':
case 'T':
  Serial.println("Test: Description of test");
  // Test code here
  Serial.println("Test complete. Result: ...");
  break;
```

## Hardware Assumptions

- TMC2209 is configured via hardware (DIP switches/jumpers)
- Limit switches are normally-open, active-low with pull-ups
- EN pin is active-low (LOW = enabled)
- Motor direction: HIGH = deploy, LOW = retract

## Debugging Helpers

When user reports issues, suggest:

1. Use `s` command to check status
2. Use `t` command to test basic motor function
3. Check startup diagnostics for limit switch states
4. Verify calibration range is reasonable (not 0 to 0)

## Variable Naming

- Position variables: `long` type (supports large step counts)
- Boolean flags: `is` prefix (isCalibrated, isRetractedLimitHit)
- Pin definitions: UPPER_CASE with \_PIN suffix
- Functions: camelCase verbs (moveSteps, calibrate)

## Error Handling

Always validate before action:

```cpp
if (!isCalibrated) {
  Serial.println("Error: Not calibrated. Run calibration first.");
  return;
}

if (steps == 0) {
  Serial.println("Already at target position");
  return;
}
```

## Performance Considerations

- `delayMicroseconds()` is acceptable for step timing
- Avoid `delay()` in loop() - use non-blocking patterns
- Serial output adds time - use sparingly in tight loops
- Limit switch checks add minimal overhead

## Future Enhancement Areas

When suggesting improvements, consider:

- Web interface (ESP32 WiFi capabilities)
- EEPROM for calibration persistence
- Acceleration profiles for smoother movement
- Partial positions (percentage-based)
- Scheduled operations
- Light sensor integration
- MQTT/Home Assistant integration

## Code Style

- Use descriptive variable names
- Comment complex logic
- Keep functions focused and single-purpose
- Use constants instead of magic numbers
- Indent with 2 spaces (Arduino standard)
- Add blank lines between logical sections
