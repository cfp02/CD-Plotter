#include "motion_controller.h"

MotionController::MotionController(AccelStepper* xStepper, AccelStepper* yStepper) {
    stepperX = xStepper;
    stepperY = yStepper;
    currentX_mm = 0.0;
    currentY_mm = 0.0;
    currentX_steps = 0;
    currentY_steps = 0;
    stepsPerMM_X = STEPS_PER_MM_X;
    stepsPerMM_Y = STEPS_PER_MM_Y;
    penDown = false;
    minX_mm = MIN_X_MM;
    maxX_mm = MAX_X_MM;
    minY_mm = MIN_Y_MM;
    maxY_mm = MAX_Y_MM;
    dotDwellMs = DOT_DWELL_MS;
    dotInProgress = false;
    dotStartTime = 0;
    dotState = DOT_MOVE_DONE;
}

void MotionController::initialize() {
    currentX_mm = 0.0;
    currentY_mm = 0.0;
    currentX_steps = 0;
    currentY_steps = 0;
    penDown = false;
    dotInProgress = false;
    
    // Sync stepper positions with our tracking
    stepperX->setCurrentPosition(0);
    stepperY->setCurrentPosition(0);
}

void MotionController::setStepsPerMM(float x, float y) {
    stepsPerMM_X = x;
    stepsPerMM_Y = y;
}

void MotionController::setWorkArea(float minX, float maxX, float minY, float maxY) {
    minX_mm = minX;
    maxX_mm = maxX;
    minY_mm = minY;
    maxY_mm = maxY;
}

float MotionController::getX_mm() const {
    return currentX_mm;
}

float MotionController::getY_mm() const {
    return currentY_mm;
}

void MotionController::setPosition(float x_mm, float y_mm) {
    currentX_mm = x_mm;
    currentY_mm = y_mm;
    currentX_steps = (long)(x_mm * stepsPerMM_X);
    currentY_steps = (long)(y_mm * stepsPerMM_Y);
    stepperX->setCurrentPosition(currentX_steps);
    stepperY->setCurrentPosition(currentY_steps);
}

bool MotionController::isValidPosition(float x_mm, float y_mm) const {
    return (x_mm >= minX_mm && x_mm <= maxX_mm &&
            y_mm >= minY_mm && y_mm <= maxY_mm);
}

bool MotionController::executeRapidMove(float x_mm, float y_mm) {
    // Validate position
    if (!isValidPosition(x_mm, y_mm)) {
        return false;
    }
    
    // Calculate target steps
    long targetX_steps = (long)(x_mm * stepsPerMM_X);
    long targetY_steps = (long)(y_mm * stepsPerMM_Y);
    
    // Calculate relative moves
    long deltaX = targetX_steps - currentX_steps;
    long deltaY = targetY_steps - currentY_steps;
    
    // Execute moves (pen up for rapid move)
    penDown = false;
    // TODO: Actual pen servo control here
    
    if (deltaX != 0) {
        stepperX->move(deltaX);
    }
    if (deltaY != 0) {
        stepperY->move(deltaY);
    }
    
    // Update position
    currentX_mm = x_mm;
    currentY_mm = y_mm;
    currentX_steps = targetX_steps;
    currentY_steps = targetY_steps;
    
    return true;
}

bool MotionController::executeLinearMove(float x_mm, float y_mm) {
    // Validate position
    if (!isValidPosition(x_mm, y_mm)) {
        return false;
    }
    
    // Calculate target steps
    long targetX_steps = (long)(x_mm * stepsPerMM_X);
    long targetY_steps = (long)(y_mm * stepsPerMM_Y);
    
    // Calculate relative moves
    long deltaX = targetX_steps - currentX_steps;
    long deltaY = targetY_steps - currentY_steps;
    
    // Execute moves (pen down for linear move)
    penDown = true;
    // TODO: Actual pen servo control here
    
    if (deltaX != 0) {
        stepperX->move(deltaX);
    }
    if (deltaY != 0) {
        stepperY->move(deltaY);
    }
    
    // Update position
    currentX_mm = x_mm;
    currentY_mm = y_mm;
    currentX_steps = targetX_steps;
    currentY_steps = targetY_steps;
    
    return true;
}

bool MotionController::executeDot(float x_mm, float y_mm) {
    // Validate position
    if (!isValidPosition(x_mm, y_mm)) {
        return false;
    }
    
    // Move to position with pen up
    if (!executeRapidMove(x_mm, y_mm)) {
        return false;
    }
    
    // Wait for move to complete (will be checked in update())
    // Then pen down, dwell, pen up
    dotInProgress = true;
    dotState = DOT_MOVE_DONE;
    dotStartTime = 0;
    
    return true;
}

void MotionController::update() {
    // Handle dot dwell timing
    if (dotInProgress) {
        // Check if we've reached the position (steppers stopped moving)
        if (!stepperX->isRunning() && !stepperY->isRunning()) {
            // Position reached, now handle pen down/up sequence
            if (dotState == DOT_MOVE_DONE) {
                // Pen down
                penDown = true;
                // TODO: Actual pen servo control here
                dotState = DOT_PEN_DOWN;
                dotStartTime = millis();
            } else if (dotState == DOT_PEN_DOWN) {
                // Start dwelling
                dotState = DOT_DWELLING;
            } else if (dotState == DOT_DWELLING) {
                // Check if dwell time elapsed
                if (millis() - dotStartTime >= dotDwellMs) {
                    // Pen up
                    penDown = false;
                    // TODO: Actual pen servo control here
                    dotInProgress = false;
                    dotState = DOT_MOVE_DONE;  // Reset for next dot
                }
            }
        }
    }
}

bool MotionController::executePenUp() {
    penDown = false;
    // TODO: Actual pen servo control here
    return true;
}

bool MotionController::executePenDown() {
    penDown = true;
    // TODO: Actual pen servo control here
    return true;
}

bool MotionController::executeHome() {
    // Placeholder: just set position to 0,0
    // TODO: Implement actual homing with limit switches
    setPosition(0.0, 0.0);
    return true;
}

bool MotionController::isMoving() const {
    return stepperX->isRunning() || stepperY->isRunning() || dotInProgress;
}

bool MotionController::getPenState() const {
    return penDown;
}

