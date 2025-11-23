#ifndef MOTION_CONTROLLER_H
#define MOTION_CONTROLLER_H

#include <Arduino.h>
#include <AccelStepper.h>
#include "gcode_parser.h"
#include "config.h"

class MotionController {
private:
    AccelStepper* stepperX;
    AccelStepper* stepperY;
    
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

public:
    MotionController(AccelStepper* xStepper, AccelStepper* yStepper);
    
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
    
    // Pen control (placeholder for future servo)
    bool getPenState() const;
    
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

