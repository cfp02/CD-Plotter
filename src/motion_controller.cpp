#include "motion_controller.h"

MotionController::MotionController(AccelStepper* xStepper, AccelStepper* yStepper, Servo* servo) {
    stepperX = xStepper;
    stepperY = yStepper;
    penServo = servo;
    currentX_mm = 0.0;
    currentY_mm = 0.0;
    currentX_steps = 0;
    currentY_steps = 0;
    stepsPerMM_X = STEPS_PER_MM_X;
    stepsPerMM_Y = STEPS_PER_MM_Y;
    invertX = false;
    invertY = false;
    penDown = false;
    minX_mm = MIN_X_MM;
    maxX_mm = MAX_X_MM;
    minY_mm = MIN_Y_MM;
    maxY_mm = MAX_Y_MM;
    dotDwellMs = DOT_DWELL_MS;
    dotInProgress = false;
    dotStartTime = 0;
    dotState = DOT_MOVE_DONE;
    penUpAngle = 0;
    penDownAngle = 90;
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

void MotionController::setWorkAreaLimit(char axis, char limit, float value) {
    // axis: 'X' or 'Y', limit: 'M' (min) or 'X' (max)
    if (axis == 'X' || axis == 'x') {
        if (limit == 'M' || limit == 'm') {
            minX_mm = value;
        } else if (limit == 'X' || limit == 'x') {
            maxX_mm = value;
        }
    } else if (axis == 'Y' || axis == 'y') {
        if (limit == 'M' || limit == 'm') {
            minY_mm = value;
        } else if (limit == 'X' || limit == 'x') {
            maxY_mm = value;
        }
    }
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
    long xSteps = (long)(x_mm * stepsPerMM_X);
    long ySteps = (long)(y_mm * stepsPerMM_Y);
    // Store steps without inversion (inversion is applied during moves)
    currentX_steps = xSteps;
    currentY_steps = ySteps;
    // Apply inversion when setting stepper position
    stepperX->setCurrentPosition(invertX ? -xSteps : xSteps);
    stepperY->setCurrentPosition(invertY ? -ySteps : ySteps);
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
    
    // Calculate target steps (without inversion - inversion applied to stepper)
    long targetX_steps_raw = (long)(x_mm * stepsPerMM_X);
    long targetY_steps_raw = (long)(y_mm * stepsPerMM_Y);
    
    // Get current stepper positions (accounting for inversion)
    long currentX_stepper = stepperX->currentPosition();
    long currentY_stepper = stepperY->currentPosition();
    
    // Convert stepper positions back to raw steps
    long currentX_raw = invertX ? -currentX_stepper : currentX_stepper;
    long currentY_raw = invertY ? -currentY_stepper : currentY_stepper;
    
    // Calculate relative moves in raw steps
    long deltaX_raw = targetX_steps_raw - currentX_raw;
    long deltaY_raw = targetY_steps_raw - currentY_raw;
    
    // Apply inversion for stepper movement
    long deltaX = invertX ? -deltaX_raw : deltaX_raw;
    long deltaY = invertY ? -deltaY_raw : deltaY_raw;
    
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
    currentX_steps = targetX_steps_raw;
    currentY_steps = targetY_steps_raw;
    
    return true;
}

bool MotionController::executeLinearMove(float x_mm, float y_mm) {
    // Validate position
    if (!isValidPosition(x_mm, y_mm)) {
        return false;
    }
    
    // Calculate target steps (without inversion - inversion applied to stepper)
    long targetX_steps_raw = (long)(x_mm * stepsPerMM_X);
    long targetY_steps_raw = (long)(y_mm * stepsPerMM_Y);
    
    // Get current stepper positions (accounting for inversion)
    long currentX_stepper = stepperX->currentPosition();
    long currentY_stepper = stepperY->currentPosition();
    
    // Convert stepper positions back to raw steps
    long currentX_raw = invertX ? -currentX_stepper : currentX_stepper;
    long currentY_raw = invertY ? -currentY_stepper : currentY_stepper;
    
    // Calculate relative moves in raw steps
    long deltaX_raw = targetX_steps_raw - currentX_raw;
    long deltaY_raw = targetY_steps_raw - currentY_raw;
    
    // Apply inversion for stepper movement
    long deltaX = invertX ? -deltaX_raw : deltaX_raw;
    long deltaY = invertY ? -deltaY_raw : deltaY_raw;
    
    // Execute moves (pen down for linear move)
    penDown = true;
    if (penServo) {
        penServo->write(penDownAngle);
    }
    
    if (deltaX != 0) {
        stepperX->move(deltaX);
    }
    if (deltaY != 0) {
        stepperY->move(deltaY);
    }
    
    // Update position
    currentX_mm = x_mm;
    currentY_mm = y_mm;
    currentX_steps = targetX_steps_raw;
    currentY_steps = targetY_steps_raw;
    
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
                if (penServo) {
                    penServo->write(penDownAngle);
                }
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
                    if (penServo) {
                        penServo->write(penUpAngle);
                    }
                    dotInProgress = false;
                    dotState = DOT_MOVE_DONE;  // Reset for next dot
                }
            }
        }
    }
}

bool MotionController::executePenUp() {
    penDown = false;
    if (penServo) {
        penServo->write(penUpAngle);
    }
    return true;
}

bool MotionController::executePenDown() {
    penDown = true;
    if (penServo) {
        penServo->write(penDownAngle);
    }
    return true;
}

bool MotionController::executeHome() {
    // Move to home position (top-left corner: minX, maxY)
    // This is the typical home position for plotters
    float homeX = minX_mm;
    float homeY = maxY_mm;
    
    // Execute rapid move to home position
    return executeRapidMove(homeX, homeY);
}

void MotionController::setCurrentPositionAsHome() {
    // Register current position as the new home (0, 0)
    // This ONLY sets the coordinate origin - does NOT change work area limits
    // The work area limits should already be set correctly from calibration
    
    // Get current stepper positions
    long currentX_stepper = stepperX->currentPosition();
    long currentY_stepper = stepperY->currentPosition();
    
    // Convert to raw steps (remove inversion)
    long currentX_raw = invertX ? -currentX_stepper : currentX_stepper;
    long currentY_raw = invertY ? -currentY_stepper : currentY_stepper;
    
    // Reset stepper positions to 0 (accounting for inversion)
    // This makes the current physical position = (0,0) in our coordinate system
    stepperX->setCurrentPosition(0);
    stepperY->setCurrentPosition(0);
    
    // Set our tracking to (0,0)
    currentX_mm = 0.0;
    currentY_mm = 0.0;
    currentX_steps = 0;
    currentY_steps = 0;
    
    // Work area limits remain unchanged - they define the physical boundaries
}

bool MotionController::isMoving() const {
    return stepperX->isRunning() || stepperY->isRunning() || dotInProgress;
}

bool MotionController::getPenState() const {
    return penDown;
}

void MotionController::setPenUpAngle(int angle) {
    penUpAngle = angle;
    if (!penDown && penServo) {
        penServo->write(penUpAngle);
    }
}

void MotionController::setPenDownAngle(int angle) {
    penDownAngle = angle;
    if (penDown && penServo) {
        penServo->write(penDownAngle);
    }
}

void MotionController::setDotDwellMs(unsigned long ms) {
    dotDwellMs = ms;
}

int MotionController::getPenUpAngle() const {
    return penUpAngle;
}

int MotionController::getPenDownAngle() const {
    return penDownAngle;
}

unsigned long MotionController::getDotDwellMs() const {
    return dotDwellMs;
}

float MotionController::getStepsPerMM_X() const {
    return stepsPerMM_X;
}

float MotionController::getStepsPerMM_Y() const {
    return stepsPerMM_Y;
}

float MotionController::getMinX() const {
    return minX_mm;
}

float MotionController::getMaxX() const {
    return maxX_mm;
}

float MotionController::getMinY() const {
    return minY_mm;
}

float MotionController::getMaxY() const {
    return maxY_mm;
}

void MotionController::setInvertX(bool invert) {
    invertX = invert;
}

void MotionController::setInvertY(bool invert) {
    invertY = invert;
}

bool MotionController::getInvertX() const {
    return invertX;
}

bool MotionController::getInvertY() const {
    return invertY;
}

