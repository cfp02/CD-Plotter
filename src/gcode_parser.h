#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <Arduino.h>

// Command structure
struct Command {
    char type;        // 'G', 'D', 'P', 'H', 'M', '!', '~'
    int code;         // 0, 1, 114, 119, etc.
    float x, y;       // Coordinates in mm
    bool hasX, hasY;  // Coordinate flags
    bool penDown;     // For G1 vs G0
    bool valid;       // Whether command parsed successfully
    String errorMsg;  // Error message if invalid
};

// Parse a command string and return Command struct
Command parseCommand(const String& line);

// Check if a character is a valid command type
bool isValidCommandType(char c);

#endif

