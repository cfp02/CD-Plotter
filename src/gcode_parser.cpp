#include "gcode_parser.h"
#include "config.h"

Command parseCommand(const String& line) {
    Command cmd;
    cmd.valid = false;
    cmd.hasX = false;
    cmd.hasY = false;
    cmd.penDown = false;
    cmd.x = 0.0;
    cmd.y = 0.0;
    cmd.type = '\0';
    cmd.code = -1;
    cmd.errorMsg = "";

    // Remove whitespace and convert to uppercase
    String trimmed = line;
    trimmed.trim();
    trimmed.toUpperCase();
    
    if (trimmed.length() == 0) {
        cmd.errorMsg = "empty command";
        return cmd;
    }

    // Handle special single-character commands
    if (trimmed.length() == 1) {
        char c = trimmed.charAt(0);
        if (c == '!') {
            cmd.type = '!';
            cmd.valid = true;
            return cmd;
        }
        if (c == '~') {
            cmd.type = '~';
            cmd.valid = true;
            return cmd;
        }
        if (c == 'H') {
            cmd.type = 'H';
            cmd.valid = true;
            return cmd;
        }
    }

    // Parse command type and code
    int pos = 0;
    char cmdType = trimmed.charAt(pos);
    
    if (!isValidCommandType(cmdType)) {
        cmd.errorMsg = "invalid command type";
        return cmd;
    }

    cmd.type = cmdType;
    pos++;

    // Parse numeric code if present
    if (cmdType == 'G' || cmdType == 'M' || cmdType == 'P') {
        int codeStart = pos;
        while (pos < trimmed.length() && isDigit(trimmed.charAt(pos))) {
            pos++;
        }
        if (pos > codeStart) {
            String codeStr = trimmed.substring(codeStart, pos);
            cmd.code = codeStr.toInt();
        } else {
            cmd.errorMsg = "missing command code";
            return cmd;
        }
    }

    // Parse coordinates (X, Y)
    while (pos < trimmed.length()) {
        char coordType = trimmed.charAt(pos);
        pos++;
        
        if (coordType == 'X' || coordType == 'Y') {
            int coordStart = pos;
            // Handle negative numbers
            if (pos < trimmed.length() && trimmed.charAt(pos) == '-') {
                pos++;
            }
            // Parse digits and decimal point
            while (pos < trimmed.length() && 
                   (isDigit(trimmed.charAt(pos)) || trimmed.charAt(pos) == '.')) {
                pos++;
            }
            
            if (pos > coordStart) {
                String coordStr = trimmed.substring(coordStart, pos);
                float value = coordStr.toFloat();
                
                if (coordType == 'X') {
                    cmd.x = value;
                    cmd.hasX = true;
                } else if (coordType == 'Y') {
                    cmd.y = value;
                    cmd.hasY = true;
                }
            } else {
                cmd.errorMsg = "invalid coordinate value";
                return cmd;
            }
        } else if (coordType != ' ' && coordType != '\t') {
            // Unknown character, skip or error?
            // For now, skip unknown characters
            pos--;
            break;
        }
    }

    // Set pen state based on command type
    if (cmdType == 'G') {
        if (cmd.code == 0) {
            cmd.penDown = false;  // G0 = rapid move, pen up
        } else if (cmd.code == 1) {
            cmd.penDown = true;   // G1 = linear move, pen down
        }
    } else if (cmdType == 'P') {
        if (cmd.code == 0) {
            cmd.penDown = false;  // P0 = pen up
        } else if (cmd.code == 1) {
            cmd.penDown = true;   // P1 = pen down
        }
    } else if (cmdType == 'D') {
        // D command = dot (will handle pen up/down in motion controller)
        cmd.penDown = true;
    }

    // Validate command
    if (cmdType == 'G' && (cmd.code == 0 || cmd.code == 1)) {
        // G0/G1 require at least one coordinate
        if (!cmd.hasX && !cmd.hasY) {
            cmd.errorMsg = "G0/G1 requires X or Y coordinate";
            return cmd;
        }
    } else if (cmdType == 'D') {
        // D command requires both coordinates
        if (!cmd.hasX || !cmd.hasY) {
            cmd.errorMsg = "D command requires X and Y coordinates";
            return cmd;
        }
    } else if (cmdType == 'P' && cmd.code != 0 && cmd.code != 1) {
        cmd.errorMsg = "P command requires code 0 or 1";
        return cmd;
    } else if (cmdType == 'M' && cmd.code != 114 && cmd.code != 119) {
        cmd.errorMsg = "unsupported M code";
        return cmd;
    }

    cmd.valid = true;
    return cmd;
}

bool isValidCommandType(char c) {
    return (c == 'G' || c == 'D' || c == 'P' || c == 'H' || 
            c == 'M' || c == '!' || c == '~');
}

