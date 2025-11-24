#ifndef CONFIG_H
#define CONFIG_H

// Steps per millimeter for each axis (calibrate for your setup)
// Default: 10.0 steps/mm (adjust based on your belt/pulley configuration)
#define STEPS_PER_MM_X 10.0
#define STEPS_PER_MM_Y 10.0

// Work area limits in millimeters
#define MAX_X_MM 200.0
#define MAX_Y_MM 200.0
#define MIN_X_MM 0.0
#define MIN_Y_MM 0.0

// Command queue configuration
#define QUEUE_SIZE 100  // Increased for file uploads (ESP32 has enough RAM)

// Serial communication
#define SERIAL_BAUD 115200
#define SERIAL_BUFFER_SIZE 128

// Dot command dwell time (milliseconds pen stays down for dot)
#define DOT_DWELL_MS 50

// Pen servo movement delay (milliseconds to wait for servo to complete movement)
// This prevents starting movement before the pen has fully lifted or lowered
#define PEN_SERVO_DELAY_MS 200

#endif

