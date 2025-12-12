#ifndef PIN_CONFIG_ESP32DEV_H
#define PIN_CONFIG_ESP32DEV_H

/**
 * Pin Configuration for ESP32 DevKit with TB6612FNG Drivers
 * 
 * This file contains all pin definitions for the ESP32 DevKit board
 * when using TB6612FNG motor drivers.
 */

// ====================================================================
// TB6612FNG Pin Definitions for Stepper Motor 1 (X-Axis)
// ====================================================================
// Motor A (Coil 1)
#define MOTOR1_AIN1 14  // GPIO14 - Motor A Input 1
#define MOTOR1_AIN2 12  // GPIO12 - Motor A Input 2
#define MOTOR1_PWMA 13  // GPIO13 - Motor A PWM (power control)

// Motor B (Coil 2)
#define MOTOR1_BIN1 27  // GPIO27 - Motor B Input 1
#define MOTOR1_BIN2 26  // GPIO26 - Motor B Input 2
#define MOTOR1_PWMB 19  // GPIO19 - Motor B PWM (power control)

// ====================================================================
// TB6612FNG Pin Definitions for Stepper Motor 2 (Y-Axis)
// ====================================================================
// Motor A (Coil 1)
#define MOTOR2_AIN1 33  // GPIO33 - Motor A Input 1
#define MOTOR2_AIN2 25  // GPIO25 - Motor A Input 2
#define MOTOR2_PWMA 23  // GPIO23 - Motor A PWM (power control)

// Motor B (Coil 2)
#define MOTOR2_BIN1 4   // GPIO4 - Motor B Input 1
#define MOTOR2_BIN2 5   // GPIO5 - Motor B Input 2
#define MOTOR2_PWMB 22  // GPIO22 - Motor B PWM (power control)

// Note: STBY pins are tied directly to 3.3V (not controlled by ESP32)

// ====================================================================
// PWM Channel Assignments
// ====================================================================
// Use channels 4-7 for motors to avoid conflict with ESP32Servo (which uses channels 0-3)
#define PWM_CHANNEL_M1A 4
#define PWM_CHANNEL_M1B 5
#define PWM_CHANNEL_M2A 6
#define PWM_CHANNEL_M2B 7

// ====================================================================
// Servo Pin
// ====================================================================
#define SERVO_PIN 18    // GPIO18 - Servo control pin for pen up/down

// ====================================================================
// PWM Settings
// ====================================================================
#define PWM_FREQUENCY 20000  // 20 kHz (ultrasonic - above human hearing ~20 kHz)
#define PWM_RESOLUTION 8     // 8-bit resolution (0-255)

#endif // PIN_CONFIG_ESP32DEV_H

