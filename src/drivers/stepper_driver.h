#ifndef STEPPER_DRIVER_H
#define STEPPER_DRIVER_H

#include <Arduino.h>

/**
 * Abstract interface for stepper motor drivers
 * 
 * This interface provides a common API for different stepper driver types
 * (TB6612FNG, TMC2209, etc.), allowing the motion controller to work with
 * any driver implementation.
 */
class StepperDriver {
public:
    virtual ~StepperDriver() = default;
    
    // ====================================================================
    // Core Motion Control (required by motion controller)
    // ====================================================================
    
    /**
     * Set maximum speed in steps per second
     */
    virtual void setMaxSpeed(float speed) = 0;
    
    /**
     * Set acceleration in steps per second squared
     */
    virtual void setAcceleration(float acceleration) = 0;
    
    /**
     * Move relative to current position
     * @param relativeSteps Number of steps to move (positive or negative)
     */
    virtual void move(long relativeSteps) = 0;
    
    /**
     * Move to absolute position
     * @param absoluteSteps Target position in steps
     */
    virtual void moveTo(long absoluteSteps) = 0;
    
    /**
     * Execute one step (non-blocking, call frequently in loop)
     * @return true if still moving, false if target reached
     */
    virtual bool run() = 0;
    
    /**
     * Stop immediately (emergency stop)
     */
    virtual void stop() = 0;
    
    // ====================================================================
    // Status Queries
    // ====================================================================
    
    /**
     * Get current position in steps
     */
    virtual long currentPosition() const = 0;
    
    /**
     * Get target position in steps
     */
    virtual long targetPosition() const = 0;
    
    /**
     * Check if motor is currently moving
     */
    virtual bool isRunning() const = 0;
    
    /**
     * Get current speed in steps per second
     */
    virtual float speed() const = 0;
    
    // ====================================================================
    // Position Management
    // ====================================================================
    
    /**
     * Set current position (for homing/zeroing)
     * @param position New position value in steps
     */
    virtual void setCurrentPosition(long position) = 0;
    
    // ====================================================================
    // Power/Current Control (driver-specific implementation)
    // ====================================================================
    
    /**
     * Set motor power/current
     * @param power Power level (0-255 for PWM, or driver-specific range)
     *              TB6612FNG: PWM duty cycle (0-255)
     *              TMC2209: Current percentage or mA (driver converts)
     */
    virtual void setPower(int power) = 0;
    
    /**
     * Enable motor (allow movement)
     */
    virtual void enable() = 0;
    
    /**
     * Disable motor (disable holding torque, save power)
     */
    virtual void disable() = 0;
    
    // ====================================================================
    // Initialization
    // ====================================================================
    
    /**
     * Initialize driver hardware (pins, PWM, UART, etc.)
     * Must be called once in setup() before use
     */
    virtual void begin() = 0;
};

#endif // STEPPER_DRIVER_H

