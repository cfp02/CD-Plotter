#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESP32Servo.h>

// WiFi credentials - loaded from wifi_config.h
// Copy wifi_config.h.example to wifi_config.h and fill in your credentials
#include "wifi_config.h"

// Hardware configuration (selected via build flags)
#ifdef BOARD_ESP32_DEVKIT
    #include "hardware/pin_config_esp32dev.h"
#elif defined(BOARD_XIAO_ESP32S3)
    #include "hardware/pin_config_xiao_esp32s3.h"
#endif

// Driver selection (via build flags)
#ifdef DRIVER_TB6612
    #include "drivers/tb6612_driver.h"
    #include <AccelStepper.h>  // For step mode constants
#elif defined(DRIVER_TMC2209)
    #include "drivers/tmc2209_driver.h"
    #include <AccelStepper.h>  // Used by TMC2209Driver for motion control
    #include <HardwareSerial.h>  // For UART communication
#endif

// G-code interpreter modules
#include "config.h"
#include "gcode_parser.h"
#include "command_queue.h"
#include "motion_controller.h"
#include "plotter_state.h"
#include "serial_interface.h"

// Driver interface
#include "drivers/stepper_driver.h"

// Web server on port 80
WebServer server(80);

// Servo for pen control
Servo penServo;

// Preferences for persistent storage
Preferences preferences;

// Step mode: FULL4WIRE = full step, HALF4WIRE = half step
// Half step provides smoother motion and more precision but uses more power
// Default to HALF4WIRE (half step)
#ifdef DRIVER_TB6612
    int stepMode = AccelStepper::HALF4WIRE;  // Default to half step
#elif defined(DRIVER_TMC2209)
    // TMC2209 uses microstepping via UART, not hardware step mode
    // This will be handled differently when implemented
    int stepMode = 0;  // Placeholder
#endif

// Motor parameters (adjustable at runtime via web interface)
float maxSpeed = 1000.0;          // Maximum speed in steps per second
float acceleration = 1000.0;       // Acceleration in steps per second squared
int motorPowerRunning = 250;      // Power when moving (0-255, ~98%)
int motorPowerHolding = 120;      // Power when holding (0-255, 47%)

// Mechanical parameters (for calibration)
int fullStepsPerRev = 20;         // Full steps per revolution (CD drive steppers)
float linearTravelPerRev = 3.0;   // Linear travel per revolution in mm (3mm pitch)
// Microstepping is queried from driver (16 for TMC2209, 1-2 for TB6612)

// Pen control parameters (adjustable at runtime via web interface)
int penUpAngle = 0;               // Servo angle for pen up (0-180 degrees)
int penDownAngle = 90;            // Servo angle for pen down (0-180 degrees)
unsigned long dotDwellMs = 50;    // Dwell time for dot command (milliseconds)
int currentServoAngle = 0;        // Track current servo position to maintain it
unsigned long lastServoWrite = 0; // Track when servo was last written

// ====================================================================
// Driver Instances (selected via build flags)
// ====================================================================
#ifdef DRIVER_TB6612
    // TB6612FNG drivers using AccelStepper
    TB6612Driver stepper1_driver(stepMode,
                                  MOTOR1_AIN1, MOTOR1_AIN2,
                                  MOTOR1_BIN1, MOTOR1_BIN2,
                                  MOTOR1_PWMA, MOTOR1_PWMB,
                                  PWM_CHANNEL_M1A, PWM_CHANNEL_M1B);
    
    TB6612Driver stepper2_driver(stepMode,
                                  MOTOR2_AIN1, MOTOR2_AIN2,
                                  MOTOR2_BIN1, MOTOR2_BIN2,
                                  MOTOR2_PWMA, MOTOR2_PWMB,
                                  PWM_CHANNEL_M2A, PWM_CHANNEL_M2B);
#elif defined(DRIVER_TMC2209)
    // Shared UART for both TMC2209 drivers
    HardwareSerial tmcUart(TMC_UART_NUM);
    
    // TMC2209 drivers with shared UART
    TMC2209Driver stepper1_driver(MOTOR1_STEP, MOTOR1_DIR, MOTOR_EN,
                                   &tmcUart, TMC2209_ADDRESS_MOTOR1, TMC2209_RSENSE);
    
    TMC2209Driver stepper2_driver(MOTOR2_STEP, MOTOR2_DIR, MOTOR_EN,
                                   &tmcUart, TMC2209_ADDRESS_MOTOR2, TMC2209_RSENSE);
#endif

// Driver pointers for motion controller
StepperDriver* stepper1 = &stepper1_driver;
StepperDriver* stepper2 = &stepper2_driver;

// G-code interpreter system
CommandQueue commandQueue;
PlotterStateMachine stateMachine;
MotionController motionController(stepper1, stepper2, &penServo);
SerialInterface serialInterface(&commandQueue);

// Function to set motor power (delegates to drivers)
void setMotorPower(int power) {
  stepper1->setPower(power);
  stepper2->setPower(power);
}

// Function to update motor speed and acceleration settings
void updateMotorSettings() {
  stepper1->setMaxSpeed(maxSpeed);
  stepper1->setAcceleration(acceleration);
  stepper2->setMaxSpeed(maxSpeed);
  stepper2->setAcceleration(acceleration);
}

// Helper functions for mechanical parameter calculations
float calculateFullStepsPerMM() {
  if (linearTravelPerRev <= 0) return 0.0;
  return (float)fullStepsPerRev / linearTravelPerRev;
}

float calculateMicrostepsPerMM(char axis) {
  float fullStepsPerMM = calculateFullStepsPerMM();
  int microstepping = (axis == 'X') ? stepper1->getMicrostepping() : stepper2->getMicrostepping();
  return fullStepsPerMM * (float)microstepping;
}

// Function to change step mode (requires reinitializing steppers)
void changeStepMode(int newMode) {
  stepMode = newMode;
  #ifdef DRIVER_TB6612
    // Note: AccelStepper objects are already created, but the mode is set at construction
    // For a proper mode change, we'd need to recreate them, but that's complex
    // Instead, we'll just update the mode variable and note it in serial
    Serial.print("Step mode changed to: ");
    Serial.println((newMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
    Serial.println("NOTE: Step mode change requires restart to take full effect.");
  #elif defined(DRIVER_TMC2209)
    // TMC2209 microstepping is configured via UART, not hardware step mode
    Serial.println("TMC2209 microstepping configured via UART (not yet implemented)");
  #endif
}

// Function to generate HTML page
String getHTMLPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP32 Stepper Motor Control</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += "h1 { color: #333; }";
  html += ".motor-control { border: 2px solid #4CAF50; padding: 15px; margin: 15px 0; border-radius: 5px; }";
  html += ".motor-control h2 { margin-top: 0; color: #4CAF50; }";
  html += "input[type=\"number\"], input[type=\"text\"] { width: 150px; padding: 8px; margin: 5px; }";
  html += "button { padding: 10px 20px; margin: 5px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; }";
  html += "button:hover { background: #45a049; }";
  html += ".status { background: #e8f5e9; padding: 10px; margin: 10px 0; border-radius: 5px; }";
  html += ".jog-buttons { display: flex; gap: 5px; flex-wrap: wrap; }";
  html += ".jog-buttons button { flex: 1; min-width: 80px; }";
  html += ".settings { border: 2px solid #2196F3; padding: 15px; margin: 15px 0; border-radius: 5px; background: #e3f2fd; }";
  html += ".settings h2 { margin-top: 0; color: #2196F3; }";
  html += ".settings-row { display: flex; align-items: center; margin: 10px 0; gap: 10px; }";
  html += ".settings-row label { min-width: 150px; }";
  html += ".settings-row input { width: 100px; }";
  html += "</style></head><body>";
  html += "<div class=\"container\">";
  html += "<h1>ESP32 2D Plotter Control</h1>";
  
  // Status message area
  html += "<div id=\"statusMessage\" style=\"display: none; padding: 10px; margin: 10px 0; border-radius: 5px; background: #4CAF50; color: white; text-align: center; font-weight: bold;\"></div>";
  
  // Mechanical Parameters section
  html += "<div class=\"settings\">";
  html += "<h2>Mechanical Parameters</h2>";
  
  // Get microstepping from drivers
  int microsteppingX = stepper1->getMicrostepping();
  int microsteppingY = stepper2->getMicrostepping();
  float fullStepsPerMM = calculateFullStepsPerMM();
  float microstepsPerMM_X = calculateMicrostepsPerMM('X');
  float microstepsPerMM_Y = calculateMicrostepsPerMM('Y');
  
  html += "<div class=\"settings-row\">";
  html += "<label>Full Steps per Revolution:</label>";
  html += "<input type=\"number\" id=\"fullStepsPerRev\" value=\"" + String(fullStepsPerRev) + "\" min=\"1\" max=\"200\" step=\"1\">";
  html += "<button onclick=\"setFullStepsPerRev()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Linear Travel per Revolution (mm):</label>";
  html += "<input type=\"number\" id=\"linearTravelPerRev\" value=\"" + String(linearTravelPerRev, 2) + "\" min=\"0.1\" max=\"100\" step=\"0.1\">";
  html += "<button onclick=\"setLinearTravelPerRev()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\" style=\"background: #f5f5f5; padding: 10px; border-radius: 5px; margin: 10px 0;\">";
  html += "<div style=\"margin: 5px 0;\"><strong>Microstepping:</strong> X-axis: <span id=\"microsteppingX\">" + String(microsteppingX) + "x</span> | Y-axis: <span id=\"microsteppingY\">" + String(microsteppingY) + "x</span> (auto-detected)</div>";
  html += "<div style=\"margin: 5px 0;\"><strong>Calculated Full Steps/mm:</strong> " + String(fullStepsPerMM, 3) + "</div>";
  html += "<div style=\"margin: 5px 0;\"><strong>Calculated Microsteps/mm:</strong> X: " + String(microstepsPerMM_X, 3) + " | Y: " + String(microstepsPerMM_Y, 3) + "</div>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Full Steps/mm (X) - Override:</label>";
  html += "<input type=\"number\" id=\"fullStepsPerMM_X\" value=\"" + String(fullStepsPerMM, 3) + "\" min=\"0.1\" max=\"100\" step=\"0.001\">";
  html += "<button onclick=\"setFullStepsPerMM('X')\">Set</button>";
  html += "<span style=\"margin-left: 10px; color: #666; font-size: 0.9em;\">(Leave empty to use calculated value)</span>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Full Steps/mm (Y) - Override:</label>";
  html += "<input type=\"number\" id=\"fullStepsPerMM_Y\" value=\"" + String(fullStepsPerMM, 3) + "\" min=\"0.1\" max=\"100\" step=\"0.001\">";
  html += "<button onclick=\"setFullStepsPerMM('Y')\">Set</button>";
  html += "<span style=\"margin-left: 10px; color: #666; font-size: 0.9em;\">(Leave empty to use calculated value)</span>";
  html += "</div>";
  html += "</div>";
  
  // Work Area Display (read-only, calculated from step limits)
  html += "<div class=\"settings\">";
  html += "<h2>Work Area</h2>";
  html += "<div class=\"settings-row\" style=\"background: #e8f5e9; padding: 10px; border-radius: 5px;\">";
  html += "<div style=\"margin: 5px 0;\"><strong>X Range:</strong> <span id=\"workAreaX\">" + String(motionController.getMinX(), 1) + " to " + String(motionController.getMaxX(), 1) + " mm</span> (Width: " + String(motionController.getMaxX() - motionController.getMinX(), 1) + " mm)</div>";
  html += "<div style=\"margin: 5px 0;\"><strong>Y Range:</strong> <span id=\"workAreaY\">" + String(motionController.getMinY(), 1) + " to " + String(motionController.getMaxY(), 1) + " mm</span> (Height: " + String(motionController.getMaxY() - motionController.getMinY(), 1) + " mm)</div>";
  html += "<div style=\"margin: 5px 0; font-size: 0.9em; color: #666;\">Work area is calculated from step limits and steps/mm. Use calibration below to set limits.</div>";
  html += "</div>";
  html += "</div>";
  html += "<div class=\"settings-row\" style=\"margin-top: 15px; padding-top: 15px; border-top: 1px solid #ccc;\">";
  html += "<label>Axis Direction:</label>";
  html += "<div style=\"display: inline-block; margin-left: 10px;\">";
  html += "<span style=\"margin-right: 15px;\">X: <span id=\"xDir\" style=\"font-weight: bold; color: " + String(motionController.getInvertX() ? "#f44336" : "#4CAF50") + ";\">" + String(motionController.getInvertX() ? "← (inverted)" : "→ (normal)") + "</span></span>";
  html += "<button onclick=\"flipDirection('X')\" style=\"padding: 5px 15px; margin-left: 5px;\">Flip X</button>";
  html += "</div>";
  html += "<div style=\"display: inline-block; margin-left: 20px;\">";
  html += "<span style=\"margin-right: 15px;\">Y: <span id=\"yDir\" style=\"font-weight: bold; color: " + String(motionController.getInvertY() ? "#f44336" : "#4CAF50") + ";\">" + String(motionController.getInvertY() ? "↑ (inverted)" : "↓ (normal)") + "</span></span>";
  html += "<button onclick=\"flipDirection('Y')\" style=\"padding: 5px 15px; margin-left: 5px;\">Flip Y</button>";
  html += "</div>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0; padding: 10px; background: #fff3cd; border-radius: 5px; font-size: 0.9em;\">";
  html += "<strong>Direction Guide:</strong><br>";
  html += "• X-axis: → = positive (right), ← = negative (left)<br>";
  html += "• Y-axis: ↓ = positive (down), ↑ = negative (up)<br>";
  html += "• Inverted means the motor direction is reversed";
  html += "</div>";
  html += "<h2 style=\"margin-top: 20px;\">Motor Settings</h2>";
  html += "<div class=\"settings-row\">";
  html += "<label>Max Speed (steps/sec):</label>";
  html += "<input type=\"number\" id=\"maxSpeed\" value=\"" + String(maxSpeed, 0) + "\" min=\"1\" max=\"2000\" step=\"10\">";
  html += "<button onclick=\"setMaxSpeed()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Acceleration (steps/sec²):</label>";
  html += "<input type=\"number\" id=\"acceleration\" value=\"" + String(acceleration, 0) + "\" min=\"1\" max=\"2000\" step=\"5\">";
  html += "<button onclick=\"setAcceleration()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Power When Running (0-255):</label>";
  html += "<input type=\"number\" id=\"powerRunning\" value=\"" + String(motorPowerRunning) + "\" min=\"0\" max=\"255\" step=\"5\">";
  html += "<button onclick=\"setPowerRunning()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Power When Holding (0-255):</label>";
  html += "<input type=\"number\" id=\"powerHolding\" value=\"" + String(motorPowerHolding) + "\" min=\"0\" max=\"255\" step=\"5\">";
  html += "<button onclick=\"setPowerHolding()\">Set</button>";
  html += "</div>";
  html += "<h2 style=\"margin-top: 20px;\">Pen Control Settings</h2>";
  html += "<div class=\"settings-row\">";
  html += "<label>Pen Up Angle (0-180°):</label>";
  html += "<input type=\"number\" id=\"penUpAngle\" value=\"" + String(penUpAngle) + "\" min=\"0\" max=\"180\" step=\"1\">";
  html += "<button onclick=\"setPenUpAngle()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Pen Down Angle (0-180°):</label>";
  html += "<input type=\"number\" id=\"penDownAngle\" value=\"" + String(penDownAngle) + "\" min=\"0\" max=\"180\" step=\"1\">";
  html += "<button onclick=\"setPenDownAngle()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Dot Dwell Time (ms):</label>";
  html += "<input type=\"number\" id=\"dotDwellMs\" value=\"" + String(dotDwellMs) + "\" min=\"10\" max=\"1000\" step=\"10\">";
  html += "<button onclick=\"setDotDwellMs()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Test Pen:</label>";
  html += "<button onclick=\"testPen('up')\" style=\"background: #2196F3; margin-left: 10px;\">Pen Up</button>";
  html += "<button onclick=\"testPen('down')\" style=\"background: #4CAF50; margin-left: 5px;\">Pen Down</button>";
  html += "</div>";
  html += "<div class=\"settings-row\" style=\"margin-top: 10px;\">";
  html += "<label>Direct Servo Control (0-180°):</label>";
  html += "<input type=\"range\" id=\"servoAngle\" min=\"0\" max=\"180\" value=\"" + String(penUpAngle) + "\" style=\"width: 200px; margin: 0 10px;\" oninput=\"document.getElementById('servoAngleValue').textContent = this.value + '°'; clearTimeout(servoAngleTimeout); servoAngleTimeout = setTimeout(() => setServoAngle(this.value), 150);\">";
  html += "<span id=\"servoAngleValue\" style=\"font-weight: bold; min-width: 50px; display: inline-block;\">" + String(penUpAngle) + "°</span>";
  html += "<button onclick=\"setServoAngle(document.getElementById('servoAngle').value)\" style=\"margin-left: 10px;\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Step Mode:</label>";
  #ifdef DRIVER_TB6612
    // Set the default selected option based on current step mode
    String stepModeSelected = (stepMode == AccelStepper::HALF4WIRE) ? "selected" : "";
    String fullStepSelected = (stepMode == AccelStepper::FULL4WIRE) ? "selected" : "";
    html += "<select id=\"stepMode\" style=\"padding: 8px; margin: 5px; width: 150px;\">";
    html += "<option value=\"full\" " + fullStepSelected + ">Full Step</option>";
    html += "<option value=\"half\" " + stepModeSelected + ">Half Step</option>";
    html += "</select>";
    html += "<button onclick=\"setStepMode()\">Set</button>";
    html += "<span style=\"margin-left: 10px; color: #666; font-size: 0.9em;\">(Requires restart)</span>";
  #elif defined(DRIVER_TMC2209)
    html += "<span style=\"color: #666;\">TMC2209: Microstepping configured via UART (1/16 default)</span>";
  #endif
  html += "</div>";
  html += "</div>";
  
  // Motor 1 controls
  html += "<div class=\"motor-control\">";
  html += "<h2>Motor 1 (X-Axis)</h2>";
  html += "<div class=\"status\" id=\"status1\">Position: 0 | Target: 0 | Speed: 0 steps/sec</div>";
  html += "<div><label>Move to Position:</label><br>";
  html += "<input type=\"number\" id=\"pos1\" value=\"0\" step=\"10\">";
  html += "<button onclick=\"moveTo(1)\">Move To</button></div>";
  html += "<div><label>Relative Move (steps):</label><br>";
  html += "<input type=\"number\" id=\"rel1\" value=\"100\" step=\"10\">";
  html += "<button onclick=\"moveRelative(1)\">Move</button></div>";
  html += "<div><label>Jog Controls:</label>";
  html += "<div class=\"jog-buttons\">";
  html += "<button onclick=\"jog(1, -100)\">-100</button>";
  html += "<button onclick=\"jog(1, -10)\">-10</button>";
  html += "<button onclick=\"jog(1, -1)\">-1</button>";
  html += "<button onclick=\"stopMotor(1)\">Stop</button>";
  html += "<button onclick=\"jog(1, 1)\">+1</button>";
  html += "<button onclick=\"jog(1, 10)\">+10</button>";
  html += "<button onclick=\"jog(1, 100)\">+100</button>";
  html += "</div></div>";
  html += "<div><label>Set Home (Current Position = 0):</label>";
  html += "<button onclick=\"setHome(1)\">Set Home</button></div>";
  html += "</div>";
  
  // Motor 2 controls
  html += "<div class=\"motor-control\">";
  html += "<h2>Motor 2 (Y-Axis)</h2>";
  html += "<div class=\"status\" id=\"status2\">Position: 0 | Target: 0 | Speed: 0 steps/sec</div>";
  html += "<div><label>Move to Position:</label><br>";
  html += "<input type=\"number\" id=\"pos2\" value=\"0\" step=\"10\">";
  html += "<button onclick=\"moveTo(2)\">Move To</button></div>";
  html += "<div><label>Relative Move (steps):</label><br>";
  html += "<input type=\"number\" id=\"rel2\" value=\"100\" step=\"10\">";
  html += "<button onclick=\"moveRelative(2)\">Move</button></div>";
  html += "<div><label>Jog Controls:</label>";
  html += "<div class=\"jog-buttons\">";
  html += "<button onclick=\"jog(2, -100)\">-100</button>";
  html += "<button onclick=\"jog(2, -10)\">-10</button>";
  html += "<button onclick=\"jog(2, -1)\">-1</button>";
  html += "<button onclick=\"stopMotor(2)\">Stop</button>";
  html += "<button onclick=\"jog(2, 1)\">+1</button>";
  html += "<button onclick=\"jog(2, 10)\">+10</button>";
  html += "<button onclick=\"jog(2, 100)\">+100</button>";
  html += "</div></div>";
  html += "<div><label>Set Home (Current Position = 0):</label>";
  html += "<button onclick=\"setHome(2)\">Set Home</button></div>";
  html += "</div>";
  
  // Global controls
  html += "<div style=\"margin-top: 20px;\">";
  html += "<button onclick=\"stopAll()\" style=\"background: #f44336;\">Stop All Motors</button>";
  html += "<button onclick=\"homeAll()\" style=\"background: #2196F3;\">Home All (Move to 0,0)</button>";
  html += "</div>";
  
  // Simplified Calibration (Jog to Limits)
  html += "<div class=\"settings\" style=\"margin-top: 20px; background: #fff9c4;\">";
  html += "<h2>Calibration - Set Work Area Limits</h2>";
  html += "<div style=\"margin: 10px 0; padding: 10px; background: #fff; border-radius: 5px;\">";
  html += "<p style=\"margin: 5px 0;\"><strong>Instructions:</strong> Jog each axis to its physical limits and click the corresponding button to set that limit.</p>";
  html += "<div style=\"margin: 15px 0; padding: 15px; background: #e8f5e9; border-radius: 5px;\">";
  
  // X-Axis Calibration
  html += "<div style=\"margin: 10px 0; padding: 10px; background: #fff; border-radius: 5px;\">";
  html += "<h3 style=\"margin-top: 0;\">X-Axis</h3>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<strong>Current Step Position: <span id=\"xStepPos\">0</span></strong>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<button onclick=\"calJog('X', -1000)\" style=\"padding: 8px 15px; margin: 2px; background: #f44336;\">-1000</button>";
  html += "<button onclick=\"calJog('X', -100)\" style=\"padding: 8px 15px; margin: 2px; background: #ff9800;\">-100</button>";
  html += "<button onclick=\"calJog('X', -10)\" style=\"padding: 8px 15px; margin: 2px; background: #ffc107;\">-10</button>";
  html += "<button onclick=\"calJog('X', -1)\" style=\"padding: 8px 15px; margin: 2px; background: #ffeb3b;\">-1</button>";
  html += "<button onclick=\"calStop('X')\" style=\"padding: 8px 15px; margin: 2px; background: #666; color: white;\">STOP</button>";
  html += "<button onclick=\"calJog('X', 1)\" style=\"padding: 8px 15px; margin: 2px; background: #4CAF50;\">+1</button>";
  html += "<button onclick=\"calJog('X', 10)\" style=\"padding: 8px 15px; margin: 2px; background: #8BC34A;\">+10</button>";
  html += "<button onclick=\"calJog('X', 100)\" style=\"padding: 8px 15px; margin: 2px; background: #9CCC65;\">+100</button>";
  html += "<button onclick=\"calJog('X', 1000)\" style=\"padding: 8px 15px; margin: 2px; background: #AED581;\">+1000</button>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<button onclick=\"setStepLimit('X', 'min')\" style=\"padding: 8px 20px; background: #2196F3; color: white; margin-right: 10px;\">Set X Min (Left)</button>";
  html += "<button onclick=\"setStepLimit('X', 'max')\" style=\"padding: 8px 20px; background: #4CAF50; color: white;\">Set X Max (Right)</button>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0; font-size: 0.9em;\">";
  html += "<strong>X Limits:</strong> Min: <span id=\"xMinStep\">Not set</span> | Max: <span id=\"xMaxStep\">Not set</span>";
  html += "</div>";
  html += "</div>";
  
  // Y-Axis Calibration
  html += "<div style=\"margin: 10px 0; padding: 10px; background: #fff; border-radius: 5px;\">";
  html += "<h3 style=\"margin-top: 0;\">Y-Axis</h3>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<strong>Current Step Position: <span id=\"yStepPos\">0</span></strong>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<button onclick=\"calJog('Y', -1000)\" style=\"padding: 8px 15px; margin: 2px; background: #f44336;\">-1000</button>";
  html += "<button onclick=\"calJog('Y', -100)\" style=\"padding: 8px 15px; margin: 2px; background: #ff9800;\">-100</button>";
  html += "<button onclick=\"calJog('Y', -10)\" style=\"padding: 8px 15px; margin: 2px; background: #ffc107;\">-10</button>";
  html += "<button onclick=\"calJog('Y', -1)\" style=\"padding: 8px 15px; margin: 2px; background: #ffeb3b;\">-1</button>";
  html += "<button onclick=\"calStop('Y')\" style=\"padding: 8px 15px; margin: 2px; background: #666; color: white;\">STOP</button>";
  html += "<button onclick=\"calJog('Y', 1)\" style=\"padding: 8px 15px; margin: 2px; background: #4CAF50;\">+1</button>";
  html += "<button onclick=\"calJog('Y', 10)\" style=\"padding: 8px 15px; margin: 2px; background: #8BC34A;\">+10</button>";
  html += "<button onclick=\"calJog('Y', 100)\" style=\"padding: 8px 15px; margin: 2px; background: #9CCC65;\">+100</button>";
  html += "<button onclick=\"calJog('Y', 1000)\" style=\"padding: 8px 15px; margin: 2px; background: #AED581;\">+1000</button>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<button onclick=\"setStepLimit('Y', 'min')\" style=\"padding: 8px 20px; background: #2196F3; color: white; margin-right: 10px;\">Set Y Min (Bottom)</button>";
  html += "<button onclick=\"setStepLimit('Y', 'max')\" style=\"padding: 8px 20px; background: #4CAF50; color: white;\">Set Y Max (Top)</button>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0; font-size: 0.9em;\">";
  html += "<strong>Y Limits:</strong> Min: <span id=\"yMinStep\">Not set</span> | Max: <span id=\"yMaxStep\">Not set</span>";
  html += "</div>";
  html += "</div>";
  
  html += "<div style=\"margin: 15px 0; padding: 10px; background: #fff3cd; border-radius: 5px;\">";
  html += "<button onclick=\"applyCalibration()\" style=\"padding: 10px 30px; background: #4CAF50; color: white; font-size: 1.1em; font-weight: bold;\">Apply Calibration</button>";
  html += "<p style=\"margin: 10px 0 0 0; font-size: 0.9em;\">This will calculate the work area in mm from the step limits and steps/mm settings above.</p>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  
  // 2D Interactive Canvas
  html += "<div class=\"settings\" style=\"margin-top: 20px;\">";
  html += "<h2>2D Plotter Board</h2>";
  html += "<div style=\"text-align: center; margin: 10px 0;\">";
  html += "<canvas id=\"plotterCanvas\" style=\"border: 2px solid #333; background: #f9f9f9; cursor: crosshair; max-width: 100%;\"></canvas>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0; text-align: center;\">";
  html += "<label>Click Mode: </label>";
  html += "<select id=\"clickMode\" style=\"padding: 5px; margin: 0 10px;\">";
  html += "<option value=\"rapid\">Rapid Move (G0)</option>";
  html += "<option value=\"linear\">Linear Move (G1)</option>";
  html += "<option value=\"dot\">Dot (D)</option>";
  html += "</select>";
  html += "<button onclick=\"homePlotter()\" style=\"background: #2196F3; margin-left: 10px;\">Move to Home</button>";
  html += "<button onclick=\"setHomePosition()\" style=\"background: #FF9800; margin-left: 10px;\">Set Home Here</button>";
  html += "<button onclick=\"clearCanvas()\" style=\"margin-left: 10px;\">Clear Path</button>";
  html += "</div>";
  html += "<div class=\"status\" id=\"canvasStatus\" style=\"text-align: center;\">Click on the canvas to move the plotter</div>";
  html += "</div>";
  
  // G-code command interface
  html += "<div class=\"settings\" style=\"margin-top: 20px;\">";
  html += "<h2>G-code Commands</h2>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<label>Send G-code command:</label><br>";
  html += "<input type=\"text\" id=\"gcodeCommand\" placeholder=\"G0 X10 Y10\" style=\"width: 300px; padding: 8px; margin: 5px 0;\">";
  html += "<button onclick=\"sendGcode()\">Send</button>";
  html += "</div>";
  html += "<div style=\"margin: 10px 0;\">";
  html += "<label>Send multiple commands (one per line):</label><br>";
  html += "<textarea id=\"gcodeMulti\" placeholder=\"G0 X10 Y10&#10;G0 X20 Y20&#10;D X15 Y15\" style=\"width: 100%; height: 100px; padding: 8px; margin: 5px 0; font-family: monospace;\"></textarea>";
  html += "<button onclick=\"sendGcodeMulti()\">Send All</button>";
  html += "</div>";
  html += "<div class=\"status\" id=\"gcodeStatus\" style=\"margin-top: 10px;\">Ready</div>";
  html += "<div style=\"margin-top: 10px; font-size: 0.9em; color: #666;\">";
  html += "<strong>Commands:</strong> G0/G1 (move), D (dot), P0/P1 (pen), H (home), M114 (position), ! (stop), ~ (resume)";
  html += "</div>";
  html += "</div></div>";
  
  // JavaScript
  html += "<script>";
  html += "function showStatus(message, isError = false) {";
  html += "const statusEl = document.getElementById('statusMessage');";
  html += "statusEl.textContent = message;";
  html += "statusEl.style.background = isError ? '#f44336' : '#4CAF50';";
  html += "statusEl.style.display = 'block';";
  html += "setTimeout(() => { statusEl.style.display = 'none'; }, 3000);";
  html += "}";
  html += "function moveTo(motor) {";
  html += "const pos = document.getElementById('pos' + motor).value;";
  html += "fetch('/move?motor=' + motor + '&pos=' + pos);";
  html += "}";
  html += "function moveRelative(motor) {";
  html += "const steps = document.getElementById('rel' + motor).value;";
  html += "fetch('/moverel?motor=' + motor + '&steps=' + steps);";
  html += "}";
  html += "function jog(motor, steps) {";
  html += "fetch('/moverel?motor=' + motor + '&steps=' + steps);";
  html += "}";
  html += "function stopMotor(motor) {";
  html += "fetch('/stop?motor=' + motor);";
  html += "}";
  html += "function stopAll() {";
  html += "fetch('/stopall');";
  html += "}";
  html += "function setHome(motor) {";
  html += "fetch('/sethome?motor=' + motor);";
  html += "}";
  html += "function homeAll() {";
  html += "fetch('/homeall');";
  html += "}";
  html += "function setMaxSpeed() {";
  html += "const speed = document.getElementById('maxSpeed').value;";
  html += "fetch('/setspeed?speed=' + speed).then(() => showStatus('Max Speed set to ' + speed + ' steps/sec'));";
  html += "}";
  html += "function setAcceleration() {";
  html += "const accel = document.getElementById('acceleration').value;";
  html += "fetch('/setaccel?accel=' + accel).then(() => showStatus('Acceleration set to ' + accel + ' steps/sec²'));";
  html += "}";
  html += "function setPowerRunning() {";
  html += "const power = document.getElementById('powerRunning').value;";
  html += "fetch('/setpower?type=running&power=' + power).then(() => showStatus('Running power set to ' + power));";
  html += "}";
  html += "function setPowerHolding() {";
  html += "const power = document.getElementById('powerHolding').value;";
  html += "fetch('/setpower?type=holding&power=' + power).then(() => showStatus('Holding power set to ' + power));";
  html += "}";
  html += "function setPenUpAngle() {";
  html += "const angle = document.getElementById('penUpAngle').value;";
  html += "fetch('/setpenangle?state=up&angle=' + angle).then(() => showStatus('Pen up angle set to ' + angle + '°'));";
  html += "}";
  html += "function setPenDownAngle() {";
  html += "const angle = document.getElementById('penDownAngle').value;";
  html += "fetch('/setpenangle?state=down&angle=' + angle).then(() => showStatus('Pen down angle set to ' + angle + '°'));";
  html += "}";
  html += "function setDotDwellMs() {";
  html += "const ms = document.getElementById('dotDwellMs').value;";
  html += "fetch('/setdotdwell?ms=' + ms).then(() => showStatus('Dot dwell time set to ' + ms + ' ms'));";
  html += "}";
  html += "function testPen(state) {";
  html += "fetch('/testpen?state=' + state).then(() => {";
  html += "showStatus('Pen moved to ' + state + ' position');";
  html += "});";
  html += "}";
  html += "let servoAngleTimeout = null;";
  html += "function setServoAngle(angle) {";
  html += "if (servoAngleTimeout) clearTimeout(servoAngleTimeout);";
  html += "fetch('/setservoangle?angle=' + angle).then(r => r.text()).then(result => {";
  html += "if (result === 'ok') {";
  html += "showStatus('Servo set to ' + angle + '°');";
  html += "} else {";
  html += "showStatus('Error: ' + result, true);";
  html += "}";
  html += "});";
  html += "}";
  html += "function setStepMode() {";
  html += "const mode = document.getElementById('stepMode').value;";
  html += "fetch('/setstepmode?mode=' + mode).then(() => showStatus('Step mode set to ' + mode + '. Please restart ESP32 for changes to take effect.'));";
  html += "}";
  // Mechanical Parameters functions
  html += "function setFullStepsPerRev() {";
  html += "const value = parseInt(document.getElementById('fullStepsPerRev').value);";
  html += "fetch('/setfullstepsperrev?value=' + value).then(() => {";
  html += "showStatus('Full steps per revolution set to ' + value);";
  html += "location.reload();";
  html += "});";
  html += "}";
  html += "function setLinearTravelPerRev() {";
  html += "const value = parseFloat(document.getElementById('linearTravelPerRev').value);";
  html += "fetch('/setlineartravelperrev?value=' + value).then(() => {";
  html += "showStatus('Linear travel per revolution set to ' + value + ' mm');";
  html += "location.reload();";
  html += "});";
  html += "}";
  html += "function setFullStepsPerMM(axis) {";
  html += "const value = parseFloat(document.getElementById('fullStepsPerMM_' + axis).value);";
  html += "fetch('/setfullstepspermm?axis=' + axis + '&value=' + value).then(() => {";
  html += "showStatus('Full steps/mm (' + axis + ') set to ' + value);";
  html += "location.reload();";
  html += "});";
  html += "}";
  
  // Simplified calibration functions
  html += "function calJog(axis, steps) {";
  html += "const motor = (axis === 'X') ? 1 : 2;";
  html += "fetch('/moverel?motor=' + motor + '&steps=' + steps);";
  html += "}";
  html += "function calStop(axis) {";
  html += "const motor = (axis === 'X') ? 1 : 2;";
  html += "fetch('/stop?motor=' + motor);";
  html += "}";
  html += "function setStepLimit(axis, limit) {";
  html += "fetch('/status').then(r => r.json()).then(data => {";
  html += "const motor = (axis === 'X') ? 1 : 2;";
  html += "const stepPos = (motor === 1) ? data.motor1.position : data.motor2.position;";
  html += "fetch('/setsteplimit?axis=' + axis + '&limit=' + limit + '&steps=' + stepPos).then(() => {";
  html += "showStatus(axis + ' ' + limit + ' set to ' + stepPos + ' steps');";
  html += "updateStepLimits();";
  html += "});";
  html += "});";
  html += "}";
  html += "function applyCalibration() {";
  html += "fetch('/applycalibration').then(r => r.text()).then(result => {";
  html += "if (result === 'ok') {";
  html += "showStatus('Calibration applied! Work area calculated from step limits.');";
  html += "location.reload();";
  html += "} else {";
  html += "showStatus('Error: ' + result, true);";
  html += "}";
  html += "});";
  html += "}";
  html += "function updateStepLimits() {";
  html += "fetch('/getsteplimits').then(r => r.json()).then(data => {";
  html += "document.getElementById('xMinStep').textContent = data.xMin !== 0 ? data.xMin + ' steps' : 'Not set';";
  html += "document.getElementById('xMaxStep').textContent = data.xMax !== 0 ? data.xMax + ' steps' : 'Not set';";
  html += "document.getElementById('yMinStep').textContent = data.yMin !== 0 ? data.yMin + ' steps' : 'Not set';";
  html += "document.getElementById('yMaxStep').textContent = data.yMax !== 0 ? data.yMax + ' steps' : 'Not set';";
  html += "});";
  html += "}";
  html += "setInterval(function() {";
  html += "fetch('/status').then(r => r.json()).then(data => {";
  html += "document.getElementById('xStepPos').textContent = data.motor1.position;";
  html += "document.getElementById('yStepPos').textContent = data.motor2.position;";
  html += "});";
  html += "}, 200);";
  html += "updateStepLimits();";
  html += "function flipDirection(axis) {";
  html += "fetch('/flipdirection?axis=' + axis).then(() => {";
  html += "fetch('/getdirection').then(r => r.json()).then(data => {";
  html += "const xEl = document.getElementById('xDir');";
  html += "const yEl = document.getElementById('yDir');";
  html += "xEl.textContent = data.invertX ? '← (inverted)' : '→ (normal)';";
  html += "xEl.style.color = data.invertX ? '#f44336' : '#4CAF50';";
  html += "yEl.textContent = data.invertY ? '↑ (inverted)' : '↓ (normal)';";
  html += "yEl.style.color = data.invertY ? '#f44336' : '#4CAF50';";
  html += "showStatus('Direction flipped for ' + axis + ' axis');";
  html += "});";
  html += "});";
  html += "}";
  html += "let canvas, ctx, canvasWidth = 600, canvasHeight = 600;";
  html += "let pathPoints = [];";
  html += "function initCanvas() {";
  html += "canvas = document.getElementById('plotterCanvas');";
  html += "ctx = canvas.getContext('2d');";
  html += "canvasWidth = Math.min(600, window.innerWidth - 60);";
  html += "canvasHeight = canvasWidth;";
  html += "canvas.width = canvasWidth;";
  html += "canvas.height = canvasHeight;";
  html += "updateCanvas();";
  html += "canvas.addEventListener('click', function(e) {";
  html += "const rect = canvas.getBoundingClientRect();";
  html += "const x = e.clientX - rect.left;";
  html += "const y = e.clientY - rect.top;";
  html += "const mode = document.getElementById('clickMode').value;";
  html += "fetch('/getworkarea').then(r => r.json()).then(data => {";
  html += "const mmX = data.minX + (x / canvasWidth) * (data.maxX - data.minX);";
  html += "const mmY = data.maxY - (y / canvasHeight) * (data.maxY - data.minY);";
  html += "let cmd = '';";
  html += "if (mode === 'rapid') cmd = 'G0 X' + mmX.toFixed(2) + ' Y' + mmY.toFixed(2);";
  html += "else if (mode === 'linear') cmd = 'G1 X' + mmX.toFixed(2) + ' Y' + mmY.toFixed(2);";
  html += "else if (mode === 'dot') cmd = 'D X' + mmX.toFixed(2) + ' Y' + mmY.toFixed(2);";
  html += "document.getElementById('gcodeCommand').value = cmd;";
  html += "sendGcode();";
  html += "pathPoints.push({x: x, y: y});";
  html += "updateCanvas();";
  html += "});";
  html += "});";
  html += "}";
  html += "function updateCanvas() {";
  html += "if (!canvas || !ctx) return;";
  html += "ctx.clearRect(0, 0, canvasWidth, canvasHeight);";
  html += "fetch('/getworkarea').then(r => r.json()).then(data => {";
  html += "ctx.strokeStyle = '#333';";
  html += "ctx.lineWidth = 1;";
  html += "ctx.strokeRect(0, 0, canvasWidth, canvasHeight);";
  html += "ctx.font = 'bold 16px Arial';";
  html += "ctx.fillStyle = '#1976D2';";
  html += "ctx.strokeStyle = '#fff';";
  html += "ctx.lineWidth = 3;";
  html += "ctx.textAlign = 'center';";
  html += "ctx.textBaseline = 'middle';";
  html += "ctx.strokeText('X+ (right)', canvasWidth - 50, 25);";
  html += "ctx.fillText('X+ (right)', canvasWidth - 50, 25);";
  html += "ctx.strokeText('X- (left)', 50, 25);";
  html += "ctx.fillText('X- (left)', 50, 25);";
  html += "ctx.save();";
  html += "ctx.translate(25, canvasHeight - 25);";
  html += "ctx.rotate(-Math.PI / 2);";
  html += "ctx.strokeText('Y+ (down)', 0, 0);";
  html += "ctx.fillText('Y+ (down)', 0, 0);";
  html += "ctx.restore();";
  html += "ctx.save();";
  html += "ctx.translate(canvasWidth - 25, 25);";
  html += "ctx.rotate(-Math.PI / 2);";
  html += "ctx.strokeText('Y- (up)', 0, 0);";
  html += "ctx.fillText('Y- (up)', 0, 0);";
  html += "ctx.restore();";
  html += "ctx.textAlign = 'left';";
  html += "ctx.textBaseline = 'top';";
  html += "ctx.lineWidth = 1;";
  html += "ctx.font = '10px Arial';";
  html += "ctx.fillStyle = '#666';";
  html += "ctx.textAlign = 'left';";
  html += "ctx.textBaseline = 'top';";
  html += "ctx.fillText('Min X: ' + data.minX.toFixed(1) + 'mm', 5, canvasHeight - 50);";
  html += "ctx.fillText('Max X: ' + data.maxX.toFixed(1) + 'mm', 5, canvasHeight - 35);";
  html += "ctx.fillText('Min Y: ' + data.minY.toFixed(1) + 'mm', 5, canvasHeight - 20);";
  html += "ctx.fillText('Max Y: ' + data.maxY.toFixed(1) + 'mm', 5, canvasHeight - 5);";
  html += "ctx.textAlign = 'right';";
  html += "ctx.fillText('Width: ' + (data.maxX - data.minX).toFixed(1) + 'mm', canvasWidth - 5, canvasHeight - 50);";
  html += "ctx.fillText('Height: ' + (data.maxY - data.minY).toFixed(1) + 'mm', canvasWidth - 5, canvasHeight - 35);";
  html += "ctx.strokeStyle = '#ccc';";
  html += "ctx.setLineDash([5, 5]);";
  html += "for (let i = 1; i < 10; i++) {";
  html += "const x = (i / 10) * canvasWidth;";
  html += "const y = (i / 10) * canvasHeight;";
  html += "ctx.beginPath();";
  html += "ctx.moveTo(x, 0);";
  html += "ctx.lineTo(x, canvasHeight);";
  html += "ctx.stroke();";
  html += "ctx.beginPath();";
  html += "ctx.moveTo(0, y);";
  html += "ctx.lineTo(canvasWidth, y);";
  html += "ctx.stroke();";
  html += "}";
  html += "ctx.setLineDash([]);";
  html += "fetch('/getposition').then(r => r.json()).then(pos => {";
  html += "const px = ((pos.x - data.minX) / (data.maxX - data.minX)) * canvasWidth;";
  html += "const py = ((data.maxY - pos.y) / (data.maxY - data.minY)) * canvasHeight;";
  html += "ctx.fillStyle = '#4CAF50';";
  html += "ctx.beginPath();";
  html += "ctx.arc(px, py, 8, 0, 2 * Math.PI);";
  html += "ctx.fill();";
  html += "ctx.strokeStyle = '#2E7D32';";
  html += "ctx.lineWidth = 2;";
  html += "ctx.stroke();";
  html += "});";
  html += "ctx.strokeStyle = '#2196F3';";
  html += "ctx.lineWidth = 2;";
  html += "if (pathPoints.length > 1) {";
  html += "ctx.beginPath();";
  html += "ctx.moveTo(pathPoints[0].x, pathPoints[0].y);";
  html += "for (let i = 1; i < pathPoints.length; i++) {";
  html += "ctx.lineTo(pathPoints[i].x, pathPoints[i].y);";
  html += "}";
  html += "ctx.stroke();";
  html += "}";
  html += "pathPoints.forEach((p, i) => {";
  html += "ctx.fillStyle = i === 0 ? '#f44336' : '#2196F3';";
  html += "ctx.beginPath();";
  html += "ctx.arc(p.x, p.y, 4, 0, 2 * Math.PI);";
  html += "ctx.fill();";
  html += "});";
  html += "});";
  html += "}";
  html += "function clearCanvas() {";
  html += "pathPoints = [];";
  html += "updateCanvas();";
  html += "}";
  html += "function homePlotter() {";
  html += "document.getElementById('canvasStatus').textContent = 'Moving to home position...';";
  html += "fetch('/gcode?cmd=H')";
  html += ".then(response => response.text())";
  html += ".then(data => {";
  html += "if (data === 'ok' || data === 'queued') {";
  html += "document.getElementById('canvasStatus').textContent = 'Moving to home...';";
  html += "setTimeout(() => { updateCanvas(); document.getElementById('canvasStatus').textContent = 'At home position'; }, 500);";
  html += "} else {";
  html += "document.getElementById('canvasStatus').textContent = 'Error: ' + data;";
  html += "}";
  html += "});";
  html += "}";
  html += "function setHomePosition() {";
  html += "document.getElementById('canvasStatus').textContent = 'Setting current position as home...';";
  html += "fetch('/sethomeposition')";
  html += ".then(response => response.text())";
  html += ".then(data => {";
  html += "if (data === 'ok') {";
  html += "document.getElementById('canvasStatus').textContent = 'Home position set! Work area updated.';";
  html += "setTimeout(() => {";
  html += "location.reload();";
  html += "}, 1000);";
  html += "} else {";
  html += "document.getElementById('canvasStatus').textContent = 'Error: ' + data;";
  html += "}";
  html += "});";
  html += "}";
  html += "setInterval(updateCanvas, 1000);";
  html += "function sendGcode() {";
  html += "const cmd = document.getElementById('gcodeCommand').value.trim();";
  html += "if (!cmd) return;";
  html += "document.getElementById('gcodeStatus').textContent = 'Sending: ' + cmd;";
  html += "fetch('/gcode?cmd=' + encodeURIComponent(cmd))";
  html += ".then(response => response.text())";
  html += ".then(data => {";
  html += "document.getElementById('gcodeStatus').textContent = 'Response: ' + data;";
  html += "if (data === 'ok') {";
  html += "document.getElementById('gcodeCommand').value = '';";
  html += "}";
  html += "});";
  html += "}";
  html += "function sendGcodeMulti() {";
  html += "const cmds = document.getElementById('gcodeMulti').value.trim().split('\\n');";
  html += "if (cmds.length === 0 || (cmds.length === 1 && cmds[0] === '')) return;";
  html += "document.getElementById('gcodeStatus').textContent = 'Sending ' + cmds.length + ' commands...';";
  html += "let sent = 0;";
  html += "cmds.forEach((cmd, index) => {";
  html += "cmd = cmd.trim();";
  html += "if (cmd) {";
  html += "setTimeout(() => {";
  html += "fetch('/gcode?cmd=' + encodeURIComponent(cmd))";
  html += ".then(response => response.text())";
  html += ".then(data => {";
  html += "sent++;";
  html += "if (sent === cmds.filter(c => c.trim()).length) {";
  html += "document.getElementById('gcodeStatus').textContent = 'All commands sent (' + sent + ' commands)';";
  html += "document.getElementById('gcodeMulti').value = '';";
  html += "}";
  html += "});";
  html += "}, index * 50);";  // 50ms delay between commands
  html += "}";
  html += "});";
  html += "}";
  html += "setInterval(function() {";
  html += "fetch('/status').then(response => response.json()).then(data => {";
  html += "document.getElementById('status1').textContent = 'Position: ' + data.motor1.position + ' | Target: ' + data.motor1.target + ' | Speed: ' + data.motor1.speed.toFixed(1) + ' steps/sec';";
  html += "document.getElementById('status2').textContent = 'Position: ' + data.motor2.position + ' | Target: ' + data.motor2.target + ' | Speed: ' + data.motor2.speed.toFixed(1) + ' steps/sec';";
  html += "});";
  html += "}, 500);";
  html += "window.onload = function() { initCanvas(); };";
  html += "</script></body></html>";
  
  return html;
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed!");
  }
}

void handleRoot() {
  server.send(200, "text/html", getHTMLPage());
}

void handleMove() {
  if (server.hasArg("motor") && server.hasArg("pos")) {
    int motor = server.arg("motor").toInt();
    long pos = server.arg("pos").toInt();
    
    Serial.print("Move command: Motor ");
    Serial.print(motor);
    Serial.print(" to position ");
    Serial.println(pos);
    
    if (motor == 1) {
      stepper1->moveTo(pos);
      Serial.print("Motor 1: current=");
      Serial.print(stepper1->currentPosition());
      Serial.print(" target=");
      Serial.println(stepper1->targetPosition());
    } else if (motor == 2) {
      stepper2->moveTo(pos);
      Serial.print("Motor 2: current=");
      Serial.print(stepper2->currentPosition());
      Serial.print(" target=");
      Serial.println(stepper2->targetPosition());
    } else {
      server.send(400, "text/plain", "Invalid motor number");
      return;
    }
    
    // Ensure motor is enabled
    if (motor == 1) {
      stepper1->enable();
    } else {
      stepper2->enable();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleMoveRel() {
  if (server.hasArg("motor") && server.hasArg("steps")) {
    int motor = server.arg("motor").toInt();
    long steps = server.arg("steps").toInt();
    
    Serial.print("Move relative: Motor ");
    Serial.print(motor);
    Serial.print(" by ");
    Serial.print(steps);
    Serial.println(" steps");
    
    if (motor == 1) {
      stepper1->move(steps);
      Serial.print("Motor 1: current=");
      Serial.print(stepper1->currentPosition());
      Serial.print(" target=");
      Serial.println(stepper1->targetPosition());
    } else if (motor == 2) {
      stepper2->move(steps);
      Serial.print("Motor 2: current=");
      Serial.print(stepper2->currentPosition());
      Serial.print(" target=");
      Serial.println(stepper2->targetPosition());
    } else {
      server.send(400, "text/plain", "Invalid motor number");
      return;
    }
    
    // Ensure motor is enabled
    if (motor == 1) {
      stepper1->enable();
    } else {
      stepper2->enable();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleStop() {
  if (server.hasArg("motor")) {
    int motor = server.arg("motor").toInt();
    
    if (motor == 1) {
      stepper1->stop();
    } else if (motor == 2) {
      stepper2->stop();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleStopAll() {
  stepper1->stop();
  stepper2->stop();
  server.send(200, "text/plain", "OK");
}

void handleSetHome() {
  if (server.hasArg("motor")) {
    int motor = server.arg("motor").toInt();
    
    if (motor == 1) {
      stepper1->setCurrentPosition(0);
    } else if (motor == 2) {
      stepper2->setCurrentPosition(0);
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleHomeAll() {
  Serial.println("Home All: Moving both motors to position 0");
  
  // Enable both motors
  stepper1->enable();
  stepper2->enable();
  
  // Move both to home (0,0)
  stepper1->moveTo(0);
  stepper2->moveTo(0);
  
  Serial.print("Motor 1: current=");
  Serial.print(stepper1->currentPosition());
  Serial.print(" target=");
  Serial.println(stepper1->targetPosition());
  Serial.print("Motor 2: current=");
  Serial.print(stepper2->currentPosition());
  Serial.print(" target=");
  Serial.println(stepper2->targetPosition());
  
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"motor1\":{";
  json += "\"position\":" + String(stepper1->currentPosition()) + ",";
  json += "\"target\":" + String(stepper1->targetPosition()) + ",";
  json += "\"speed\":" + String(stepper1->speed());
  json += "},";
  json += "\"motor2\":{";
  json += "\"position\":" + String(stepper2->currentPosition()) + ",";
  json += "\"target\":" + String(stepper2->targetPosition()) + ",";
  json += "\"speed\":" + String(stepper2->speed());
  json += "}";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSetSpeed() {
  if (server.hasArg("speed")) {
    maxSpeed = server.arg("speed").toFloat();
    if (maxSpeed < 1.0) maxSpeed = 1.0;
    if (maxSpeed > 2000.0) maxSpeed = 2000.0;
    updateMotorSettings();
    
    // Save to preferences
    preferences.begin("motor", false);
    preferences.putFloat("maxSpeed", maxSpeed);
    preferences.end();
    
    Serial.print("Max speed set to: ");
    Serial.println(maxSpeed);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetAcceleration() {
  if (server.hasArg("accel")) {
    acceleration = server.arg("accel").toFloat();
    if (acceleration < 1.0) acceleration = 1.0;
    if (acceleration > 2000.0) acceleration = 2000.0;
    updateMotorSettings();
    
    // Save to preferences
    preferences.begin("motor", false);
    preferences.putFloat("acceleration", acceleration);
    preferences.end();
    
    Serial.print("Acceleration set to: ");
    Serial.println(acceleration);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetPower() {
  if (server.hasArg("type") && server.hasArg("power")) {
    int power = server.arg("power").toInt();
    if (power < 0) power = 0;
    if (power > 255) power = 255;
    
    String type = server.arg("type");
    preferences.begin("motor", false);
    if (type == "running") {
      motorPowerRunning = power;
      preferences.putInt("powerRunning", motorPowerRunning);
      Serial.print("Running power set to: ");
      Serial.println(motorPowerRunning);
    } else if (type == "holding") {
      motorPowerHolding = power;
      preferences.putInt("powerHolding", motorPowerHolding);
      Serial.print("Holding power set to: ");
      Serial.println(motorPowerHolding);
      // If motors are currently stopped, update power immediately
      if (!stepper1->isRunning() && !stepper2->isRunning()) {
        setMotorPower(motorPowerHolding);
      }
    }
    preferences.end();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetPenAngle() {
  if (server.hasArg("state") && server.hasArg("angle")) {
    String state = server.arg("state");
    int angle = server.arg("angle").toInt();
    
    if (angle < 0 || angle > 180) {
      server.send(400, "text/plain", "error: angle must be 0-180");
      return;
    }
    
    preferences.begin("pen", false);
    if (state == "up") {
      penUpAngle = angle;
      motionController.setPenUpAngle(angle);
      preferences.putInt("penUpAngle", angle);
      Serial.print("Pen up angle set to: ");
    } else if (state == "down") {
      penDownAngle = angle;
      motionController.setPenDownAngle(angle);
      preferences.putInt("penDownAngle", angle);
      Serial.print("Pen down angle set to: ");
    } else {
      preferences.end();
      server.send(400, "text/plain", "error: invalid state (use 'up' or 'down')");
      return;
    }
    preferences.end();
    Serial.println(angle);
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleSetDotDwell() {
  if (server.hasArg("ms")) {
    unsigned long ms = server.arg("ms").toInt();
    
    if (ms < 10 || ms > 1000) {
      server.send(400, "text/plain", "error: dwell time must be 10-1000 ms");
      return;
    }
    
    dotDwellMs = ms;
    motionController.setDotDwellMs(ms);
    
    // Save to preferences
    preferences.begin("pen", false);
    preferences.putULong("dotDwellMs", ms);
    preferences.end();
    
    Serial.print("Dot dwell time set to: ");
    Serial.print(ms);
    Serial.println(" ms");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameter");
  }
}

void handleSetServoAngle() {
  if (server.hasArg("angle")) {
    int angle = server.arg("angle").toInt();
    
    if (angle < 0 || angle > 180) {
      server.send(400, "text/plain", "error: angle must be 0-180");
      return;
    }
    
    // Enable manual mode to prevent motion controller interference
    motionController.setManualMode(true);
    
    // Ensure servo is attached (uses channels 0-3, motors use 4-7)
    if (!penServo.attached()) {
      penServo.attach(SERVO_PIN, 500, 2500);
    }
    
    // Update tracked angle
    currentServoAngle = angle;
    lastServoWrite = millis();
    
    // Write once - ESP32Servo maintains position automatically via continuous PWM
    penServo.write(angle);
    
    Serial.print("Direct servo control: Set to ");
    Serial.print(angle);
    Serial.println("° (manual mode enabled)");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing angle parameter");
  }
}

void handleTestPen() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    
    // Enable manual mode to prevent motion controller interference
    motionController.setManualMode(true);
    
    // Ensure servo is attached (in case it got detached somehow)
    if (!penServo.attached()) {
      penServo.attach(SERVO_PIN, 500, 2500);
    }
    
    int targetAngle;
    if (state == "up") {
      targetAngle = penUpAngle;
    } else if (state == "down") {
      targetAngle = penDownAngle;
    } else {
      server.send(400, "text/plain", "error: invalid state (use 'up' or 'down')");
      return;
    }
    
    // Update tracked angle
    currentServoAngle = targetAngle;
    lastServoWrite = millis();
    
    // Write once - ESP32Servo maintains position automatically via continuous PWM
    penServo.write(targetAngle);
    
    Serial.print("Test: Pen moved to ");
    Serial.print(state);
    Serial.print(" position (angle: ");
    Serial.print(targetAngle);
    Serial.println("°) (manual mode enabled)");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameter");
  }
}

void handleSetStepMode() {
  #ifdef DRIVER_TB6612
    if (server.hasArg("mode")) {
      String mode = server.arg("mode");
      int newMode;
      if (mode == "half") {
        newMode = AccelStepper::HALF4WIRE;
        Serial.println("Step mode set to HALF STEP (requires restart)");
      } else {
        newMode = AccelStepper::FULL4WIRE;
        Serial.println("Step mode set to FULL STEP (requires restart)");
      }
      
      // Save to preferences
      preferences.begin("motor", false);  // Read-write mode
      preferences.putInt("stepMode", newMode);
      preferences.end();
      
      stepMode = newMode;  // Update current value
      
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Bad Request");
    }
  #elif defined(DRIVER_TMC2209)
    // TMC2209 microstepping is configured via UART, not via web interface yet
    server.send(200, "text/plain", "TMC2209 microstepping not yet configurable via web");
  #endif
}

void handleGcode() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    cmd.trim();
    
    if (cmd.length() == 0) {
      server.send(400, "text/plain", "error: empty command");
      return;
    }
    
    // Parse the command
    Command parsedCmd = parseCommand(cmd);
    
    if (!parsedCmd.valid) {
      server.send(200, "text/plain", "error: " + parsedCmd.errorMsg);
      return;
    }
    
    // Check if queue is full
    if (commandQueue.isFull()) {
      server.send(200, "text/plain", "error: queue full");
      return;
    }
    
    // Check if we can accept commands
    if (!stateMachine.canAcceptCommands() && parsedCmd.type != '!' && parsedCmd.type != '~') {
      server.send(200, "text/plain", "error: plotter not ready (state: " + stateMachine.getStateString() + ")");
      return;
    }
    
    // Enqueue the command
    if (commandQueue.enqueue(parsedCmd)) {
      Serial.print("G-code queued via web: ");
      Serial.println(cmd);
      server.send(200, "text/plain", "ok");
    } else {
      server.send(200, "text/plain", "error: queue full");
    }
  } else {
    server.send(400, "text/plain", "error: missing command");
  }
}

void handleSetFullStepsPerRev() {
  if (server.hasArg("value")) {
    int value = server.arg("value").toInt();
    
    if (value < 1 || value > 200) {
      server.send(400, "text/plain", "error: invalid value (1-200)");
      return;
    }
    
    fullStepsPerRev = value;
    
    // Save to preferences
    preferences.begin("mechanical", false);
    preferences.putInt("fullStepsPerRev", fullStepsPerRev);
    preferences.end();
    
    // Recalculate and update motion controller
    float fullStepsPerMM = calculateFullStepsPerMM();
    float microstepsPerMM_X = calculateMicrostepsPerMM('X');
    float microstepsPerMM_Y = calculateMicrostepsPerMM('Y');
    motionController.setStepsPerMM(microstepsPerMM_X, microstepsPerMM_Y);
    
    Serial.print("Full steps per revolution set to: ");
    Serial.println(fullStepsPerRev);
    Serial.print("Recalculated: Full steps/mm = ");
    Serial.print(fullStepsPerMM, 3);
    Serial.print(", Microsteps/mm X = ");
    Serial.print(microstepsPerMM_X, 3);
    Serial.print(", Y = ");
    Serial.println(microstepsPerMM_Y, 3);
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameter");
  }
}

void handleSetLinearTravelPerRev() {
  if (server.hasArg("value")) {
    float value = server.arg("value").toFloat();
    
    if (value <= 0 || value > 100) {
      server.send(400, "text/plain", "error: invalid value (0.1-100)");
      return;
    }
    
    linearTravelPerRev = value;
    
    // Save to preferences
    preferences.begin("mechanical", false);
    preferences.putFloat("linearTravelPerRev", linearTravelPerRev);
    preferences.end();
    
    // Recalculate and update motion controller
    float fullStepsPerMM = calculateFullStepsPerMM();
    float microstepsPerMM_X = calculateMicrostepsPerMM('X');
    float microstepsPerMM_Y = calculateMicrostepsPerMM('Y');
    motionController.setStepsPerMM(microstepsPerMM_X, microstepsPerMM_Y);
    
    Serial.print("Linear travel per revolution set to: ");
    Serial.print(linearTravelPerRev, 2);
    Serial.println(" mm");
    Serial.print("Recalculated: Full steps/mm = ");
    Serial.print(fullStepsPerMM, 3);
    Serial.print(", Microsteps/mm X = ");
    Serial.print(microstepsPerMM_X, 3);
    Serial.print(", Y = ");
    Serial.println(microstepsPerMM_Y, 3);
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameter");
  }
}

void handleSetFullStepsPerMM() {
  if (server.hasArg("axis") && server.hasArg("value")) {
    String axis = server.arg("axis");
    float value = server.arg("value").toFloat();
    
    if (value <= 0 || value > 100) {
      server.send(400, "text/plain", "error: invalid value (0.1-100)");
      return;
    }
    
    // Save override to preferences
    preferences.begin("mechanical", false);
    if (axis == "X" || axis == "x") {
      preferences.putFloat("fullStepsPerMM_X_override", value);
      Serial.print("Full steps/mm X override set to: ");
    } else if (axis == "Y" || axis == "y") {
      preferences.putFloat("fullStepsPerMM_Y_override", value);
      Serial.print("Full steps/mm Y override set to: ");
    } else {
      preferences.end();
      server.send(400, "text/plain", "error: invalid axis");
      return;
    }
    preferences.end();
    
    // Recalculate microsteps/mm with override
    int microstepping = (axis == "X" || axis == "x") ? stepper1->getMicrostepping() : stepper2->getMicrostepping();
    float microstepsPerMM = value * (float)microstepping;
    
    // Update motion controller
    float x = motionController.getStepsPerMM_X();
    float y = motionController.getStepsPerMM_Y();
    if (axis == "X" || axis == "x") {
      x = microstepsPerMM;
    } else {
      y = microstepsPerMM;
    }
    motionController.setStepsPerMM(x, y);
    
    Serial.print(value, 3);
    Serial.print(" (microsteps/mm = ");
    Serial.print(microstepsPerMM, 3);
    Serial.println(")");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleSetWorkArea() {
  if (server.hasArg("minX") && server.hasArg("maxX") && server.hasArg("minY") && server.hasArg("maxY")) {
    float minX = server.arg("minX").toFloat();
    float maxX = server.arg("maxX").toFloat();
    float minY = server.arg("minY").toFloat();
    float maxY = server.arg("maxY").toFloat();
    
    if (minX >= maxX || minY >= maxY) {
      server.send(400, "text/plain", "error: invalid range");
      return;
    }
    
    motionController.setWorkArea(minX, maxX, minY, maxY);
    
    // Save to preferences
    preferences.begin("plotter", false);
    preferences.putFloat("minX", minX);
    preferences.putFloat("maxX", maxX);
    preferences.putFloat("minY", minY);
    preferences.putFloat("maxY", maxY);
    preferences.end();
    
    Serial.print("Work area updated: X[");
    Serial.print(minX, 1);
    Serial.print(",");
    Serial.print(maxX, 1);
    Serial.print("] Y[");
    Serial.print(minY, 1);
    Serial.print(",");
    Serial.print(maxY, 1);
    Serial.println("]");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleGetWorkArea() {
  String json = "{";
  json += "\"minX\":" + String(motionController.getMinX(), 2) + ",";
  json += "\"maxX\":" + String(motionController.getMaxX(), 2) + ",";
  json += "\"minY\":" + String(motionController.getMinY(), 2) + ",";
  json += "\"maxY\":" + String(motionController.getMaxY(), 2);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleGetPosition() {
  String json = "{";
  json += "\"x\":" + String(motionController.getX_mm(), 2) + ",";
  json += "\"y\":" + String(motionController.getY_mm(), 2);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleFlipDirection() {
  if (server.hasArg("axis")) {
    String axis = server.arg("axis");
    bool newState;
    
    if (axis == "X" || axis == "x") {
      newState = !motionController.getInvertX();
      motionController.setInvertX(newState);
    } else if (axis == "Y" || axis == "y") {
      newState = !motionController.getInvertY();
      motionController.setInvertY(newState);
    } else {
      server.send(400, "text/plain", "error: invalid axis");
      return;
    }
    
    // Save to preferences
    preferences.begin("plotter", false);
    if (axis == "X" || axis == "x") {
      preferences.putBool("invertX", newState);
    } else {
      preferences.putBool("invertY", newState);
    }
    preferences.end();
    
    Serial.print("Direction flipped for ");
    Serial.print(axis);
    Serial.print(" axis: ");
    Serial.println(newState ? "inverted" : "normal");
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing axis parameter");
  }
}

void handleGetDirection() {
  String json = "{";
  json += "\"invertX\":" + String(motionController.getInvertX() ? "true" : "false") + ",";
  json += "\"invertY\":" + String(motionController.getInvertY() ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSetHomePosition() {
  // Register current position as the new home (0, 0)
  // This ONLY sets the coordinate origin - does NOT change work area limits
  motionController.setCurrentPositionAsHome();
  
  // Work area limits remain unchanged - they are set during calibration
  // Just save that home has been set (for reference)
  Serial.print("Home position set at current location (0,0). Work area unchanged: X[");
  Serial.print(motionController.getMinX(), 1);
  Serial.print(",");
  Serial.print(motionController.getMaxX(), 1);
  Serial.print("] Y[");
  Serial.print(motionController.getMinY(), 1);
  Serial.print(",");
  Serial.print(motionController.getMaxY(), 1);
  Serial.println("]");
  
  server.send(200, "text/plain", "ok");
}

void handleSetWorkAreaLimit() {
  if (server.hasArg("axis") && server.hasArg("limit") && server.hasArg("value")) {
    String axisStr = server.arg("axis");
    String limitStr = server.arg("limit");
    float value = server.arg("value").toFloat();
    
    char axis = (axisStr == "X" || axisStr == "x") ? 'X' : 'Y';
    char limit = (limitStr == "min" || limitStr == "Min") ? 'M' : 'X';
    
    motionController.setWorkAreaLimit(axis, limit, value);
    
    // Save to preferences
    preferences.begin("plotter", false);
    preferences.putFloat("minX", motionController.getMinX());
    preferences.putFloat("maxX", motionController.getMaxX());
    preferences.putFloat("minY", motionController.getMinY());
    preferences.putFloat("maxY", motionController.getMaxY());
    preferences.end();
    
    Serial.print("Work area limit set: ");
    Serial.print(axis);
    Serial.print(" ");
    Serial.print(limitStr);
    Serial.print(" = ");
    Serial.println(value, 2);
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleSetStepLimit() {
  if (server.hasArg("axis") && server.hasArg("limit") && server.hasArg("steps")) {
    String axisStr = server.arg("axis");
    String limitStr = server.arg("limit");
    long steps = server.arg("steps").toInt();
    
    // Save step limits to preferences (in steps, not mm)
    preferences.begin("calibration", false);
    if (axisStr == "X" || axisStr == "x") {
      if (limitStr == "max") {
        preferences.putLong("xMaxSteps", steps);
        Serial.print("X-axis max steps set to: ");
      } else {
        preferences.putLong("xMinSteps", steps);
        Serial.print("X-axis min steps set to: ");
      }
    } else {
      if (limitStr == "max") {
        preferences.putLong("yMaxSteps", steps);
        Serial.print("Y-axis max steps set to: ");
      } else {
        preferences.putLong("yMinSteps", steps);
        Serial.print("Y-axis min steps set to: ");
      }
    }
    preferences.end();
    Serial.println(steps);
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleGetStepLimits() {
  preferences.begin("calibration", true);
  long xMinSteps = preferences.getLong("xMinSteps", 0);
  long xMaxSteps = preferences.getLong("xMaxSteps", 0);
  long yMinSteps = preferences.getLong("yMinSteps", 0);
  long yMaxSteps = preferences.getLong("yMaxSteps", 0);
  preferences.end();
  
  String json = "{";
  // Use 0 as sentinel value - JavaScript will check for 0 vs null
  json += "\"xMin\":" + String(xMinSteps) + ",";
  json += "\"xMax\":" + String(xMaxSteps) + ",";
  json += "\"yMin\":" + String(yMinSteps) + ",";
  json += "\"yMax\":" + String(yMaxSteps);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleApplyCalibration() {
  // Load step limits from preferences
  preferences.begin("calibration", true);
  long xMinSteps = preferences.getLong("xMinSteps", 0);
  long xMaxSteps = preferences.getLong("xMaxSteps", 0);
  long yMinSteps = preferences.getLong("yMinSteps", 0);
  long yMaxSteps = preferences.getLong("yMaxSteps", 0);
  preferences.end();
  
  // Check if limits are set
  if (xMinSteps == 0 && xMaxSteps == 0 && yMinSteps == 0 && yMaxSteps == 0) {
    server.send(400, "text/plain", "error: no step limits set. Please set limits first.");
    return;
  }
  
  // Ensure min < max (swap if needed)
  if (xMinSteps > xMaxSteps) { long temp = xMinSteps; xMinSteps = xMaxSteps; xMaxSteps = temp; }
  if (yMinSteps > yMaxSteps) { long temp = yMinSteps; yMinSteps = yMaxSteps; yMaxSteps = temp; }
  
  // Get microsteps/mm for each axis
  float microstepsPerMM_X = calculateMicrostepsPerMM('X');
  float microstepsPerMM_Y = calculateMicrostepsPerMM('Y');
  
  // Convert step limits to mm
  float minX_mm = xMinSteps / microstepsPerMM_X;
  float maxX_mm = xMaxSteps / microstepsPerMM_X;
  float minY_mm = yMinSteps / microstepsPerMM_Y;
  float maxY_mm = yMaxSteps / microstepsPerMM_Y;
  
  // Set work area
  motionController.setWorkArea(minX_mm, maxX_mm, minY_mm, maxY_mm);
  
  // Save work area to preferences
  preferences.begin("plotter", false);
  preferences.putFloat("minX", minX_mm);
  preferences.putFloat("maxX", maxX_mm);
  preferences.putFloat("minY", minY_mm);
  preferences.putFloat("maxY", maxY_mm);
  preferences.end();
  
  Serial.print("Calibration applied! Work area: X[");
  Serial.print(minX_mm, 1);
  Serial.print(",");
  Serial.print(maxX_mm, 1);
  Serial.print("] Y[");
  Serial.print(minY_mm, 1);
  Serial.print(",");
  Serial.print(maxY_mm, 1);
  Serial.print("] mm (from step limits: X[");
  Serial.print(xMinSteps);
  Serial.print(",");
  Serial.print(xMaxSteps);
  Serial.print("] Y[");
  Serial.print(yMinSteps);
  Serial.print(",");
  Serial.print(yMaxSteps);
  Serial.println("])");
  
  server.send(200, "text/plain", "ok");
}

void handleFinishCalibration() {
  // Load step limits from calibration
  preferences.begin("calibration", true);
  long xMinSteps = preferences.getLong("xMinSteps", 0);
  long xMaxSteps = preferences.getLong("xMaxSteps", 0);
  long yMinSteps = preferences.getLong("yMinSteps", 0);
  long yMaxSteps = preferences.getLong("yMaxSteps", 0);
  preferences.end();
  
  // Ensure min < max (swap if needed)
  if (xMinSteps > xMaxSteps) { long temp = xMinSteps; xMinSteps = xMaxSteps; xMaxSteps = temp; }
  if (yMinSteps > yMaxSteps) { long temp = yMinSteps; yMinSteps = yMaxSteps; yMaxSteps = temp; }
  
  // Get current steps/mm to convert to mm
  float stepsPerMM_X = motionController.getStepsPerMM_X();
  float stepsPerMM_Y = motionController.getStepsPerMM_Y();
  
  // Convert step limits to mm
  // Note: If Y uses negative values (e.g., -680 to 0), we preserve that
  float minX_mm = xMinSteps / stepsPerMM_X;
  float maxX_mm = xMaxSteps / stepsPerMM_X;
  float minY_mm = yMinSteps / stepsPerMM_Y;
  float maxY_mm = yMaxSteps / stepsPerMM_Y;
  
  // Set work area - these are the physical limits in mm
  motionController.setWorkArea(minX_mm, maxX_mm, minY_mm, maxY_mm);
  
  // Save step limits and work area to preferences
  preferences.begin("plotter", false);
  preferences.putFloat("minX", minX_mm);
  preferences.putFloat("maxX", maxX_mm);
  preferences.putFloat("minY", minY_mm);
  preferences.putFloat("maxY", maxY_mm);
  preferences.putLong("xMinSteps", xMinSteps);
  preferences.putLong("xMaxSteps", xMaxSteps);
  preferences.putLong("yMinSteps", yMinSteps);
  preferences.putLong("yMaxSteps", yMaxSteps);
  if (server.hasArg("motor1Axis")) {
    preferences.putString("motor1Axis", server.arg("motor1Axis"));
  }
  preferences.end();
  
  Serial.print("Calibration complete! Step limits: X[");
  Serial.print(xMinSteps);
  Serial.print(",");
  Serial.print(xMaxSteps);
  Serial.print("] Y[");
  Serial.print(yMinSteps);
  Serial.print(",");
  Serial.print(yMaxSteps);
  Serial.print("] | Work area (mm): X[");
  Serial.print(minX_mm, 1);
  Serial.print(",");
  Serial.print(maxX_mm, 1);
  Serial.print("] Y[");
  Serial.print(minY_mm, 1);
  Serial.print(",");
  Serial.print(maxY_mm, 1);
  Serial.println("]");
  
  server.send(200, "text/plain", "ok");
}

void handleCalibrateFromPhysical() {
  // Calibrate steps/mm based on measured physical size
  // Formula: steps/mm = (maxSteps - minSteps) / measuredSize_mm
  if (server.hasArg("axis") && server.hasArg("size")) {
    String axisStr = server.arg("axis");
    float measuredSize = server.arg("size").toFloat();
    
    if (measuredSize <= 0) {
      server.send(400, "text/plain", "error: size must be > 0");
      return;
    }
    
    // Load step limits from calibration
    preferences.begin("calibration", true);
    long minSteps, maxSteps;
    if (axisStr == "X" || axisStr == "x") {
      minSteps = preferences.getLong("xMinSteps", 0);
      maxSteps = preferences.getLong("xMaxSteps", 0);
    } else if (axisStr == "Y" || axisStr == "y") {
      minSteps = preferences.getLong("yMinSteps", 0);
      maxSteps = preferences.getLong("yMaxSteps", 0);
    } else {
      preferences.end();
      server.send(400, "text/plain", "error: invalid axis");
      return;
    }
    preferences.end();
    
    // Ensure min < max
    if (minSteps > maxSteps) {
      long temp = minSteps;
      minSteps = maxSteps;
      maxSteps = temp;
    }
    
    // Calculate steps/mm from physical measurement
    long stepRange = maxSteps - minSteps;
    if (stepRange <= 0) {
      server.send(400, "text/plain", "error: step limits not set (run calibration first)");
      return;
    }
    
    float newStepsPerMM = stepRange / measuredSize;
    
    // Update steps/mm for this axis
    float x = motionController.getStepsPerMM_X();
    float y = motionController.getStepsPerMM_Y();
    
    if (axisStr == "X" || axisStr == "x") {
      x = newStepsPerMM;
    } else {
      y = newStepsPerMM;
    }
    
    motionController.setStepsPerMM(x, y);
    
    // Save steps/mm to preferences
    preferences.begin("plotter", false);
    preferences.putFloat("stepsPerMM_X", x);
    preferences.putFloat("stepsPerMM_Y", y);
    preferences.end();
    
    // Recalculate work area limits based on new steps/mm
    // Get current work area center and adjust limits to match measured size
    float currentMinX = motionController.getMinX();
    float currentMaxX = motionController.getMaxX();
    float currentMinY = motionController.getMinY();
    float currentMaxY = motionController.getMaxY();
    
    if (axisStr == "X" || axisStr == "x") {
      // Keep center position, adjust width to measured size
      float center = (currentMinX + currentMaxX) / 2.0;
      float newMinX = center - (measuredSize / 2.0);
      float newMaxX = center + (measuredSize / 2.0);
      motionController.setWorkArea(newMinX, newMaxX, currentMinY, currentMaxY);
      
      // Save updated work area
      preferences.begin("plotter", false);
      preferences.putFloat("minX", newMinX);
      preferences.putFloat("maxX", newMaxX);
      preferences.end();
      
      Serial.print("X-axis calibrated: ");
      Serial.print(measuredSize, 2);
      Serial.print("mm = ");
      Serial.print(stepRange);
      Serial.print(" steps → ");
      Serial.print(newStepsPerMM, 3);
      Serial.println(" steps/mm");
    } else {
      // Keep center position, adjust height to measured size
      float center = (currentMinY + currentMaxY) / 2.0;
      float newMinY = center - (measuredSize / 2.0);
      float newMaxY = center + (measuredSize / 2.0);
      motionController.setWorkArea(currentMinX, currentMaxX, newMinY, newMaxY);
      
      // Save updated work area
      preferences.begin("plotter", false);
      preferences.putFloat("minY", newMinY);
      preferences.putFloat("maxY", newMaxY);
      preferences.end();
      
      Serial.print("Y-axis calibrated: ");
      Serial.print(measuredSize, 2);
      Serial.print("mm = ");
      Serial.print(stepRange);
      Serial.print(" steps → ");
      Serial.print(newStepsPerMM, 3);
      Serial.println(" steps/mm");
    }
    
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "error: missing parameters");
  }
}

void handleQueueStatus() {
  String json = "{";
  json += "\"size\":" + String(QUEUE_SIZE) + ",";
  json += "\"used\":" + String(commandQueue.getCount()) + ",";
  json += "\"free\":" + String(QUEUE_SIZE - commandQueue.getCount()) + ",";
  json += "\"state\":\"" + stateMachine.getStateString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleUploadGcode() {
  // Handle batch G-code upload (POST with body containing G-code lines)
  if (server.hasArg("plain")) {
    String gcodeContent = server.arg("plain");
    int linesQueued = 0;
    int linesFailed = 0;
    
    // Split by newlines and queue each command
    int startPos = 0;
    while (startPos < gcodeContent.length()) {
      int endPos = gcodeContent.indexOf('\n', startPos);
      if (endPos == -1) endPos = gcodeContent.length();
      
      String line = gcodeContent.substring(startPos, endPos);
      line.trim();
      
      if (line.length() > 0 && !line.startsWith(";")) {  // Skip empty lines and comments
        Command parsedCmd = parseCommand(line);
        
        if (parsedCmd.valid) {
          if (commandQueue.enqueue(parsedCmd)) {
            linesQueued++;
          } else {
            linesFailed++;
            break;  // Queue full, stop processing
          }
        } else {
          linesFailed++;
        }
      }
      
      startPos = endPos + 1;
    }
    
    String response = "{";
    response += "\"queued\":" + String(linesQueued) + ",";
    response += "\"failed\":" + String(linesFailed) + ",";
    response += "\"queueFree\":" + String(QUEUE_SIZE - commandQueue.getCount());
    response += "}";
    
    Serial.print("Batch upload: ");
    Serial.print(linesQueued);
    Serial.print(" queued, ");
    Serial.print(linesFailed);
    Serial.println(" failed");
    
    server.send(200, "application/json", response);
  } else {
    server.send(400, "text/plain", "error: no G-code content");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  #ifdef DRIVER_TB6612
    Serial.println("ESP32 Stepper Motor Control with TB6612FNG");
    Serial.println("Using AccelStepper library");
  #elif defined(DRIVER_TMC2209)
    Serial.println("ESP32-S3 XIAO Stepper Motor Control with TMC2209");
    Serial.println("Using TMCStepper library");
  #endif
  Serial.println("Initializing...");
  
  // Load motor settings from preferences (must be done before stepper objects are used)
  preferences.begin("motor", true);  // Read-only mode
  #ifdef DRIVER_TB6612
    int savedMode = preferences.getInt("stepMode", AccelStepper::HALF4WIRE);
  #elif defined(DRIVER_TMC2209)
    int savedMode = 0;  // TMC2209 doesn't use AccelStepper step modes
  #endif
  float savedMaxSpeed = preferences.getFloat("maxSpeed", 1000.0);
  float savedAcceleration = preferences.getFloat("acceleration", 1000.0);
  int savedPowerRunning = preferences.getInt("powerRunning", 250);
  int savedPowerHolding = preferences.getInt("powerHolding", 120);
  preferences.end();
  
  // Load mechanical parameters from preferences
  preferences.begin("mechanical", true);  // Read-only mode
  fullStepsPerRev = preferences.getInt("fullStepsPerRev", 20);
  linearTravelPerRev = preferences.getFloat("linearTravelPerRev", 3.0);
  preferences.end();
  
  // Load pen settings from preferences
  preferences.begin("pen", true);  // Read-only mode
  penUpAngle = preferences.getInt("penUpAngle", 0);
  penDownAngle = preferences.getInt("penDownAngle", 90);
  dotDwellMs = preferences.getULong("dotDwellMs", 50);
  preferences.end();
  
  // Update stepMode if a saved value exists
  #ifdef DRIVER_TB6612
    if (savedMode == AccelStepper::FULL4WIRE || savedMode == AccelStepper::HALF4WIRE) {
      stepMode = savedMode;
    }
  #elif defined(DRIVER_TMC2209)
    // TMC2209 step mode is handled via UART configuration
    stepMode = savedMode;  // Placeholder
  #endif
  
  // Load saved speed and power settings
  maxSpeed = savedMaxSpeed;
  acceleration = savedAcceleration;
  motorPowerRunning = savedPowerRunning;
  motorPowerHolding = savedPowerHolding;
  
  Serial.print("Step mode loaded: ");
  #ifdef DRIVER_TB6612
    Serial.println((stepMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
  #elif defined(DRIVER_TMC2209)
    Serial.println("TMC2209 (microstepping via UART)");
  #endif
  
  // STBY pins are tied directly to 3.3V (not controlled by ESP32)
  // No need to configure them in code
  
  #ifdef DRIVER_TMC2209
    // Initialize shared UART for TMC2209 drivers
    // Must be done before driver begin() calls
    tmcUart.begin(TMC_UART_BAUD, SERIAL_8N1, TMC_UART_RX, TMC_UART_TX);
    delay(100);  // Give UART time to initialize
    Serial.print("TMC2209 UART initialized on RX=");
    Serial.print(TMC_UART_RX);
    Serial.print(" TX=");
    Serial.println(TMC_UART_TX);
  #endif
  
  // Initialize stepper drivers (handles PWM/UART setup internally)
  stepper1->begin();
  stepper2->begin();
  
  // Set initial motor power (will be adjusted based on motion state)
  setMotorPower(motorPowerHolding);
  
  // Initialize servo for pen control
  // ESP32Servo uses LEDC channels 0-3 by default, so we moved motors to channels 4-7
  // Allocate timer 0 for the servo (servos need 50Hz, motors use 20kHz, so different timers)
  // Note: ESP32Servo will automatically use available channels 0-3
  penServo.attach(SERVO_PIN, 500, 2500);
  delay(100);  // Give servo time to initialize
  penServo.write(penUpAngle);  // Start with pen up
  delay(100);  // Give servo time to move
  Serial.print("Servo initialized on pin ");
  Serial.print(SERVO_PIN);
  Serial.print(" - Pen up: ");
  Serial.print(penUpAngle);
  Serial.print("°, Pen down: ");
  Serial.print(penDownAngle);
  Serial.println("°");
  
  // Configure motor settings (drivers already initialized above)
  // IMPORTANT: Set speed and acceleration BEFORE any movement commands
  updateMotorSettings();
  
  // Initialize positions to 0
  stepper1->setCurrentPosition(0);
  stepper2->setCurrentPosition(0);
  
  // Ensure motors are enabled (especially important for TMC2209)
  stepper1->enable();
  stepper2->enable();
  
  Serial.println("Motors initialized and enabled");
  
  // Initialize motion controller (pass pen settings)
  motionController.initialize();
  motionController.setPenUpAngle(penUpAngle);
  motionController.setPenDownAngle(penDownAngle);
  motionController.setDotDwellMs(dotDwellMs);
  
  // Calculate microsteps/mm from mechanical parameters
  float fullStepsPerMM = calculateFullStepsPerMM();
  float microstepsPerMM_X = calculateMicrostepsPerMM('X');
  float microstepsPerMM_Y = calculateMicrostepsPerMM('Y');
  
  // Check for overrides in preferences
  preferences.begin("mechanical", true);
  float overrideX = preferences.getFloat("fullStepsPerMM_X_override", 0.0);
  float overrideY = preferences.getFloat("fullStepsPerMM_Y_override", 0.0);
  preferences.end();
  
  if (overrideX > 0) {
    microstepsPerMM_X = overrideX * (float)stepper1->getMicrostepping();
    Serial.print("Using X-axis override: ");
    Serial.print(overrideX, 3);
    Serial.print(" full steps/mm = ");
    Serial.print(microstepsPerMM_X, 3);
    Serial.println(" microsteps/mm");
  }
  if (overrideY > 0) {
    microstepsPerMM_Y = overrideY * (float)stepper2->getMicrostepping();
    Serial.print("Using Y-axis override: ");
    Serial.print(overrideY, 3);
    Serial.print(" full steps/mm = ");
    Serial.print(microstepsPerMM_Y, 3);
    Serial.println(" microsteps/mm");
  }
  
  // Load saved configuration from preferences
  preferences.begin("plotter", true);  // Read-only mode
  float savedMinX = preferences.getFloat("minX", MIN_X_MM);
  float savedMaxX = preferences.getFloat("maxX", MAX_X_MM);
  float savedMinY = preferences.getFloat("minY", MIN_Y_MM);
  float savedMaxY = preferences.getFloat("maxY", MAX_Y_MM);
  bool savedInvertX = preferences.getBool("invertX", false);
  bool savedInvertY = preferences.getBool("invertY", false);
  preferences.end();
  
  // Apply saved configuration
  motionController.setStepsPerMM(microstepsPerMM_X, microstepsPerMM_Y);
  motionController.setWorkArea(savedMinX, savedMaxX, savedMinY, savedMaxY);
  motionController.setInvertX(savedInvertX);
  motionController.setInvertY(savedInvertY);
  
  Serial.print("Loaded configuration: ");
  Serial.print("Microsteps/mm X=");
  Serial.print(microstepsPerMM_X, 3);
  Serial.print(" Y=");
  Serial.print(microstepsPerMM_Y, 3);
  Serial.print(" | Work area X[");
  Serial.print(savedMinX, 1);
  Serial.print(",");
  Serial.print(savedMaxX, 1);
  Serial.print("] Y[");
  Serial.print(savedMinY, 1);
  Serial.print(",");
  Serial.print(savedMaxY, 1);
  Serial.print("] | Direction X:");
  Serial.print(savedInvertX ? "inverted" : "normal");
  Serial.print(" Y:");
  Serial.println(savedInvertY ? "inverted" : "normal");
  
  Serial.println("Stepper motors initialized!");
  Serial.print("Step Mode: ");
  Serial.println((stepMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
  Serial.print("Max Speed: ");
  Serial.print(maxSpeed);
  Serial.println(" steps/sec (loaded from flash)");
  Serial.print("Acceleration: ");
  Serial.print(acceleration);
  Serial.println(" steps/sec^2 (loaded from flash)");
  Serial.print("Motor Power - Running: ");
  Serial.print(motorPowerRunning);
  Serial.print(" Holding: ");
  Serial.println(motorPowerHolding);
  Serial.print("Mechanical parameters: ");
  Serial.print(fullStepsPerRev);
  Serial.print(" steps/rev, ");
  Serial.print(linearTravelPerRev, 2);
  Serial.print(" mm/rev");
  Serial.print(" | Microstepping: X=");
  Serial.print(stepper1->getMicrostepping());
  Serial.print("x Y=");
  Serial.print(stepper2->getMicrostepping());
  Serial.print("x");
  Serial.print(" | Microsteps/mm: X=");
  Serial.print(microstepsPerMM_X, 3);
  Serial.print(" Y=");
  Serial.println(microstepsPerMM_Y, 3);
  Serial.println("G-code interpreter ready. Send commands via serial.");
  
  // Setup WiFi and Web Server
  setupWiFi();
  
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/moverel", handleMoveRel);
  server.on("/stop", handleStop);
  server.on("/stopall", handleStopAll);
  server.on("/sethome", handleSetHome);
  server.on("/homeall", handleHomeAll);
  server.on("/status", handleStatus);
  server.on("/setspeed", handleSetSpeed);
  server.on("/setaccel", handleSetAcceleration);
  server.on("/setpower", handleSetPower);
  server.on("/setstepmode", handleSetStepMode);
  server.on("/setpenangle", handleSetPenAngle);
  server.on("/setdotdwell", handleSetDotDwell);
  server.on("/testpen", handleTestPen);
  server.on("/setservoangle", handleSetServoAngle);
  server.on("/gcode", handleGcode);
  // Mechanical parameters
  server.on("/setfullstepsperrev", handleSetFullStepsPerRev);
  server.on("/setlineartravelperrev", handleSetLinearTravelPerRev);
  server.on("/setfullstepspermm", handleSetFullStepsPerMM);
  
  // Calibration
  server.on("/setsteplimit", handleSetStepLimit);
  server.on("/getsteplimits", handleGetStepLimits);
  server.on("/applycalibration", handleApplyCalibration);
  
  // Work area and position
  server.on("/getworkarea", handleGetWorkArea);
  server.on("/getposition", handleGetPosition);
  server.on("/flipdirection", handleFlipDirection);
  server.on("/getdirection", handleGetDirection);
  server.on("/sethomeposition", handleSetHomePosition);
  server.on("/queuestatus", handleQueueStatus);
  server.on("/uploadgcode", HTTP_POST, handleUploadGcode);
  
  server.begin();
  Serial.println("Web server started!");
  Serial.print("Open your browser and go to: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Handle web server requests
  server.handleClient();
  
  // Process serial input for G-code commands
  serialInterface.processSerialInput();
  
  // Update motion controller (handles dot dwell timing)
  motionController.update();
  
  // Stepper drivers must be called as often as possible for smooth motion
  // This is non-blocking and handles acceleration/deceleration automatically
  stepper1->run();
  stepper2->run();
  
  // G-code command execution
  static Command currentCommand;
  static bool commandInProgress = false;
  
  // Check if we can process next command
  if (stateMachine.getState() == STATE_IDLE && !commandInProgress) {
    // Try to dequeue next command
    if (commandQueue.dequeue(currentCommand)) {
      commandInProgress = true;
      stateMachine.setState(STATE_MOVING);
      
      // Execute command based on type
      bool success = false;
      String errorMsg = "";
      
      switch (currentCommand.type) {
        case 'G':
          // Disable manual mode when executing G-code commands (return to automatic control)
          motionController.setManualMode(false);
          if (currentCommand.code == 0) {
            // G0 - Rapid move (pen up)
            // Use current position if coordinate not specified
            float x = currentCommand.hasX ? currentCommand.x : motionController.getX_mm();
            float y = currentCommand.hasY ? currentCommand.y : motionController.getY_mm();
            success = motionController.executeRapidMove(x, y);
            if (!success) errorMsg = "position out of bounds";
          } else if (currentCommand.code == 1) {
            // G1 - Linear move (pen down)
            // Use current position if coordinate not specified
            float x = currentCommand.hasX ? currentCommand.x : motionController.getX_mm();
            float y = currentCommand.hasY ? currentCommand.y : motionController.getY_mm();
            success = motionController.executeLinearMove(x, y);
            if (!success) errorMsg = "position out of bounds";
          } else {
            errorMsg = "unsupported G code";
          }
          break;
          
        case 'D':
          // Disable manual mode when executing dot commands (return to automatic control)
          motionController.setManualMode(false);
          // D - Dot command
          success = motionController.executeDot(currentCommand.x, currentCommand.y);
          if (!success) errorMsg = "position out of bounds";
          break;
          
        case 'P':
          // P0 - Pen up, P1 - Pen down
          // Disable manual mode when executing pen commands (return to automatic control)
          motionController.setManualMode(false);
          if (currentCommand.code == 0) {
            success = motionController.executePenUp();
          } else if (currentCommand.code == 1) {
            success = motionController.executePenDown();
          }
          break;
          
        case 'H':
          // H - Home
          success = motionController.executeHome();
          break;
          
        case 'M':
          // M114 - Get position, M119 - Get limits
          if (currentCommand.code == 114) {
            Serial.print("X:");
            Serial.print(motionController.getX_mm(), 2);
            Serial.print(" Y:");
            Serial.print(motionController.getY_mm(), 2);
            Serial.print(" Pen:");
            Serial.println(motionController.getPenState() ? "DOWN" : "UP");
            serialInterface.sendOK();
            commandInProgress = false;
            stateMachine.setState(STATE_IDLE);
            success = true;  // Mark as handled
          } else if (currentCommand.code == 119) {
            Serial.println("X:0 Y:0");  // Placeholder - no limit switches yet
            serialInterface.sendOK();
            commandInProgress = false;
            stateMachine.setState(STATE_IDLE);
            success = true;  // Mark as handled
          } else {
            errorMsg = "unsupported M code";
          }
          break;
          
        case '!':
          // Emergency stop
          stepper1->stop();
          stepper2->stop();
          stateMachine.emergencyStop();
          serialInterface.sendOK();
          commandInProgress = false;
          success = true;  // Mark as handled
          break;
          
        case '~':
          // Resume
          stateMachine.resume();
          serialInterface.sendOK();
          commandInProgress = false;
          success = true;  // Mark as handled
          break;
      }
      
      if (!success && errorMsg.length() > 0) {
        serialInterface.sendError(errorMsg);
        commandInProgress = false;
        stateMachine.setState(STATE_IDLE);
      } else if (success && !motionController.isMoving()) {
        // Command completed immediately (like P0/P1, H, M commands)
        // Motion commands will complete when steppers stop
        if (currentCommand.type == 'P' || currentCommand.type == 'H' || 
            currentCommand.type == 'M' || currentCommand.type == '!' || 
            currentCommand.type == '~') {
          // These commands don't require motion completion
          // (already handled above with sendOK)
        }
      }
      // Motion commands (G0, G1, D) will complete when steppers stop
    }
  }
  
  // Check if current command has completed
  if (commandInProgress && stateMachine.getState() == STATE_MOVING) {
    if (!motionController.isMoving() && !stepper1->isRunning() && !stepper2->isRunning()) {
      // Command completed
      serialInterface.sendOK();
      commandInProgress = false;
      stateMachine.setState(STATE_IDLE);
    }
  }
  
  // Adjust motor power based on motion state to reduce heat
  // Increase power when moving, reduce when stopped/holding
  static unsigned long lastPowerCheck = 0;
  if (millis() - lastPowerCheck > 100) {  // Check every 100ms
    bool motor1Running = stepper1->isRunning();
    bool motor2Running = stepper2->isRunning();
    
    if (motor1Running || motor2Running) {
      // At least one motor is moving - use running power
      setMotorPower(motorPowerRunning);
    } else {
      // Both motors stopped - use lower holding power to reduce heat
      setMotorPower(motorPowerHolding);
    }
    
    lastPowerCheck = millis();
  }
  
}

