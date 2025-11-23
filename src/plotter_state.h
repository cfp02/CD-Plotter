#ifndef PLOTTER_STATE_H
#define PLOTTER_STATE_H

#include <Arduino.h>

enum PlotterState {
    STATE_IDLE,
    STATE_MOVING,
    STATE_HOMING,
    STATE_PAUSED,
    STATE_ERROR
};

class PlotterStateMachine {
private:
    PlotterState currentState;
    String errorMessage;

public:
    PlotterStateMachine();
    PlotterState getState() const;
    bool setState(PlotterState newState);
    bool canAcceptCommands() const;
    void pause();
    void resume();
    void emergencyStop();
    void clearError();
    String getStateString() const;
    String getErrorMessage() const;
    void setError(const String& msg);
};

#endif

