# G-code Interpreter Testing Guide

## Quick Start

1. **Upload the code** to your ESP32 using PlatformIO
2. **Open Serial Monitor** at 115200 baud
3. **Send G-code commands** via serial

## Configuration

Before testing, you may need to calibrate `STEPS_PER_MM` in `src/config.h`:
- Default is 10.0 steps/mm
- Measure actual movement and adjust accordingly
- For example, if you command `G0 X10` (10mm) but it moves 12mm, set `STEPS_PER_MM_X = 10.0 * (10/12) = 8.33`

## Basic Commands to Test

### 1. Get Current Position
```
M114
```
Expected response: `X:0.00 Y:0.00 Pen:UP` followed by `ok`

### 2. Rapid Move (Pen Up)
```
G0 X10 Y10
```
Moves to position (10mm, 10mm) with pen up. Response: `ok` when complete.

### 3. Linear Move (Pen Down)
```
G1 X20 Y20
```
Moves to position (20mm, 20mm) with pen down. Response: `ok` when complete.

### 4. Partial Coordinates
```
G0 X30
```
Moves only X axis to 30mm, Y stays at current position.

```
G0 Y15
```
Moves only Y axis to 15mm, X stays at current position.

### 5. Dot Command
```
D X5 Y5
```
Moves to (5mm, 5mm), pen down, dwell 50ms, pen up. Response: `ok` when complete.

### 6. Pen Control
```
P1
```
Pen down (no movement)

```
P0
```
Pen up (no movement)

### 7. Home
```
H
```
Sets current position to (0, 0). Response: `ok` immediately.

### 8. Emergency Stop
```
!
```
Stops all motion immediately and pauses. Response: `ok`

### 9. Resume
```
~
```
Resumes from paused state. Response: `ok`

## Testing Sequence

1. **Test basic movement:**
   ```
   G0 X10 Y10
   M114
   G0 X0 Y0
   ```

2. **Test dot command:**
   ```
   D X5 Y5
   D X10 Y5
   D X10 Y10
   ```

3. **Test queue (send multiple commands quickly):**
   ```
   G0 X10 Y10
   G0 X20 Y20
   G0 X30 Y30
   ```
   All commands should queue and execute sequentially.

4. **Test error handling:**
   ```
   G0 X1000 Y1000
   ```
   Should return: `error: position out of bounds`

   ```
   INVALID
   ```
   Should return: `error: invalid command type`

## Coordinate System

- **Units**: Millimeters (mm)
- **Origin**: (0, 0) at home position
- **Absolute positioning**: All coordinates are absolute from origin
- **Work area**: Default 0-200mm in both X and Y (configurable in `config.h`)

## Calibration

To calibrate steps per mm:

1. Send: `G0 X10` (move 10mm)
2. Measure actual distance moved
3. Calculate: `STEPS_PER_MM_X = 10.0 * (10 / actual_distance_mm)`
4. Update `config.h` and recompile

Repeat for Y axis.

## Web Interface

The web interface continues to work alongside serial commands:
- Web interface uses **steps** as units
- Serial commands use **millimeters** as units
- Both coordinate systems are tracked independently but share the same physical motors

## Troubleshooting

**Motor doesn't move:**
- Check serial monitor for error messages
- Verify steps/mm calibration
- Check motor power settings

**Commands not executing:**
- Check if queue is full (max 20 commands)
- Verify state is IDLE (not PAUSED or ERROR)
- Send `M114` to check current position

**Position seems wrong:**
- Recalibrate steps/mm
- Send `H` to reset home position
- Check work area limits in `config.h`

## Example Python Script

```python
import serial
import time

# Connect to ESP32
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
time.sleep(2)  # Wait for ESP32 to initialize

def send_command(cmd):
    ser.write((cmd + "\n").encode())
    ser.flush()
    response = ser.readline().decode().strip()
    print(f"Sent: {cmd} -> {response}")
    return response == "ok"

# Test sequence
send_command("G0 X10 Y10")
send_command("D X5 Y5")
send_command("D X15 Y5")
send_command("G0 X0 Y0")
send_command("M114")

ser.close()
```

## Next Steps

Once basic commands work:
1. Calibrate steps/mm for your actual hardware
2. Test with a Python script for automated control
3. Implement image-to-dots conversion (external script)
4. Add pen servo control when hardware is ready
5. Add limit switches for proper homing

