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
    
    // UART is optional - if not provided, driver uses hardware MS1/MS2 pin settings
    if (uartSerial) {
        // Initialize UART if provided
        // Note: UART should be initialized once for both drivers (shared UART)
        delay(10);  // Small delay for UART stability
        
        // Create TMC2209 driver instance for UART communication
        // TMC2209Stepper constructor: (HardwareSerial* SerialPort, float RS, uint8_t addr)
        if (!tmcDriver) {
            tmcDriver = new TMC2209Stepper(uartSerial, rsense, uartAddress);
            tmcDriver->begin();
        }
        
        // Configure TMC2209 registers via UART (optional features)
        if (tmcDriver) {
            // Set microstepping via UART (overrides MS1/MS2 pins if UART works)
            tmcDriver->microsteps(TMC2209_MICROSTEPS);
            
            // Set current (run and hold) - only works via UART
            tmcDriver->rms_current(TMC2209_CURRENT_RUN_MA);
            
            // Calculate and set holding current
            int ihold = (TMC2209_CURRENT_HOLD_MA * 32) / 9600;
            if (ihold > 31) ihold = 31;
            if (ihold < 1) ihold = 1;
            tmcDriver->ihold(ihold);
            
            // Enable stealthChop for silent operation (optional, only via UART)
            tmcDriver->en_spreadCycle(false);  // Disable spreadCycle = enable stealthChop
            tmcDriver->pwm_autoscale(true);    // Enable automatic PWM scaling
            tmcDriver->pwm_autograd(true);     // Enable automatic PWM gradient
            
            // Test communication and verify configuration
            uint32_t drv_status = tmcDriver->DRV_STATUS();
            uint32_t chopconf = tmcDriver->CHOPCONF();
            
            if (drv_status == 0xFFFFFFFF || drv_status == 0 || 
                chopconf == 0xFFFFFFFF || chopconf == 0) {
                Serial.print("TMC2209 (address ");
                Serial.print(uartAddress);
                Serial.println("): UART communication failed - using hardware MS1/MS2 pin settings");
                Serial.println("  (This is OK if you're using MS1/MS2 pins for microstepping)");
            } else {
                // Extract and display actual microstepping
                uint8_t mres = (chopconf >> 0) & 0x0F;
                int actualMicrosteps = 256 >> mres;
                
                Serial.print("TMC2209 (address ");
                Serial.print(uartAddress);
                Serial.print("): UART OK - microstepping: 1/");
                Serial.print(actualMicrosteps);
                Serial.print(" (MRES=");
                Serial.print(mres);
                Serial.println(")");
            }
        }
    } else {
        // No UART provided - driver will use hardware MS1/MS2 pin settings
        Serial.print("TMC2209 (address ");
        Serial.print(uartAddress);
        Serial.println("): No UART - using hardware MS1/MS2 pin settings for microstepping");
        Serial.println("  (Set MS1/MS2 pins: LOW/LOW=1/8, LOW/HIGH=1/32, HIGH/LOW=1/64, HIGH/HIGH=1/16)");
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
    // Try to read actual microstepping from TMC2209 register via UART
    // If UART is not available or communication fails, return configured/default value
    if (tmcDriver) {
        // Read CHOPCONF register - bits 0-3 contain MRES (microstep resolution)
        uint32_t chopconf = tmcDriver->CHOPCONF();
        
        // Check if communication is working (0xFFFFFFFF or 0 indicates failure)
        if (chopconf != 0xFFFFFFFF && chopconf != 0) {
            // Extract MRES bits (bits 0-3)
            uint8_t mres = (chopconf >> 0) & 0x0F;
            
            // Convert MRES to microstepping value
            // MRES values: 0=256, 1=128, 2=64, 3=32, 4=16, 5=8, 6=4, 7=2, 8=1
            int microsteps = 256;
            if (mres <= 8) {
                microsteps = 256 >> mres;
            }
            
            return microsteps;
        }
    }
    
    // No UART or UART read failed - return configured value
    // NOTE: If using hardware MS1/MS2 pins, you need to manually set this
    // to match your pin configuration, or the code will assume the configured value
    #ifdef BOARD_XIAO_ESP32S3
        return TMC2209_MICROSTEPS;  // Defined in pin config (16)
    #else
        return 16;  // Default for TMC2209
    #endif
}

