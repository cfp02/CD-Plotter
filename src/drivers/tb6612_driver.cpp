#include "tb6612_driver.h"
#include <Arduino.h>

TB6612Driver::TB6612Driver(int stepMode,
                           int pinA1, int pinA2, int pinB1, int pinB2,
                           int pwmA, int pwmB,
                           int channelA, int channelB)
    : stepper(stepMode, pinA1, pinA2, pinB1, pinB2),
      pwmPinA(pwmA),
      pwmPinB(pwmB),
      pwmChannelA(channelA),
      pwmChannelB(channelB),
      currentPower(255),
      enabled(true)
{
    // Constructor - begin() will be called later to initialize hardware
}

void TB6612Driver::begin() {
    // Configure PWM channels for power control
    ledcSetup(pwmChannelA, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(pwmChannelB, PWM_FREQUENCY, PWM_RESOLUTION);
    
    ledcAttachPin(pwmPinA, pwmChannelA);
    ledcAttachPin(pwmPinB, pwmChannelB);
    
    // Set initial power
    setPower(255);  // Full power initially, will be adjusted by main code
    enable();
}

void TB6612Driver::setMaxSpeed(float speed) {
    stepper.setMaxSpeed(speed);
}

void TB6612Driver::setAcceleration(float acceleration) {
    stepper.setAcceleration(acceleration);
}

void TB6612Driver::move(long relativeSteps) {
    stepper.move(relativeSteps);
}

void TB6612Driver::moveTo(long absoluteSteps) {
    stepper.moveTo(absoluteSteps);
}

bool TB6612Driver::run() {
    return stepper.run();
}

void TB6612Driver::stop() {
    stepper.stop();
}

long TB6612Driver::currentPosition() const {
    return stepper.currentPosition();
}

long TB6612Driver::targetPosition() const {
    return stepper.targetPosition();
}

bool TB6612Driver::isRunning() const {
    return stepper.isRunning();
}

float TB6612Driver::speed() const {
    return stepper.speed();
}

void TB6612Driver::setCurrentPosition(long position) {
    stepper.setCurrentPosition(position);
}

void TB6612Driver::setPower(int power) {
    // Clamp power to 0-255 range
    if (power > 255) power = 255;
    if (power < 0) power = 0;
    
    currentPower = power;
    
    if (enabled) {
        ledcWrite(pwmChannelA, power);
        ledcWrite(pwmChannelB, power);
    } else {
        // If disabled, keep power at 0
        ledcWrite(pwmChannelA, 0);
        ledcWrite(pwmChannelB, 0);
    }
}

void TB6612Driver::enable() {
    enabled = true;
    // Restore power setting
    ledcWrite(pwmChannelA, currentPower);
    ledcWrite(pwmChannelB, currentPower);
}

void TB6612Driver::disable() {
    enabled = false;
    // Set power to 0 when disabled
    ledcWrite(pwmChannelA, 0);
    ledcWrite(pwmChannelB, 0);
}

