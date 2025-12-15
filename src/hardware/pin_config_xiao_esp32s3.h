#ifndef PIN_CONFIG_XIAO_ESP32S3_H
#define PIN_CONFIG_XIAO_ESP32S3_H

/**
 * Pin Configuration for Seeed Studio XIAO ESP32-S3 with TMC2209 Drivers
 * 
 * This file contains all pin definitions for the XIAO ESP32-S3 board
 * when using TMC2209 motor drivers with shared UART.
 */

// ====================================================================
// TMC2209 Pin Definitions for Stepper Motor 1 (X-Axis)
// ====================================================================
#define MOTOR1_STEP 1   // GPIO1 - Step pin
#define MOTOR1_DIR 2    // GPIO2 - Direction pin

// ====================================================================
// TMC2209 Pin Definitions for Stepper Motor 2 (Y-Axis)
// ====================================================================
#define MOTOR2_STEP 3   // GPIO3 - Step pin
#define MOTOR2_DIR 4    // GPIO4 - Direction pin

// ====================================================================
// Shared TMC2209 Control Pins
// ====================================================================
#define MOTOR_EN 5      // GPIO5 - Enable pin (shared for both motors)

// ====================================================================
// TMC2209 UART Communication (Shared UART for both drivers)
// ====================================================================
#define TMC_UART_TX 43  // GPIO43 - UART TX (connects to TMC2209 RX/PDN_UART)
#define TMC_UART_RX 44  // GPIO44 - UART RX (connects to TMC2209 TX)

// UART Configuration
#define TMC_UART_BAUD 115200  // Standard TMC2209 UART baud rate
#define TMC_UART_NUM 1        // UART peripheral number (UART1)

// TMC2209 UART Addresses (for shared UART bus)
// Address 0 = default (PDN_UART pin not connected or tied low)
// Address 1 = PDN_UART pin tied high
#define TMC2209_ADDRESS_MOTOR1 0  // First driver (default address)
#define TMC2209_ADDRESS_MOTOR2 1  // Second driver (PDN_UART high)

// ====================================================================
// Servo Pin
// ====================================================================
#define SERVO_PIN 7    // GPIO7 - Servo control pin for pen up/down

// ====================================================================
// Endstop Pins (Planned - not yet wired)
// ====================================================================
#define ENDSTOP_X 8    // GPIO8 - X-axis endstop (planned)
#define ENDSTOP_Y 9    // GPIO9 - Y-axis endstop (planned)

// ====================================================================
// TMC2209 Configuration Constants
// ====================================================================
#define TMC2209_RSENSE 0.11f    // Sense resistor value (ohms) - typical for SilentStepStick

// Microstepping setting - IMPORTANT: Match this to your MS1/MS2 pin configuration!
// If using hardware MS1/MS2 pins (not UART), set this to match your pin settings:
//   MS1 floating, MS2 floating → defaults to 1/8  microstepping → set to 8
//   MS1 LOW,  MS2 LOW  → 1/8  microstepping → set to 8
//   MS1 LOW,  MS2 HIGH → 1/32 microstepping → set to 32
//   MS1 HIGH, MS2 LOW  → 1/64 microstepping → set to 64
//   MS1 HIGH, MS2 HIGH → 1/16 microstepping → set to 16
// If using UART, this value will be written to the driver (overrides MS1/MS2 pins)
// 
// EASIEST: Leave MS1/MS2 floating (not connected) → defaults to 1/8 → set to 8
#define TMC2209_MICROSTEPS 8   // Microstepping: 1/8 (default when MS1/MS2 are floating)

#define TMC2209_CURRENT_RUN_MA 800   // Running current in mA (adjust based on motor)
#define TMC2209_CURRENT_HOLD_MA 400  // Holding current in mA (50% of running)

#endif // PIN_CONFIG_XIAO_ESP32S3_H

