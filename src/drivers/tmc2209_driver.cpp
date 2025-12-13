#include "tmc2209_driver.h"
#include <TMCStepper.h>  // TMCStepper library
#include <Arduino.h>

// Include pin configuration for TMC2209 constants
#ifdef BOARD_XIAO_ESP32S3
    #include "../hardware/pin_config_xiao_esp32s3.h"
#endif

TMC2209Driver::TMC2209Driver(int stepPin, int dirPin, int enablePin,
                             HardwareSerial* uartSerial, int uartAddress, float rsense)
    : stepper(AccelStepper::DRIVER, stepPin, dirPin),  // DRIVER mode for step/direction
      stepPin(stepPin),
      dirPin(dirPin),
      enablePin(enablePin),
      uartSerial(uartSerial),
      uartAddress(uartAddress),
      tmcDriver(nullptr),
      currentPower(255),
      enabled(false),
      rsense(rsense)
{
    // tmcDriver will be created in begin() after UART is initialized
}

TMC2209Driver::~TMC2209Driver() {
    if (tmcDriver) {
        delete tmcDriver;
    }
}

void TMC2209Driver::begin() {
    // Configure enable pin
    pinMode(enablePin, OUTPUT);
    digitalWrite(enablePin, HIGH);  // Disabled by default (active low)
    
    // Initialize UART if not already done
    // Note: UART should be initialized once for both drivers (shared UART)
    // We'll check if it's already initialized by checking if it's available
    if (uartSerial) {
        // UART initialization will be handled in main.cpp for shared UART
        // Just verify it's configured
        delay(10);  // Small delay for UART stability
    }
    
    // Create TMC2209 driver instance for UART communication
    // TMC2209Stepper constructor: (HardwareSerial* SerialPort, float RS, uint8_t addr)
    if (!tmcDriver && uartSerial) {
        tmcDriver = new TMC2209Stepper(uartSerial, rsense, uartAddress);
        tmcDriver->begin();
    }
    
    // Configure TMC2209 registers via UART
    if (tmcDriver) {
        // Set microstepping to 1/16 (as requested)
        tmcDriver->microsteps(TMC2209_MICROSTEPS);
        
        // Set current (run and hold)
        tmcDriver->rms_current(TMC2209_CURRENT_RUN_MA);  // Set RMS current
        
        // Calculate and set holding current
        int ihold = (TMC2209_CURRENT_HOLD_MA * 32) / 9600;
        if (ihold > 31) ihold = 31;
        if (ihold < 1) ihold = 1;
        tmcDriver->ihold(ihold);
        
        // Enable stealthChop for silent operation
        tmcDriver->en_spreadCycle(false);  // Disable spreadCycle = enable stealthChop
        tmcDriver->pwm_autoscale(true);    // Enable automatic PWM scaling
        tmcDriver->pwm_autograd(true);     // Enable automatic PWM gradient
        
        // Test communication
        uint32_t drv_status = tmcDriver->DRV_STATUS();
        if (drv_status == 0xFFFFFFFF || drv_status == 0) {
            Serial.print("Warning: TMC2209 communication may have failed (address ");
            Serial.print(uartAddress);
            Serial.println(")");
        } else {
            Serial.print("TMC2209 initialized (address ");
            Serial.print(uartAddress);
            Serial.print(") - DRV_STATUS: 0x");
            Serial.println(drv_status, HEX);
        }
    }
    
    // Enable the driver
    enable();
}

void TMC2209Driver::setMaxSpeed(float speed) {
    stepper.setMaxSpeed(speed);
}

void TMC2209Driver::setAcceleration(float acceleration) {
    stepper.setAcceleration(acceleration);
}

void TMC2209Driver::move(long relativeSteps) {
    stepper.move(relativeSteps);
}

void TMC2209Driver::moveTo(long absoluteSteps) {
    stepper.moveTo(absoluteSteps);
}

bool TMC2209Driver::run() {
    return stepper.run();
}

void TMC2209Driver::stop() {
    stepper.stop();
}

long TMC2209Driver::currentPosition() const {
    // AccelStepper methods are not const, but we need const interface
    // Use const_cast to work around this limitation
    return const_cast<AccelStepper&>(stepper).currentPosition();
}

long TMC2209Driver::targetPosition() const {
    return const_cast<AccelStepper&>(stepper).targetPosition();
}

bool TMC2209Driver::isRunning() const {
    return const_cast<AccelStepper&>(stepper).isRunning();
}

float TMC2209Driver::speed() const {
    return const_cast<AccelStepper&>(stepper).speed();
}

void TMC2209Driver::setCurrentPosition(long position) {
    stepper.setCurrentPosition(position);
}

void TMC2209Driver::setPower(int power) {
    // Clamp power to 0-255 range
    if (power > 255) power = 255;
    if (power < 0) power = 0;
    
    currentPower = power;
    
    if (!tmcDriver) return;
    
    // Map power (0-255) to current percentage (0-100%)
    // Then map to current in mA
    float powerPercent = (float)power / 255.0;
    int runCurrent = (int)(TMC2209_CURRENT_RUN_MA * powerPercent);
    int holdCurrent = (int)(TMC2209_CURRENT_HOLD_MA * powerPercent);
    
    // Minimum current values to prevent motor from losing steps
    if (runCurrent < 100) runCurrent = 100;
    if (holdCurrent < 50) holdCurrent = 50;
    
    // Update TMC2209 current via UART
    tmcDriver->rms_current(runCurrent);
    
    // Adjust holding current based on power
    int ihold = (holdCurrent * 32) / 9600;
    if (ihold > 31) ihold = 31;
    if (ihold < 1) ihold = 1;
    tmcDriver->ihold(ihold);
}

void TMC2209Driver::enable() {
    enabled = true;
    // Note: If enable pin is shared, this will enable all motors on that pin
    // That's fine for our use case where both motors share MOTOR_EN
    digitalWrite(enablePin, LOW);  // Active low - LOW = enabled
    if (tmcDriver) {
        tmcDriver->toff(4);  // Enable driver (toff > 0 enables, typical value is 4)
    }
    // Small delay to ensure enable signal is stable
    delayMicroseconds(10);
}

void TMC2209Driver::disable() {
    enabled = false;
    digitalWrite(enablePin, HIGH);  // Disable (active low)
    if (tmcDriver) {
        tmcDriver->toff(0);  // Disable driver
    }
    stop();
}

void TMC2209Driver::setMicrostepping(int microsteps) {
    if (!tmcDriver) return;
    tmcDriver->microsteps(microsteps);
}

void TMC2209Driver::setCurrent(int runCurrent_mA, int holdCurrent_mA) {
    if (!tmcDriver) return;
    tmcDriver->rms_current(runCurrent_mA);
    int ihold = (holdCurrent_mA * 32) / 9600;
    if (ihold > 31) ihold = 31;
    if (ihold < 1) ihold = 1;
    tmcDriver->ihold(ihold);
}

void TMC2209Driver::setStealthChop(bool enable) {
    if (!tmcDriver) return;
    tmcDriver->en_spreadCycle(!enable);  // Disable spreadCycle = enable stealthChop
    if (enable) {
        tmcDriver->pwm_autoscale(true);
        tmcDriver->pwm_autograd(true);
    }
}

void TMC2209Driver::setSpreadCycle(bool enable) {
    if (!tmcDriver) return;
    tmcDriver->en_spreadCycle(enable);
}

int TMC2209Driver::getMicrostepping() const {
    // TMC2209 microstepping is configured via UART
    // Default is 16x as set in pin_config_xiao_esp32s3.h
    // We could query it from the driver, but for simplicity return the configured value
    #ifdef BOARD_XIAO_ESP32S3
        return TMC2209_MICROSTEPS;  // Defined in pin config (16)
    #else
        return 16;  // Default for TMC2209
    #endif
}

