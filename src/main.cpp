#include <Arduino.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// WiFi credentials - loaded from wifi_config.h
// Copy wifi_config.h.example to wifi_config.h and fill in your credentials
#include "wifi_config.h"

// Web server on port 80
WebServer server(80);

// TB6612FNG Pin Definitions for Stepper Motor 1
// Motor A (Coil 1)
#define MOTOR1_AIN1 14  // GPIO14 - Motor A Input 1
#define MOTOR1_AIN2 12  // GPIO12 - Motor A Input 2
#define MOTOR1_PWMA 13  // GPIO13 - Motor A PWM (power control)

// Motor B (Coil 2)
#define MOTOR1_BIN1 27  // GPIO27 - Motor B Input 1
#define MOTOR1_BIN2 26  // GPIO26 - Motor B Input 2
#define MOTOR1_PWMB 19  // GPIO19 - Motor B PWM (power control)

// TB6612FNG Pin Definitions for Stepper Motor 2
// Motor A (Coil 1)
#define MOTOR2_AIN1 33  // GPIO33 - Motor A Input 1 (NOTE: GPIO33 is input-only, but can work for some ESP32 variants)
#define MOTOR2_AIN2 25  // GPIO25 - Motor A Input 2
#define MOTOR2_PWMA 23  // GPIO23 - Motor A PWM (power control)

// Motor B (Coil 2)
// WARNING: GPIO32 and GPIO35 are INPUT-ONLY and cannot be used as outputs!
// You MUST change these to output-capable pins (e.g., GPIO4, GPIO5, GPIO16, GPIO17)
#define MOTOR2_BIN1 4   // GPIO4 - Motor B Input 1 (CHANGE THIS - was GPIO32)
#define MOTOR2_BIN2 5   // GPIO5 - Motor B Input 2 (CHANGE THIS - was GPIO35)
#define MOTOR2_PWMB 22  // GPIO22 - Motor B PWM (power control)

// STBY pins are tied directly to 3.3V (not controlled by ESP32)

// PWM settings for ESP32
#define PWM_FREQUENCY 5000
#define PWM_RESOLUTION 8  // 8-bit resolution (0-255)
#define PWM_CHANNEL_M1A 0
#define PWM_CHANNEL_M1B 1
#define PWM_CHANNEL_M2A 2
#define PWM_CHANNEL_M2B 3

// Motor power settings (0-255, lower = less heat but less torque)
#define MOTOR_POWER_RUNNING 180  // Power when moving (70% - reduces heat)
#define MOTOR_POWER_HOLDING 120  // Power when holding position (47% - much less heat)

// Preferences for persistent storage
Preferences preferences;

// Step mode: FULL4WIRE = full step, HALF4WIRE = half step
// Half step provides smoother motion and more precision but uses more power
// Must be declared before stepper objects
// Default to HALF4WIRE (half step)
int stepMode = AccelStepper::HALF4WIRE;  // Default to half step

// Motor parameters (adjustable at runtime via web interface)
float maxSpeed = 1000.0;          // Maximum speed in steps per second
float acceleration = 1000.0;       // Acceleration in steps per second squared
int motorPowerRunning = 250;      // Power when moving (0-255, ~98%)
int motorPowerHolding = 120;      // Power when holding (0-255, 47%)
#define STEPS_PER_REV 200         // Steps per revolution (typical for CD drive steppers)

// AccelStepper setup - mode will be set dynamically
// Motor 1
AccelStepper stepper1(stepMode, 
                      MOTOR1_AIN1, MOTOR1_AIN2, 
                      MOTOR1_BIN1, MOTOR1_BIN2);

// Motor 2
AccelStepper stepper2(stepMode, 
                      MOTOR2_AIN1, MOTOR2_AIN2, 
                      MOTOR2_BIN1, MOTOR2_BIN2);

// Function to set motor power via PWM
void setMotorPower(int power) {
  // Clamp power to 0-255 range
  if (power > 255) power = 255;
  if (power < 0) power = 0;
  
  ledcWrite(PWM_CHANNEL_M1A, power);
  ledcWrite(PWM_CHANNEL_M1B, power);
  ledcWrite(PWM_CHANNEL_M2A, power);
  ledcWrite(PWM_CHANNEL_M2B, power);
}

// Function to update motor speed and acceleration settings
void updateMotorSettings() {
  stepper1.setMaxSpeed(maxSpeed);
  stepper1.setAcceleration(acceleration);
  stepper2.setMaxSpeed(maxSpeed);
  stepper2.setAcceleration(acceleration);
}

// Function to change step mode (requires reinitializing steppers)
void changeStepMode(int newMode) {
  stepMode = newMode;
  // Note: AccelStepper objects are already created, but the mode is set at construction
  // For a proper mode change, we'd need to recreate them, but that's complex
  // Instead, we'll just update the mode variable and note it in serial
  Serial.print("Step mode changed to: ");
  Serial.println((newMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
  Serial.println("NOTE: Step mode change requires restart to take full effect.");
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
  
  // Settings section
  html += "<div class=\"settings\">";
  html += "<h2>Motor Settings</h2>";
  html += "<div class=\"settings-row\">";
  html += "<label>Max Speed (steps/sec):</label>";
  html += "<input type=\"number\" id=\"maxSpeed\" value=\"1000\" min=\"1\" max=\"2000\" step=\"10\">";
  html += "<button onclick=\"setMaxSpeed()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Acceleration (steps/sec²):</label>";
  html += "<input type=\"number\" id=\"acceleration\" value=\"1000\" min=\"1\" max=\"2000\" step=\"5\">";
  html += "<button onclick=\"setAcceleration()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Power When Running (0-255):</label>";
  html += "<input type=\"number\" id=\"powerRunning\" value=\"250\" min=\"0\" max=\"255\" step=\"5\">";
  html += "<button onclick=\"setPowerRunning()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Power When Holding (0-255):</label>";
  html += "<input type=\"number\" id=\"powerHolding\" value=\"120\" min=\"0\" max=\"255\" step=\"5\">";
  html += "<button onclick=\"setPowerHolding()\">Set</button>";
  html += "</div>";
  html += "<div class=\"settings-row\">";
  html += "<label>Step Mode:</label>";
  // Set the default selected option based on current step mode
  String stepModeSelected = (stepMode == AccelStepper::HALF4WIRE) ? "selected" : "";
  String fullStepSelected = (stepMode == AccelStepper::FULL4WIRE) ? "selected" : "";
  html += "<select id=\"stepMode\" style=\"padding: 8px; margin: 5px; width: 150px;\">";
  html += "<option value=\"full\" " + fullStepSelected + ">Full Step</option>";
  html += "<option value=\"half\" " + stepModeSelected + ">Half Step</option>";
  html += "</select>";
  html += "<button onclick=\"setStepMode()\">Set</button>";
  html += "<span style=\"margin-left: 10px; color: #666; font-size: 0.9em;\">(Requires restart)</span>";
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
  html += "</div></div>";
  
  // JavaScript
  html += "<script>";
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
  html += "fetch('/setspeed?speed=' + speed).then(() => alert('Max Speed set to ' + speed + ' steps/sec'));";
  html += "}";
  html += "function setAcceleration() {";
  html += "const accel = document.getElementById('acceleration').value;";
  html += "fetch('/setaccel?accel=' + accel).then(() => alert('Acceleration set to ' + accel + ' steps/sec²'));";
  html += "}";
  html += "function setPowerRunning() {";
  html += "const power = document.getElementById('powerRunning').value;";
  html += "fetch('/setpower?type=running&power=' + power).then(() => alert('Running power set to ' + power));";
  html += "}";
  html += "function setPowerHolding() {";
  html += "const power = document.getElementById('powerHolding').value;";
  html += "fetch('/setpower?type=holding&power=' + power).then(() => alert('Holding power set to ' + power));";
  html += "}";
  html += "function setStepMode() {";
  html += "const mode = document.getElementById('stepMode').value;";
  html += "fetch('/setstepmode?mode=' + mode).then(() => alert('Step mode set to ' + mode + '. Please restart ESP32 for changes to take effect.'));";
  html += "}";
  html += "setInterval(function() {";
  html += "fetch('/status').then(response => response.json()).then(data => {";
  html += "document.getElementById('status1').textContent = 'Position: ' + data.motor1.position + ' | Target: ' + data.motor1.target + ' | Speed: ' + data.motor1.speed.toFixed(1) + ' steps/sec';";
  html += "document.getElementById('status2').textContent = 'Position: ' + data.motor2.position + ' | Target: ' + data.motor2.target + ' | Speed: ' + data.motor2.speed.toFixed(1) + ' steps/sec';";
  html += "});";
  html += "}, 500);";
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
    
    if (motor == 1) {
      stepper1.moveTo(pos);
    } else if (motor == 2) {
      stepper2.moveTo(pos);
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
    
    if (motor == 1) {
      stepper1.move(steps);
    } else if (motor == 2) {
      stepper2.move(steps);
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
      stepper1.stop();
    } else if (motor == 2) {
      stepper2.stop();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleStopAll() {
  stepper1.stop();
  stepper2.stop();
  server.send(200, "text/plain", "OK");
}

void handleSetHome() {
  if (server.hasArg("motor")) {
    int motor = server.arg("motor").toInt();
    
    if (motor == 1) {
      stepper1.setCurrentPosition(0);
    } else if (motor == 2) {
      stepper2.setCurrentPosition(0);
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleHomeAll() {
  stepper1.moveTo(0);
  stepper2.moveTo(0);
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"motor1\":{";
  json += "\"position\":" + String(stepper1.currentPosition()) + ",";
  json += "\"target\":" + String(stepper1.targetPosition()) + ",";
  json += "\"speed\":" + String(stepper1.speed());
  json += "},";
  json += "\"motor2\":{";
  json += "\"position\":" + String(stepper2.currentPosition()) + ",";
  json += "\"target\":" + String(stepper2.targetPosition()) + ",";
  json += "\"speed\":" + String(stepper2.speed());
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
    if (type == "running") {
      motorPowerRunning = power;
      Serial.print("Running power set to: ");
      Serial.println(motorPowerRunning);
    } else if (type == "holding") {
      motorPowerHolding = power;
      Serial.print("Holding power set to: ");
      Serial.println(motorPowerHolding);
      // If motors are currently stopped, update power immediately
      if (!stepper1.isRunning() && !stepper2.isRunning()) {
        setMotorPower(motorPowerHolding);
      }
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetStepMode() {
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
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Stepper Motor Control with TB6612FNG");
  Serial.println("Using AccelStepper library");
  Serial.println("Initializing...");
  
  // Load step mode from preferences (must be done before stepper objects are used)
  preferences.begin("motor", true);  // Read-only mode
  int savedMode = preferences.getInt("stepMode", AccelStepper::HALF4WIRE);
  preferences.end();
  
  // Update stepMode if a saved value exists
  if (savedMode == AccelStepper::FULL4WIRE || savedMode == AccelStepper::HALF4WIRE) {
    stepMode = savedMode;
  }
  
  Serial.print("Step mode loaded: ");
  Serial.println((stepMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
  
  // STBY pins are tied directly to 3.3V (not controlled by ESP32)
  // No need to configure them in code
  
  // Configure PWM channels for motor power control
  ledcSetup(PWM_CHANNEL_M1A, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_M1B, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_M2A, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_M2B, PWM_FREQUENCY, PWM_RESOLUTION);
  
  ledcAttachPin(MOTOR1_PWMA, PWM_CHANNEL_M1A);
  ledcAttachPin(MOTOR1_PWMB, PWM_CHANNEL_M1B);
  ledcAttachPin(MOTOR2_PWMA, PWM_CHANNEL_M2A);
  ledcAttachPin(MOTOR2_PWMB, PWM_CHANNEL_M2B);
  
  // Set initial motor power (will be adjusted based on motion state)
  setMotorPower(motorPowerHolding);
  
  // Configure AccelStepper for both motors
  updateMotorSettings();
  stepper1.setCurrentPosition(0);
  stepper2.setCurrentPosition(0);
  
  Serial.println("Stepper motors initialized!");
  Serial.print("Step Mode: ");
  Serial.println((stepMode == AccelStepper::HALF4WIRE) ? "HALF STEP" : "FULL STEP");
  Serial.print("Max Speed: ");
  Serial.print(maxSpeed);
  Serial.println(" steps/sec");
  Serial.print("Acceleration: ");
  Serial.print(acceleration);
  Serial.println(" steps/sec^2");
  
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
  
  server.begin();
  Serial.println("Web server started!");
  Serial.print("Open your browser and go to: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Handle web server requests
  server.handleClient();
  
  // AccelStepper must be called as often as possible for smooth motion
  // This is non-blocking and handles acceleration/deceleration automatically
  stepper1.run();
  stepper2.run();
  
  // Adjust motor power based on motion state to reduce heat
  // Increase power when moving, reduce when stopped/holding
  static unsigned long lastPowerCheck = 0;
  if (millis() - lastPowerCheck > 100) {  // Check every 100ms
    bool motor1Running = stepper1.isRunning();
    bool motor2Running = stepper2.isRunning();
    
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

