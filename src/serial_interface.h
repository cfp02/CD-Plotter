#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <Arduino.h>
#include "gcode_parser.h"
#include "command_queue.h"
#include "config.h"

class SerialInterface {
private:
    String lineBuffer;
    CommandQueue* queue;
    unsigned long lastResponseTime;
    static const unsigned long RESPONSE_DELAY_MS = 10;

public:
    SerialInterface(CommandQueue* cmdQueue);
    void processSerialInput();
    void sendResponse(const String& response);
    void sendError(const String& message);
    void sendOK();
};

#endif

