#ifndef MOTION_CONTROLLER_H
#define MOTION_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "gcode_parser.h"
#include "config.h"
#include "drivers/stepper_driver.h"

class MotionController {
private:
    StepperDriver* stepperX;
    StepperDriver* stepperY;
    Servo* penServo;
    
    // Current position in mm
    float currentX_mm;
    float currentY_mm;
    
    // Current position in steps
    long currentX_steps;
    long currentY_steps;
    
    // Steps per mm (can be calibrated)
    float stepsPerMM_X;
    float stepsPerMM_Y;
    
    // Direction inversion flags
    bool invertX;
    bool invertY;
    
    // Pen state
    bool penDown;
    
    // Work area limits
    float minX_mm, maxX_mm;
    float minY_mm, maxY_mm;
    
    // Dot dwell time
    unsigned long dotDwellMs;
    unsigned long dotStartTime;
    bool dotInProgress;
    enum DotState { DOT_MOVE_DONE, DOT_PEN_DOWN, DOT_DWELLING };
    DotState dotState;
    
    // Pen servo angles
    int penUpAngle;
    int penDownAngle;
    
    // Manual mode flag (set externally to prevent interference)
    bool manualMode;

public:
    MotionController(StepperDriver* xStepper, StepperDriver* yStepper, Servo* servo);
    
    void initialize();
    void setStepsPerMM(float x, float y);
    void setWorkArea(float minX, float maxX, float minY, float maxY);
    void setWorkAreaLimit(char axis, char limit, float value);  // axis: 'X' or 'Y', limit: 'M' (min) or 'M' (max)
    
    // Position management
    float getX_mm() const;
    float getY_mm() const;
    void setPosition(float x_mm, float y_mm);
    
    // Motion execution
    bool executeRapidMove(float x_mm, float y_mm);
    bool executeLinearMove(float x_mm, float y_mm);
    bool executeDot(float x_mm, float y_mm);
    bool executePenUp();
    bool executePenDown();
    bool executeHome();
    void setCurrentPositionAsHome();  // Register current position as home (top-left)
    
    // Coordinate validation
    bool isValidPosition(float x_mm, float y_mm) const;
    
    // Motion status
    bool isMoving() const;
    void update();  // Call in loop() to handle dot dwell timing
    
    // Pen control
    bool getPenState() const;
    void setPenUpAngle(int angle);
    void setPenDownAngle(int angle);
    void setDotDwellMs(unsigned long ms);
    int getPenUpAngle() const;
    int getPenDownAngle() const;
    unsigned long getDotDwellMs() const;
    void setManualMode(bool manual);
    
    // Configuration getters
    float getStepsPerMM_X() const;
    float getStepsPerMM_Y() const;
    float getMinX() const;
    float getMaxX() const;
    float getMinY() const;
    float getMaxY() const;
    
    // Direction control
    void setInvertX(bool invert);
    void setInvertY(bool invert);
    bool getInvertX() const;
    bool getInvertY() const;
};

#endif

