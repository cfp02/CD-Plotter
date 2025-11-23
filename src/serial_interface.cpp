#include "serial_interface.h"

SerialInterface::SerialInterface(CommandQueue* cmdQueue) {
    queue = cmdQueue;
    lineBuffer = "";
    lastResponseTime = 0;
}

void SerialInterface::processSerialInput() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            if (lineBuffer.length() > 0) {
                // Parse and queue the command
                Command cmd = parseCommand(lineBuffer);
                
                if (cmd.valid) {
                    if (queue->enqueue(cmd)) {
                        // Command queued successfully
                        // Response will be sent when command completes
                    } else {
                        sendError("queue full");
                    }
                } else {
                    sendError(cmd.errorMsg);
                }
                
                lineBuffer = "";
            }
        } else if (lineBuffer.length() < SERIAL_BUFFER_SIZE - 1) {
            lineBuffer += c;
        } else {
            // Buffer overflow
            lineBuffer = "";
            sendError("command too long");
        }
    }
}

void SerialInterface::sendResponse(const String& response) {
    // Small delay to prevent overwhelming the serial buffer
    unsigned long now = millis();
    if (now - lastResponseTime >= RESPONSE_DELAY_MS) {
        Serial.print(response);
        Serial.print("\n");
        lastResponseTime = now;
    }
}

void SerialInterface::sendError(const String& message) {
    sendResponse("error: " + message);
}

void SerialInterface::sendOK() {
    sendResponse("ok");
}

