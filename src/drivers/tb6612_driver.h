#ifndef TB6612_DRIVER_H
#define TB6612_DRIVER_H

#include "stepper_driver.h"
#include <AccelStepper.h>

/**
 * TB6612FNG Driver Implementation
 * 
 * Wraps AccelStepper library to provide StepperDriver interface.
 * Handles PWM power control via ESP32 LEDC channels.
 */
class TB6612Driver : public StepperDriver {
private:
    AccelStepper stepper;
    int pwmPinA;
    int pwmPinB;
    int pwmChannelA;
    int pwmChannelB;
    int currentPower;
    bool enabled;
    
    // PWM settings
    static constexpr int PWM_FREQUENCY = 20000;  // 20 kHz (ultrasonic)
    static constexpr int PWM_RESOLUTION = 8;     // 8-bit (0-255)
    
public:
    /**
     * Constructor
     * @param stepMode AccelStepper step mode (FULL4WIRE or HALF4WIRE)
     * @param pinA1 Motor A input 1 pin
     * @param pinA2 Motor A input 2 pin
     * @param pinB1 Motor B input 1 pin
     * @param pinB2 Motor B input 2 pin
     * @param pwmA PWM pin for motor A (power control)
     * @param pwmB PWM pin for motor B (power control)
     * @param channelA LEDC channel for PWM A (must be unique, avoid 0-3 for servo)
     * @param channelB LEDC channel for PWM B (must be unique, avoid 0-3 for servo)
     */
    TB6612Driver(int stepMode, 
                 int pinA1, int pinA2, int pinB1, int pinB2,
                 int pwmA, int pwmB,
                 int channelA, int channelB);
    
    // StepperDriver interface implementation
    void setMaxSpeed(float speed) override;
    void setAcceleration(float acceleration) override;
    void move(long relativeSteps) override;
    void moveTo(long absoluteSteps) override;
    bool run() override;
    void stop() override;
    
    long currentPosition() const override;
    long targetPosition() const override;
    bool isRunning() const override;
    float speed() const override;
    
    void setCurrentPosition(long position) override;
    
    void setPower(int power) override;
    void enable() override;
    void disable() override;
    
    void begin() override;
};

#endif // TB6612_DRIVER_H

