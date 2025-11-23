#include "plotter_state.h"

PlotterStateMachine::PlotterStateMachine() {
    currentState = STATE_IDLE;
    errorMessage = "";
}

PlotterState PlotterStateMachine::getState() const {
    return currentState;
}

bool PlotterStateMachine::setState(PlotterState newState) {
    // Validate state transitions
    switch (currentState) {
        case STATE_IDLE:
            if (newState == STATE_MOVING || newState == STATE_HOMING || 
                newState == STATE_PAUSED || newState == STATE_ERROR) {
                currentState = newState;
                return true;
            }
            break;
        case STATE_MOVING:
            if (newState == STATE_IDLE || newState == STATE_PAUSED || 
                newState == STATE_ERROR) {
                currentState = newState;
                return true;
            }
            break;
        case STATE_HOMING:
            if (newState == STATE_IDLE || newState == STATE_ERROR) {
                currentState = newState;
                return true;
            }
            break;
        case STATE_PAUSED:
            if (newState == STATE_IDLE || newState == STATE_MOVING || 
                newState == STATE_ERROR) {
                currentState = newState;
                return true;
            }
            break;
        case STATE_ERROR:
            if (newState == STATE_IDLE) {
                currentState = newState;
                errorMessage = "";
                return true;
            }
            break;
    }
    return false;
}

bool PlotterStateMachine::canAcceptCommands() const {
    return (currentState == STATE_IDLE || currentState == STATE_PAUSED);
}

void PlotterStateMachine::pause() {
    if (currentState == STATE_MOVING) {
        setState(STATE_PAUSED);
    }
}

void PlotterStateMachine::resume() {
    if (currentState == STATE_PAUSED) {
        setState(STATE_IDLE);
    }
}

void PlotterStateMachine::emergencyStop() {
    if (currentState == STATE_MOVING || currentState == STATE_HOMING) {
        setState(STATE_PAUSED);
    }
}

void PlotterStateMachine::clearError() {
    if (currentState == STATE_ERROR) {
        setState(STATE_IDLE);
    }
}

String PlotterStateMachine::getStateString() const {
    switch (currentState) {
        case STATE_IDLE: return "IDLE";
        case STATE_MOVING: return "MOVING";
        case STATE_HOMING: return "HOMING";
        case STATE_PAUSED: return "PAUSED";
        case STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

String PlotterStateMachine::getErrorMessage() const {
    return errorMessage;
}

void PlotterStateMachine::setError(const String& msg) {
    errorMessage = msg;
    setState(STATE_ERROR);
}

