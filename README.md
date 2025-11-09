# Bird Blinds Controller

Automated window blinds controller for bird observation using ESP32 and TMC2209 stepper motor driver.

## Overview

This project controls horizontal window blinds using a stepper motor with automatic calibration via limit switches. The system can deploy and retract the blinds to specific positions, making it ideal for bird observation setups where you need precise, quiet control.

## Hardware Requirements

### Components

- **ESP32 Development Board** (esp32dev)
- **TMC2209 Stepper Motor Driver** (standalone mode)
- **NEMA 17 Stepper Motor** (or similar)
- **2x Limit Switches** (for calibration endpoints)
- **Power Supply** (12-24V for motor, depending on your motor specs)

### Pin Connections

#### TMC2209 Driver

| TMC2209 Pin | ESP32 Pin | Description         |
| ----------- | --------- | ------------------- |
| EN          | GPIO 25   | Enable (active LOW) |
| STEP        | GPIO 26   | Step pulse          |
| DIR         | GPIO 27   | Direction control   |
| GND         | GND       | Ground              |
| VDD         | 3.3V      | Logic power         |
| VM          | 12-24V    | Motor power         |

#### Limit Switches

| Switch    | ESP32 Pin | Description              |
| --------- | --------- | ------------------------ |
| Retracted | GPIO 32   | Fully retracted position |
| Deployed  | GPIO 33   | Fully deployed position  |

**Note:** Limit switches use internal pull-up resistors. Connect switches between GPIO pin and GND.

## TMC2209 Configuration

The TMC2209 operates in **standalone mode** (STEP/DIR control without UART). Configure the driver settings using:

- **Current Limiting:** Adjust VREF potentiometer or resistor on your driver board
- **Microstepping:** Set via MS1/MS2 pins (check your driver board documentation)
- **StealthChop vs SpreadCycle:** Configure via SPREAD pin if available

### Recommended Settings

- Current: 60-80% of motor rated current
- Microstepping: 16x (typical default)
- Mode: StealthChop for quiet operation

## Software Setup

### PlatformIO

This project uses PlatformIO. The configuration is in `platformio.ini`.

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Upload and monitor serial output
pio run --target upload --target monitor
```

### Dependencies

- Arduino Framework for ESP32
- No external libraries required (standalone STEP/DIR control)

## Usage

### Initial Setup

1. **Power on** the system
2. **Automatic calibration** runs on first boot:
   - Motor moves to retracted limit switch
   - Motor moves to deployed limit switch
   - Range is calculated and stored
   - Returns to retracted position

### Serial Commands

Connect via serial monitor at **115200 baud**:

| Command    | Action                                        |
| ---------- | --------------------------------------------- |
| `d` or `D` | Deploy blinds (move to deployed position)     |
| `r` or `R` | Retract blinds (move to retracted position)   |
| `c` or `C` | Recalibrate (find limits again)               |
| `s` or `S` | Show status (position, limits, switch states) |
| `t` or `T` | Test motor (move 100 steps)                   |

### Example Serial Output

```
Bird Blinds Controller Started
TMC2209 in standalone mode (STEP/DIR control)

=== Limit Switch Diagnostics ===
LIMIT_RETRACTED (pin 32) state: NOT TRIGGERED (HIGH)
LIMIT_DEPLOYED (pin 33) state: NOT TRIGGERED (HIGH)
================================

Performing calibration...
Starting calibration sequence...
Moving to retracted position...
Retracted limit found
Moving to deployed position...
Deployed limit found
Calibration complete. Range: 0 to 12500 steps
Already at target position
Calibration complete!
Commands: 'd' = deploy, 'r' = retract, 'c' = calibrate
```

## Troubleshooting

### Motor doesn't move

- Check EN pin is LOW (driver enabled)
- Verify motor power supply is connected
- Adjust VREF/current setting on TMC2209
- Use `t` command to test basic movement

### Calibration shows "Range: 0 to 0 steps"

- Both limit switches are triggered immediately
- Check switch wiring (should be normally-open)
- Verify switches are connected between GPIO and GND
- Use `s` command to check switch states
- If switches show TRIGGERED when not pressed, they may be normally-closed

### Motor moves but not smoothly

- Adjust `SPEED_DELAY` in code (lower = faster, higher = slower)
- Check motor current setting (VREF)
- Verify power supply can handle motor current

### Limit switches not working

- Test with `s` command to see real-time switch states
- Verify INPUT_PULLUP is enabled (it is by default)
- Check for loose connections
- Ensure switches are rated for 3.3V logic

## Customization

### Speed Adjustment

Edit `SPEED_DELAY` in `main.cpp`:

```cpp
#define SPEED_DELAY 500  // Microseconds between steps
                         // Lower = faster, Higher = slower
```

### Pin Changes

Modify pin definitions at the top of `main.cpp`:

```cpp
#define EN_PIN 25
#define STEP_PIN 26
#define DIR_PIN 27
#define LIMIT_RETRACTED 32
#define LIMIT_DEPLOYED 33
```

### Limit Switch Logic

If using normally-closed switches, invert the logic:

```cpp
bool isRetractedLimitHit()
{
  return digitalRead(LIMIT_RETRACTED) == HIGH; // Changed from LOW
}

bool isDeployedLimitHit()
{
  return digitalRead(LIMIT_DEPLOYED) == HIGH; // Changed from LOW
}
```

## Code Structure

```
src/main.cpp
├── setup()              - Initialize hardware and calibrate
├── loop()               - Process serial commands
├── calibrate()          - Find both limit switches and set range
├── deploy()             - Move to deployed position
├── retract()            - Move to retracted position
├── moveToPosition()     - Move to specific position with limit checking
├── moveSteps()          - Low-level step control
├── isRetractedLimitHit() - Check retracted limit switch
└── isDeployedLimitHit()  - Check deployed limit switch
```

## Safety Features

- **Limit switch monitoring** during all movements
- **Position tracking** prevents over-travel
- **Calibration validation** before deployment/retraction
- **Timeout protection** during calibration (50,000 steps max)

## Future Enhancements

Potential improvements:

- [ ] Web interface for remote control
- [ ] Scheduled deployment/retraction
- [ ] Light sensor integration
- [ ] Partial deployment positions (25%, 50%, 75%)
- [ ] EEPROM storage of calibration data
- [ ] Acceleration/deceleration profiles
- [ ] WiFi/MQTT control

## License

Open source - use freely for your bird observation projects!

## Contributing

Contributions welcome! Please test thoroughly before submitting pull requests.

## Support

For issues or questions:

1. Check the Troubleshooting section
2. Use `s` command to gather diagnostic information
3. Check serial output for error messages
4. Verify hardware connections match pin definitions
