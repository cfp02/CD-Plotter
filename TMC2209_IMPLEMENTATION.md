# TMC2209 Driver Implementation Complete

## Summary

The TMC2209 driver has been successfully implemented following the modular architecture established in Phase 1. The implementation uses AccelStepper for motion control and TMCStepper library for UART configuration.

## Implementation Details

### 1. Pin Configuration (`src/hardware/pin_config_xiao_esp32s3.h`)
- **Stepper 1**: Step=GPIO1, Dir=GPIO2
- **Stepper 2**: Step=GPIO3, Dir=GPIO4
- **Enable**: GPIO5 (shared for both motors)
- **UART**: TX=GPIO43, RX=GPIO44 (shared UART bus)
- **Servo**: GPIO7
- **Endstops**: GPIO8, GPIO9 (planned, not yet wired)

### 2. TMC2209 Driver (`src/drivers/tmc2209_driver.h/cpp`)

**Architecture:**
- Uses `AccelStepper` in DRIVER mode (step/direction) for smooth motion control
- Uses `TMCStepper` library for UART communication and configuration
- Implements full `StepperDriver` interface

**Key Features:**
- **Motion Control**: AccelStepper handles acceleration/deceleration profiles
- **UART Configuration**: TMCStepper library configures:
  - Microstepping (1/16 as requested)
  - Current limits (running and holding)
  - StealthChop mode (silent operation)
  - PWM auto-scaling
- **Shared UART**: Both drivers use the same UART with different addresses (0 and 1)
- **Power Control**: Maps 0-255 power value to current percentage via UART

**Configuration:**
- **Microstepping**: 1/16 (default, configurable)
- **Running Current**: 800mA (default, adjustable)
- **Holding Current**: 400mA (50% of running, adjustable)
- **RSense**: 0.11Ω (typical for SilentStepStick)
- **UART Baud**: 115200

### 3. Main.cpp Updates

**Driver Instantiation:**
```cpp
#ifdef DRIVER_TMC2209
    HardwareSerial tmcUart(TMC_UART_NUM);
    TMC2209Driver stepper1_driver(MOTOR1_STEP, MOTOR1_DIR, MOTOR_EN,
                                   &tmcUart, TMC2209_ADDRESS_MOTOR1, TMC2209_RSENSE);
    TMC2209Driver stepper2_driver(MOTOR2_STEP, MOTOR2_DIR, MOTOR_EN,
                                   &tmcUart, TMC2209_ADDRESS_MOTOR2, TMC2209_RSENSE);
#endif
```

**UART Initialization:**
- Shared UART initialized once in `setup()` before driver `begin()` calls
- Both drivers share the same HardwareSerial instance
- Different UART addresses allow communication with both drivers

### 4. PlatformIO Configuration

**XIAO ESP32-S3 Environment:**
```ini
[env:seeed_xiao_esp32s3]
build_flags =
    -DBOARD_XIAO_ESP32S3
    -DDRIVER_TMC2209
lib_deps = 
    teemuatlut/TMCStepper@^0.7.3
    waspinator/AccelStepper@^1.64
    madhephaestus/ESP32Servo@^3.0.5
```

## Hardware Setup Requirements

### TMC2209 Wiring

**For Shared UART:**
1. **Motor 1 (Address 0)**:
   - PDN_UART pin: Leave unconnected or tie to GND (default address 0)
   - UART TX/RX: Connect to ESP32 UART pins

2. **Motor 2 (Address 1)**:
   - PDN_UART pin: Tie to 3.3V or VIO (address 1)
   - UART TX/RX: Connect to same ESP32 UART pins (shared bus)

**Step/Direction Pins:**
- Each motor has its own STEP and DIR pins
- Enable pin is shared (GPIO5)

**UART Connection:**
- ESP32 TX (GPIO43) → Both TMC2209 RX/PDN_UART pins (via shared bus)
- ESP32 RX (GPIO44) → Both TMC2209 TX pins (via shared bus)
- Use appropriate pull-up resistors if needed

## Testing Checklist

### Initial Testing
- [ ] Compile for `seeed_xiao_esp32s3` environment
- [ ] Verify UART communication with both TMC2209 drivers
- [ ] Check DRV_STATUS register reads correctly
- [ ] Verify microstepping is set to 1/16

### Motion Testing
- [ ] Basic step/direction control works
- [ ] Acceleration/deceleration profiles are smooth
- [ ] Speed control works correctly
- [ ] Direction control works correctly
- [ ] Enable/disable functions work

### Power Control
- [ ] Power adjustment via web interface works
- [ ] Current scales correctly with power setting
- [ ] Holding current reduces when stopped

### Integration Testing
- [ ] Web interface works with TMC2209
- [ ] G-code commands work correctly
- [ ] Pen servo control works
- [ ] All settings persist across restarts

## Configuration Notes

### Adjusting Current Limits

Edit `src/hardware/pin_config_xiao_esp32s3.h`:
```cpp
#define TMC2209_CURRENT_RUN_MA 800   // Adjust based on your motor
#define TMC2209_CURRENT_HOLD_MA 400  // Typically 50% of running
```

### Changing Microstepping

The default is 1/16. To change, edit the constant:
```cpp
#define TMC2209_MICROSTEPS 16  // Options: 1, 2, 4, 8, 16, 32, 64, 128, 256
```

Or call `setMicrostepping()` on the driver instance (requires code modification).

### UART Address Configuration

If your TMC2209 drivers have different address configurations:
- Edit `TMC2209_ADDRESS_MOTOR1` and `TMC2209_ADDRESS_MOTOR2` in pin config
- Address 0: PDN_UART low or unconnected
- Address 1: PDN_UART high
- Address 2: PDN_UART with different configuration (if supported)

## Troubleshooting

### UART Communication Issues
- Verify UART pins are correct (GPIO43/44)
- Check baud rate matches (115200)
- Ensure PDN_UART pins are configured correctly for addresses
- Check for proper pull-up resistors on UART lines

### Motor Not Moving
- Verify enable pin is LOW (enabled)
- Check step/direction pins are connected
- Verify current is set appropriately
- Check TMC2209 power supply

### Noisy Operation
- Enable StealthChop mode (default)
- Adjust current limits
- Check microstepping setting
- Verify motor wiring

## Next Steps

1. **Hardware Testing**: Wire up the TMC2209 drivers and test basic functionality
2. **Calibration**: Adjust current limits based on motor specifications
3. **Fine-tuning**: Optimize acceleration profiles and speeds
4. **Endstop Integration**: When ready, add endstop support using GPIO8/9

## Code Quality

- ✅ Follows established interface pattern
- ✅ Uses AccelStepper for proven motion control
- ✅ Proper error handling and diagnostics
- ✅ Well-documented with comments
- ✅ Consistent with TB6612 driver structure
- ✅ No linter errors

