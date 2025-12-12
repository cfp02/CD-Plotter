#ifndef TMC2209_DRIVER_H
#define TMC2209_DRIVER_H

#include "stepper_driver.h"
#include <AccelStepper.h>
#include <HardwareSerial.h>

// Forward declaration - TMCStepper library types
// We'll include the actual library in the .cpp file to avoid dependency issues
class TMC2209Stepper;

/**
 * TMC2209 Driver Implementation
 * 
 * Implements StepperDriver interface for TMC2209 stepper drivers.
 * Uses AccelStepper for motion control (step/direction) and TMCStepper
 * library for UART configuration (microstepping, current, etc.).
 * 
 * Features:
 * - AccelStepper for smooth acceleration/deceleration
 * - TMCStepper for UART configuration
 * - Shared UART bus support (multiple drivers on same UART)
 * - Current control via UART
 * - Microstepping configuration via UART
 */
class TMC2209Driver : public StepperDriver {
private:
    // AccelStepper for motion control (step/direction mode)
    AccelStepper stepper;
    
    // Step and direction pins (used by AccelStepper)
    int stepPin;
    int dirPin;
    int enablePin;
    
    // UART communication for TMC2209 configuration
    HardwareSerial* uartSerial;
    int uartAddress;  // TMC2209 UART address (0 or 1 for shared bus)
    TMC2209Stepper* tmcDriver;
    
    // Power/current control
    int currentPower;  // 0-255, mapped to current percentage
    bool enabled;
    
    // TMC2209 configuration
    float rsense;  // Sense resistor value in ohms
    
public:
    /**
     * Constructor
     * @param stepPin GPIO pin for step signal
     * @param dirPin GPIO pin for direction signal
     * @param enablePin GPIO pin for enable signal (shared for both motors)
     * @param uartSerial HardwareSerial pointer for UART communication
     * @param uartAddress TMC2209 UART address (0 or 1)
     * @param rsense Sense resistor value in ohms (typically 0.11)
     */
    TMC2209Driver(int stepPin, int dirPin, int enablePin,
                   HardwareSerial* uartSerial, int uartAddress, float rsense);
    
    ~TMC2209Driver();
    
    // StepperDriver interface implementation
    void setMaxSpeed(float speed) override;
    void setAcceleration(float acceleration) override;
    void move(long relativeSteps) override;
    void moveTo(long absoluteSteps) override;
    bool run() override;  // Non-blocking step execution
    void stop() override;
    
    long currentPosition() const override;
    long targetPosition() const override;
    bool isRunning() const override;
    float speed() const override;
    
    void setCurrentPosition(long position) override;
    
    void setPower(int power) override;  // 0-255 maps to current percentage
    void enable() override;
    void disable() override;
    
    void begin() override;  // Initialize UART, configure TMC2209
    
    // TMC2209-specific methods
    void setMicrostepping(int microsteps);  // 1, 2, 4, 8, 16, 32, 64, 128, 256
    void setCurrent(int runCurrent_mA, int holdCurrent_mA);
    void setStealthChop(bool enable);  // Silent operation mode
    void setSpreadCycle(bool enable);  // High torque mode
};

#endif // TMC2209_DRIVER_H

