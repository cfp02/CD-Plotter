# Modularization Plan: Multi-Driver Support for ESP32 Plotter

## Executive Summary

This document outlines the plan to refactor the ESP32 plotter codebase to support multiple hardware configurations:
- **Configuration 1**: ESP32 DevKit + TB6612FNG drivers (existing)
- **Configuration 2**: ESP32-S3 XIAO + TMC2209 drivers (new)

The goal is to create a modular, maintainable architecture where driver-specific code is isolated and selected via PlatformIO build environments.

---

## Current Architecture Analysis

### Hardware-Specific Code Locations

1. **Pin Definitions** (`main.cpp` lines 23-60):
   - TB6612FNG pin assignments (MOTOR1_AIN1, MOTOR1_PWMA, etc.)
   - PWM channel assignments
   - Servo pin definition

2. **Stepper Objects** (`main.cpp` lines 90-99):
   - Direct `AccelStepper` instantiation with pin numbers
   - Step mode configuration

3. **Power Management** (`main.cpp` lines 107-117):
   - `setMotorPower()` function using LEDC PWM
   - PWM channel setup in `setup()`

4. **Motor Settings** (`main.cpp` lines 119-125):
   - `updateMotorSettings()` directly calls `stepper1.setMaxSpeed()`

5. **Motion Controller** (`motion_controller.h/cpp`):
   - Takes `AccelStepper*` pointers
   - Direct calls to `stepperX->move()`, `stepperX->run()`, etc.

### Shared/Generic Code (Should Remain Unchanged)

- G-code interpreter (`gcode_parser`, `command_queue`, `serial_interface`)
- Plotter state machine (`plotter_state`)
- Web server and HTML generation
- WiFi configuration
- Preferences/storage
- Pen servo control (ESP32Servo is generic)
- Coordinate system and work area management

---

## Proposed Architecture

### Design Principles

1. **Interface Abstraction**: Create a common interface for stepper control
2. **Driver Isolation**: Each driver type gets its own implementation
3. **Environment-Based Selection**: PlatformIO environments select the driver
4. **Minimal Code Duplication**: Shared logic stays in common modules
5. **Backward Compatibility**: Existing TB6612FNG code continues to work

### Directory Structure

```
src/
├── main.cpp                    # Main entry point (minimal, driver-agnostic)
├── config.h                    # Shared configuration constants
├── drivers/                    # NEW: Driver implementations
│   ├── stepper_driver.h        # Abstract base class/interface
│   ├── tb6612_driver.h
│   ├── tb6612_driver.cpp
│   ├── tmc2209_driver.h
│   └── tmc2209_driver.cpp
├── hardware/                   # NEW: Hardware configuration
│   ├── pin_config.h            # Pin definitions per board
│   └── board_config.h          # Board-specific settings
├── motion_controller.h/cpp     # MODIFIED: Use driver interface
├── gcode_parser.h/cpp          # UNCHANGED
├── command_queue.h/cpp         # UNCHANGED
├── plotter_state.h/cpp         # UNCHANGED
├── serial_interface.h/cpp      # UNCHANGED
└── wifi_config.h               # UNCHANGED
```

---

## Implementation Details

### 1. Stepper Driver Interface (`drivers/stepper_driver.h`)

```cpp
// Abstract interface for stepper motor drivers
class StepperDriver {
public:
    virtual ~StepperDriver() = default;
    
    // Core motion control (required by AccelStepper or equivalent)
    virtual void setMaxSpeed(float speed) = 0;
    virtual void setAcceleration(float acceleration) = 0;
    virtual void move(long relativeSteps) = 0;
    virtual void moveTo(long absoluteSteps) = 0;
    virtual void run() = 0;  // Non-blocking step execution
    virtual void stop() = 0;
    
    // Status queries
    virtual long currentPosition() const = 0;
    virtual long targetPosition() const = 0;
    virtual bool isRunning() const = 0;
    virtual float speed() const = 0;
    
    // Position management
    virtual void setCurrentPosition(long position) = 0;
    
    // Power/current control (driver-specific)
    virtual void setPower(int power) = 0;  // 0-255 or driver-specific range
    virtual void enable() = 0;
    virtual void disable() = 0;
    
    // Initialization
    virtual void begin() = 0;  // Setup pins, PWM, UART, etc.
};
```

**Key Design Decisions:**
- Interface matches `AccelStepper` API for TB6612FNG compatibility
- `setPower()` is abstracted (PWM for TB6612, current for TMC2209)
- `begin()` handles all hardware-specific initialization

### 2. TB6612FNG Driver Implementation (`drivers/tb6612_driver.h/cpp`)

**Wraps AccelStepper** - minimal wrapper that:
- Creates internal `AccelStepper` object
- Implements `StepperDriver` interface by delegating to `AccelStepper`
- Handles PWM power control via LEDC
- Manages pin configuration

**Key Features:**
- Uses existing `AccelStepper` library (no changes needed)
- PWM channels 4-7 (avoids servo conflict)
- Step mode: FULL4WIRE or HALF4WIRE
- Power control: 0-255 PWM duty cycle

### 3. TMC2209 Driver Implementation (`drivers/tmc2209_driver.h/cpp`)

**Uses TMCStepper library** - implements:
- Step/direction pin control
- UART communication for configuration
- Microstepping control (via UART)
- Current/current-hold control (via UART)
- SilentStepStick features (stealthChop, spreadCycle)

**Key Features:**
- Requires TMCStepper library (e.g., `teemuatlut/TMCStepper@^0.7.3`)
- UART pins for configuration (different per board)
- Step/direction pins (different per board)
- Current control via UART registers (not PWM)
- Microstepping: 1/256 max (configurable)

**TMC2209 Configuration:**
- UART address (typically 0 for single driver)
- RSense value (typically 0.11 ohms)
- Current settings (run, hold)
- Microstepping mode
- StealthChop vs SpreadCycle
- StallGuard (optional, for homing)

### 4. Hardware Configuration (`hardware/pin_config.h`)

**Environment-based pin definitions:**

```cpp
// Selected via build flags in platformio.ini
#ifdef BOARD_ESP32_DEVKIT
    // TB6612FNG pins for ESP32 DevKit
    #define MOTOR1_AIN1 14
    #define MOTOR1_AIN2 12
    // ... etc
#elif defined(BOARD_XIAO_ESP32S3)
    // TMC2209 pins for XIAO ESP32-S3
    #define MOTOR1_STEP 1
    #define MOTOR1_DIR 2
    #define MOTOR1_UART_TX 3
    #define MOTOR1_UART_RX 4
    // ... etc
#endif
```

**Alternative Approach (Recommended):**
- Create separate header files: `pin_config_esp32dev.h` and `pin_config_xiao_esp32s3.h`
- Include the appropriate one in `main.cpp` based on build flags
- More maintainable and clearer

### 5. Modified Motion Controller

**Changes:**
- Replace `AccelStepper*` with `StepperDriver*`
- All calls remain the same (interface matches)
- No logic changes needed

```cpp
class MotionController {
private:
    StepperDriver* stepperX;  // Changed from AccelStepper*
    StepperDriver* stepperY;  // Changed from AccelStepper*
    // ... rest unchanged
};
```

### 6. Modified Main.cpp

**Structure:**
```cpp
#include "drivers/stepper_driver.h"
#include "hardware/pin_config.h"  // Includes appropriate config

#ifdef DRIVER_TB6612
    #include "drivers/tb6612_driver.h"
    TB6612Driver stepper1_driver(...);
    TB6612Driver stepper2_driver(...);
#elif defined(DRIVER_TMC2209)
    #include "drivers/tmc2209_driver.h"
    TMC2209Driver stepper1_driver(...);
    TMC2209Driver stepper2_driver(...);
#endif

StepperDriver* stepper1 = &stepper1_driver;
StepperDriver* stepper2 = &stepper2_driver;

MotionController motionController(stepper1, stepper2, &penServo);
```

**Initialization:**
- Driver-specific setup in `setup()`:
  - TB6612: PWM channel setup, AccelStepper init
  - TMC2209: UART setup, TMC2209 register configuration

---

## PlatformIO Configuration

### `platformio.ini` Structure

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

build_flags =
    -DBOARD_ESP32_DEVKIT
    -DDRIVER_TB6612

lib_deps = 
    waspinator/AccelStepper@^1.64
    madhephaestus/ESP32Servo@^3.0.5

[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
upload_speed = 921600

build_flags =
    -DBOARD_XIAO_ESP32S3
    -DDRIVER_TMC2209

lib_deps = 
    teemuatlut/TMCStepper@^0.7.3
    madhephaestus/ESP32Servo@^3.0.5
    ; Note: May need AccelStepper or custom motion control
    ; TMC2209 can work with AccelStepper if we wrap it properly
```

**Key Points:**
- Build flags select driver and board
- Different libraries per environment
- Same framework (Arduino) for both

---

## Migration Strategy

### Phase 1: Create Interface and Wrapper (Non-Breaking)

1. Create `drivers/stepper_driver.h` interface
2. Create `drivers/tb6612_driver.h/cpp` wrapper around AccelStepper
3. Modify `motion_controller.h/cpp` to use `StepperDriver*`
4. Update `main.cpp` to use TB6612Driver wrapper
5. **Test**: Ensure existing functionality works unchanged

### Phase 2: Extract Pin Configuration

1. Create `hardware/pin_config_esp32dev.h` with current pins
2. Move pin definitions from `main.cpp` to config file
3. Update `main.cpp` to include config
4. **Test**: Verify pin assignments still work

### Phase 3: Add TMC2209 Support

1. Create `hardware/pin_config_xiao_esp32s3.h` (placeholder pins)
2. Create `drivers/tmc2209_driver.h/cpp` implementation
3. Add TMC2209 environment to `platformio.ini`
4. **Test**: Compile for XIAO (hardware testing when pins confirmed)

### Phase 4: Refinement

1. Add driver-specific web interface options (if needed)
2. Optimize TMC2209 configuration
3. Add microstepping control to web UI
4. Document pin assignments

---

## Key Design Questions & Decisions

### Q1: Should TMC2209 use AccelStepper or custom motion control?

**Decision: Custom wrapper recommended**
- TMC2209 can work with AccelStepper if we provide step/direction pins
- However, TMC2209's microstepping and current control are UART-based
- Better to create a wrapper that implements `StepperDriver` interface
- Can still use AccelStepper's motion planning internally if desired

**Alternative:** Use TMCStepper's built-in motion control (if available)

### Q2: How to handle microstepping?

**Decision: Abstract in interface, configure per driver**
- TB6612: Hardware step mode (FULL/HALF) via AccelStepper
- TMC2209: UART-configured microstepping (1/256 max)
- Web UI: Show appropriate options per driver
- Motion controller: Works in steps (driver handles microstepping internally)

### Q3: Power/Current Control Differences?

**Decision: Unified `setPower()` interface**
- TB6612: PWM duty cycle (0-255)
- TMC2209: Current setting via UART (0-100% or mA)
- Interface accepts 0-255, driver converts internally
- Or: Separate `setCurrent()` for TMC2209, `setPower()` for TB6612

**Recommended:** Keep `setPower(0-255)` and let drivers interpret:
- TB6612: Direct PWM mapping
- TMC2209: Map to current percentage or mA

### Q4: UART Configuration for TMC2209?

**Decision: Driver handles all UART setup**
- TMC2209Driver constructor takes UART pins
- `begin()` configures UART, sets up TMC2209 registers
- Default settings: 1/16 microstepping, reasonable current
- Advanced settings: Expose via web UI or serial commands

### Q5: Library Dependencies?

**Decision: Environment-specific**
- TB6612: AccelStepper (existing)
- TMC2209: TMCStepper library
- Both: ESP32Servo (shared)
- PlatformIO handles per-environment libs

---

## Implementation Checklist

### Core Interface
- [ ] Create `drivers/stepper_driver.h` abstract interface
- [ ] Define all required virtual methods
- [ ] Document interface contract

### TB6612FNG Driver
- [ ] Create `drivers/tb6612_driver.h/cpp`
- [ ] Wrap AccelStepper in StepperDriver interface
- [ ] Implement PWM power control
- [ ] Handle step mode configuration
- [ ] Test with existing hardware

### Hardware Configuration
- [ ] Create `hardware/pin_config_esp32dev.h`
- [ ] Move all TB6612 pin definitions
- [ ] Create `hardware/pin_config_xiao_esp32s3.h` (placeholder)
- [ ] Add build flag logic for selection

### Motion Controller Updates
- [ ] Change `AccelStepper*` to `StepperDriver*`
- [ ] Update all method calls (should be identical)
- [ ] Test motion control unchanged

### Main.cpp Refactoring
- [ ] Remove hardcoded pin definitions
- [ ] Include appropriate pin config
- [ ] Create driver instances based on build flags
- [ ] Update initialization code
- [ ] Test web interface still works

### TMC2209 Driver (Future)
- [ ] Research TMCStepper library API
- [ ] Create `drivers/tmc2209_driver.h/cpp`
- [ ] Implement UART communication
- [ ] Configure microstepping
- [ ] Implement current control
- [ ] Test with hardware (when pins confirmed)

### PlatformIO Configuration
- [ ] Update `esp32dev` environment with build flags
- [ ] Complete `seeed_xiao_esp32s3` environment
- [ ] Add appropriate library dependencies
- [ ] Test compilation for both environments

### Documentation
- [ ] Update README with multi-driver support
- [ ] Document pin assignments per board
- [ ] Create wiring diagrams
- [ ] Document TMC2209 configuration options

---

## Testing Strategy

### Unit Testing (Manual)
1. **TB6612FNG**: Verify all existing functionality works
   - Web interface
   - G-code commands
   - Stepper movement
   - Power control
   - Pen servo

2. **TMC2209**: When hardware available
   - UART communication
   - Step/direction control
   - Microstepping
   - Current control
   - Motion smoothness

### Integration Testing
- Verify motion controller works with both drivers
- Test coordinate system consistency
- Verify G-code commands work identically
- Test web interface adapts to driver (if needed)

---

## Potential Challenges & Solutions

### Challenge 1: AccelStepper Dependency
**Issue**: TMC2209 might not need AccelStepper if using TMCStepper's motion control
**Solution**: Create wrapper that implements StepperDriver, uses TMCStepper internally, or wraps AccelStepper with TMC2209 step pins

### Challenge 2: Different Step Modes
**Issue**: TB6612 uses hardware step mode, TMC2209 uses software microstepping
**Solution**: Abstract in interface - driver handles internally, motion controller works in "steps" (driver converts)

### Challenge 3: Power vs Current Control
**Issue**: Different control mechanisms
**Solution**: Unified interface with driver-specific implementation

### Challenge 4: Pin Configuration Complexity
**Issue**: Many pins, different per board
**Solution**: Separate config files, clear documentation, build-time selection

---

## Future Enhancements

1. **Additional Drivers**: Easy to add (DRV8825, A4988, etc.) by implementing `StepperDriver`
2. **Advanced TMC Features**: StallGuard for homing, CoolStep for efficiency
3. **Driver-Specific Web UI**: Show microstepping options for TMC2209
4. **Runtime Driver Selection**: Could add serial command to switch (advanced)

---

## Questions for User

1. **TMC2209 Pin Assignments**: What pins will be used on XIAO ESP32-S3?
   - Step pins (2)
   - Direction pins (2)
   - UART TX/RX pins (2, or shared UART?)
   - Enable pins (optional, 2)

2. **Microstepping Preference**: Default microstepping for TMC2209?
   - 1/16 (common default)
   - 1/256 (maximum smoothness)
   - Configurable via web UI?

3. **Current Settings**: Default run/hold current for TMC2209?
   - Depends on motor specs
   - Should be configurable

4. **UART Configuration**: 
   - Single UART shared between two TMC2209s (daisy-chain)?
   - Separate UARTs?
   - UART address configuration?

5. **AccelStepper for TMC2209**: 
   - Use AccelStepper with TMC2209 step/dir pins?
   - Or use TMCStepper's motion control?

---

## Conclusion

This modularization plan provides a clean separation of concerns while maintaining backward compatibility. The interface-based design allows easy addition of new drivers in the future. The PlatformIO environment-based selection keeps build configurations simple and clear.

**Next Steps:**
1. Review and approve this plan
2. Provide TMC2209 pin assignments
3. Begin Phase 1 implementation (interface + TB6612 wrapper)
4. Test with existing hardware
5. Implement TMC2209 driver when pins confirmed

