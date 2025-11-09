#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "wifi_config.h"

// EEPROM Configuration
#define EEPROM_SIZE 512
#define EEPROM_MAGIC_NUMBER 0xBD01 // Bird Blinds v1
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_DEPLOYED_POS 4
#define EEPROM_ADDR_SAFETY_BUFFER 8

// Safety buffer - stop this many steps before the deployed limit switch
#define DEFAULT_SAFETY_BUFFER 200

// Pin Definitions
#define EN_PIN 4   // LOW: Driver enabled, HIGH: Driver disabled
#define STEP_PIN 5 // Step on the rising edge
#define DIR_PIN 6  // Direction control

// Limit Switch Pins
#define LIMIT_RETRACTED 15 // Limit switch for fully retracted position
#define LIMIT_DEPLOYED 16  // Limit switch for fully deployed position

// Motor Configuration
#define SPEED_DELAY 500 // Delay in microseconds between steps (controls speed)

// WiFi and Web Server
AsyncWebServer server(80);
String lastAction = "System started";
unsigned long lastActionTime = 0;

// Calibration and Position Tracking
long currentPosition = 0;                  // Current position in steps
long retractedPosition = 0;                // Calibrated retracted position (should be 0)
long deployedPosition = 0;                 // Calibrated deployed position (actual limit switch position)
long safeDeployedPosition = 0;             // Safe deployed position (before limit switch)
long safetyBuffer = DEFAULT_SAFETY_BUFFER; // Steps to stop before deployed limit
bool isCalibrated = false;                 // Calibration status

// Function Declarations
void moveSteps(int steps, bool checkLimits = true);
void calibrate();
void deploy();
void retract();
void moveToPosition(long targetPosition);
bool isRetractedLimitHit();
bool isDeployedLimitHit();
void saveCalibrationToEEPROM();
bool loadCalibrationFromEEPROM();
void homeToRetractedPosition();
void setupWiFi();
void setupWebServer();
String getStatusJSON();

void setup()
{
  Serial.begin(115200);

  // Configure pin modes
  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(LIMIT_RETRACTED, INPUT_PULLUP);
  pinMode(LIMIT_DEPLOYED, INPUT_PULLUP);

  // Enable the driver (active low)
  digitalWrite(EN_PIN, LOW);
  delay(100);

  Serial.println("Bird Blinds Controller Started");
  Serial.println("TMC2209 in standalone mode (STEP/DIR control)");

  // Check limit switch states
  Serial.println("\n=== Limit Switch Diagnostics ===");
  Serial.print("LIMIT_RETRACTED (pin 32) state: ");
  Serial.println(digitalRead(LIMIT_RETRACTED) == LOW ? "TRIGGERED (LOW)" : "NOT TRIGGERED (HIGH)");
  Serial.print("LIMIT_DEPLOYED (pin 33) state: ");
  Serial.println(digitalRead(LIMIT_DEPLOYED) == LOW ? "TRIGGERED (LOW)" : "NOT TRIGGERED (HIGH)");
  Serial.println("If both show TRIGGERED when switches are not pressed,");
  Serial.println("your switches may be normally-closed or wired incorrectly.");
  Serial.println("================================\n");

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Setup WiFi
  setupWiFi();

  // Setup Web Server
  setupWebServer();

  // Try to load stored calibration
  if (loadCalibrationFromEEPROM())
  {
    Serial.println("Using stored calibration");
    isCalibrated = true;

    // Home to retracted position
    homeToRetractedPosition();

    Serial.println("Ready! System is calibrated and homed.");
  }
  else
  {
    Serial.println("No stored calibration found. Performing full calibration...");
    calibrate();
    Serial.println("Calibration complete!");
  }

  Serial.println("Commands: 'd' = deploy, 'r' = retract, 'c' = calibrate");
}

void loop()
{
  // Check for serial commands
  if (Serial.available() > 0)
  {
    char cmd = Serial.read();

    switch (cmd)
    {
    case 'd':
    case 'D':
      Serial.println("Deploying blinds...");
      deploy();
      Serial.println("Blinds deployed");
      break;

    case 'r':
    case 'R':
      Serial.println("Retracting blinds...");
      retract();
      Serial.println("Blinds retracted");
      break;

    case 'c':
    case 'C':
      Serial.println("Starting calibration...");
      calibrate();
      Serial.println("Calibration complete");
      break;

    case 's':
    case 'S':
      // Status command - check limit switches
      Serial.println("\n=== Current Status ===");
      Serial.print("LIMIT_RETRACTED: ");
      Serial.println(digitalRead(LIMIT_RETRACTED) == LOW ? "TRIGGERED" : "NOT TRIGGERED");
      Serial.print("LIMIT_DEPLOYED: ");
      Serial.println(digitalRead(LIMIT_DEPLOYED) == LOW ? "TRIGGERED" : "NOT TRIGGERED");
      Serial.print("Current position: ");
      Serial.println(currentPosition);
      Serial.print("Calibrated: ");
      Serial.println(isCalibrated ? "YES" : "NO");
      if (isCalibrated)
      {
        Serial.print("Range: ");
        Serial.print(retractedPosition);
        Serial.print(" to ");
        Serial.println(deployedPosition);
      }
      Serial.println("====================\n");
      break;

    case 't':
    case 'T':
      // Test motor movement - 100 steps forward
      Serial.println("Test: Moving 100 steps forward...");
      digitalWrite(DIR_PIN, HIGH);
      for (int i = 0; i < 100; i++)
      {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(SPEED_DELAY);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(SPEED_DELAY);
      }
      Serial.println("Test complete. Did motor move?");
      break;
    }
  }
}

void moveSteps(int steps, bool checkLimits)
{
  if (steps == 0)
    return;

  // Set direction
  bool forward = steps > 0;
  digitalWrite(DIR_PIN, forward ? HIGH : LOW);
  delayMicroseconds(10); // Direction setup time

  int absSteps = abs(steps);

  for (int i = 0; i < absSteps; i++)
  {
    // Check limit switches if enabled
    if (checkLimits)
    {
      if (forward && isDeployedLimitHit())
      {
        Serial.println("WARNING: Deployed limit switch triggered!");

        // Update the deployed endpoint if we hit the switch unexpectedly
        if (currentPosition != deployedPosition)
        {
          Serial.print("Updating deployed endpoint from ");
          Serial.print(deployedPosition);
          Serial.print(" to ");
          Serial.println(currentPosition);

          deployedPosition = currentPosition;
          safeDeployedPosition = deployedPosition - safetyBuffer;
          saveCalibrationToEEPROM();
        }

        currentPosition = deployedPosition;
        return;
      }
      if (!forward && isRetractedLimitHit())
      {
        Serial.println("Retracted limit reached");

        // Always reset to zero when retracted limit is hit
        if (currentPosition != 0)
        {
          Serial.println("Resetting position to 0");
          currentPosition = 0;
          retractedPosition = 0;
        }
        return;
      }
    }

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(SPEED_DELAY);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(SPEED_DELAY);

    // Update position
    currentPosition += forward ? 1 : -1;
  }
}

void calibrate()
{
  Serial.println("Starting calibration sequence...");

  // Step 1: Move to retracted position (limit switch hit)
  Serial.println("Moving to retracted position...");
  digitalWrite(DIR_PIN, LOW); // Direction to retracted
  delayMicroseconds(10);

  // Move until retracted limit is hit (with timeout)
  int maxCalibrationSteps = 50000;
  for (int i = 0; i < maxCalibrationSteps; i++)
  {
    if (isRetractedLimitHit())
    {
      Serial.println("Retracted limit found");
      break;
    }
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(SPEED_DELAY);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(SPEED_DELAY);
  }

  // Set retracted position as zero
  currentPosition = 0;
  retractedPosition = 0;

  delay(500); // Brief pause

  // Step 2: Move to deployed position (opposite limit switch hit)
  Serial.println("Moving to deployed position...");
  digitalWrite(DIR_PIN, HIGH); // Direction to deployed
  delayMicroseconds(10);

  long stepCount = 0;
  for (int i = 0; i < maxCalibrationSteps; i++)
  {
    if (isDeployedLimitHit())
    {
      Serial.println("Deployed limit found");
      break;
    }
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(SPEED_DELAY);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(SPEED_DELAY);
    stepCount++;
  }

  // Set deployed position
  deployedPosition = stepCount;
  currentPosition = deployedPosition;

  // Calculate safe deployed position (stop before limit switch)
  safeDeployedPosition = deployedPosition - safetyBuffer;

  isCalibrated = true;

  Serial.print("Calibration complete. Range: 0 to ");
  Serial.print(deployedPosition);
  Serial.println(" steps");
  Serial.print("Safe deployed position: ");
  Serial.print(safeDeployedPosition);
  Serial.print(" (");
  Serial.print(safetyBuffer);
  Serial.println(" steps before limit)");

  // Save calibration to EEPROM
  saveCalibrationToEEPROM();

  // Return to retracted position
  retract();
}

void deploy()
{
  if (!isCalibrated)
  {
    Serial.println("Error: Not calibrated. Run calibration first.");
    return;
  }

  // Deploy to safe position (before the limit switch)
  Serial.print("Deploying to safe position: ");
  Serial.print(safeDeployedPosition);
  Serial.println(" steps");
  moveToPosition(safeDeployedPosition);
}

void retract()
{
  if (!isCalibrated)
  {
    Serial.println("Error: Not calibrated. Run calibration first.");
    return;
  }

  moveToPosition(retractedPosition);
}

void moveToPosition(long targetPosition)
{
  long stepsToMove = targetPosition - currentPosition;

  if (stepsToMove == 0)
  {
    Serial.println("Already at target position");
    return;
  }

  Serial.print("Moving ");
  Serial.print(abs(stepsToMove));
  Serial.println(" steps");

  moveSteps(stepsToMove, true);
}

bool isRetractedLimitHit()
{
  // Assuming limit switches are normally open, active low with pullup
  return digitalRead(LIMIT_RETRACTED) == LOW;
}

bool isDeployedLimitHit()
{
  // Assuming limit switches are normally open, active low with pullup
  return digitalRead(LIMIT_DEPLOYED) == LOW;
}

void saveCalibrationToEEPROM()
{
  Serial.println("Saving calibration to EEPROM...");
  EEPROM.writeUShort(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUMBER);
  EEPROM.writeLong(EEPROM_ADDR_DEPLOYED_POS, deployedPosition);
  EEPROM.writeLong(EEPROM_ADDR_SAFETY_BUFFER, safetyBuffer);
  EEPROM.commit();
  Serial.println("Calibration saved");
}

bool loadCalibrationFromEEPROM()
{
  Serial.println("Checking for stored calibration...");

  unsigned short magic = EEPROM.readUShort(EEPROM_ADDR_MAGIC);
  if (magic != EEPROM_MAGIC_NUMBER)
  {
    Serial.println("No valid calibration found in EEPROM");
    return false;
  }

  deployedPosition = EEPROM.readLong(EEPROM_ADDR_DEPLOYED_POS);
  safetyBuffer = EEPROM.readLong(EEPROM_ADDR_SAFETY_BUFFER);

  // Validate loaded values
  if (deployedPosition <= 0 || deployedPosition > 100000)
  {
    Serial.println("Invalid calibration data in EEPROM");
    return false;
  }

  safeDeployedPosition = deployedPosition - safetyBuffer;
  retractedPosition = 0;

  Serial.println("Loaded calibration from EEPROM:");
  Serial.print("  Deployed position: ");
  Serial.println(deployedPosition);
  Serial.print("  Safety buffer: ");
  Serial.println(safetyBuffer);
  Serial.print("  Safe deployed position: ");
  Serial.println(safeDeployedPosition);

  return true;
}

void homeToRetractedPosition()
{
  Serial.println("Homing to retracted position...");

  // Move towards retracted position until limit switch is hit
  digitalWrite(DIR_PIN, LOW); // Direction to retracted
  delayMicroseconds(10);

  int maxSteps = 50000;
  int stepCount = 0;

  while (!isRetractedLimitHit() && stepCount < maxSteps)
  {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(SPEED_DELAY);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(SPEED_DELAY);
    stepCount++;
  }

  if (isRetractedLimitHit())
  {
    Serial.println("Retracted limit switch reached");
    currentPosition = 0;
    retractedPosition = 0;
    Serial.println("Home position established");
  }
  else
  {
    Serial.println("Warning: Retracted limit not found during homing!");
  }
}

void setupWiFi()
{
  Serial.println("\n=== WiFi Setup ===");
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    lastAction = "WiFi connected";
    lastActionTime = millis();
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
    Serial.println("Controller will still work via serial commands");
    lastAction = "WiFi connection failed";
    lastActionTime = millis();
  }
  Serial.println("==================\n");
}

void setupWebServer()
{
  // Serve main HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Bird Blinds Controller</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 600px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }
        .button {
            width: 100%;
            padding: 15px;
            margin: 10px 0;
            font-size: 18px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        .deploy-btn {
            background-color: #4CAF50;
            color: white;
        }
        .deploy-btn:hover {
            background-color: #45a049;
        }
        .retract-btn {
            background-color: #2196F3;
            color: white;
        }
        .retract-btn:hover {
            background-color: #0b7dda;
        }
        .calibrate-btn {
            background-color: #ff9800;
            color: white;
        }
        .calibrate-btn:hover {
            background-color: #e68900;
        }
        .status {
            margin-top: 30px;
            padding: 20px;
            background-color: #f9f9f9;
            border-radius: 5px;
            border-left: 4px solid #2196F3;
        }
        .status-item {
            margin: 10px 0;
        }
        .status-label {
            font-weight: bold;
            color: #555;
        }
        .message {
            margin-top: 10px;
            padding: 10px;
            border-radius: 5px;
            display: none;
        }
        .success {
            background-color: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .error {
            background-color: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🦅 Bird Blinds Controller</h1>
        
        <button class="button deploy-btn" onclick="sendCommand('deploy')">Deploy Blinds</button>
        <button class="button retract-btn" onclick="sendCommand('retract')">Retract Blinds</button>
        <button class="button calibrate-btn" onclick="sendCommand('calibrate')">Calibrate</button>
        
        <div id="message" class="message"></div>
        
        <div class="status">
            <h2>Status</h2>
            <div class="status-item">
                <span class="status-label">Calibrated:</span>
                <span id="calibrated">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Current Position:</span>
                <span id="position">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Deployed Position:</span>
                <span id="deployedPos">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Retracted Limit:</span>
                <span id="retractedLimit">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Deployed Limit:</span>
                <span id="deployedLimit">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Last Action:</span>
                <span id="lastAction">Loading...</span>
            </div>
        </div>
    </div>

    <script>
        function sendCommand(cmd) {
            showMessage('Sending command...', 'success');
            
            fetch('/api/' + cmd, {method: 'POST'})
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        showMessage(data.message, 'success');
                        updateStatus();
                    } else {
                        showMessage(data.message, 'error');
                    }
                })
                .catch(error => {
                    showMessage('Error: ' + error, 'error');
                });
        }

        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('calibrated').textContent = data.calibrated ? 'Yes' : 'No';
                    document.getElementById('position').textContent = data.currentPosition + ' steps';
                    document.getElementById('deployedPos').textContent = data.deployedPosition + ' steps';
                    document.getElementById('retractedLimit').textContent = data.retractedLimit ? 'TRIGGERED' : 'Not triggered';
                    document.getElementById('deployedLimit').textContent = data.deployedLimit ? 'TRIGGERED' : 'Not triggered';
                    document.getElementById('lastAction').textContent = data.lastAction;
                })
                .catch(error => console.error('Error updating status:', error));
        }

        function showMessage(msg, type) {
            const msgDiv = document.getElementById('message');
            msgDiv.textContent = msg;
            msgDiv.className = 'message ' + type;
            msgDiv.style.display = 'block';
            setTimeout(() => {
                msgDiv.style.display = 'none';
            }, 3000);
        }

        // Update status every 2 seconds
        updateStatus();
        setInterval(updateStatus, 2000);
    </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html); });

  // API: Deploy
  server.on("/api/deploy", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!isCalibrated) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated\"}");
      return;
    }
    lastAction = "Deploy command received";
    lastActionTime = millis();
    deploy();
    lastAction = "Blinds deployed";
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Blinds deployed\"}"); });

  // API: Retract
  server.on("/api/retract", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!isCalibrated) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated\"}");
      return;
    }
    lastAction = "Retract command received";
    lastActionTime = millis();
    retract();
    lastAction = "Blinds retracted";
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Blinds retracted\"}"); });

  // API: Calibrate
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    lastAction = "Calibration started";
    lastActionTime = millis();
    calibrate();
    lastAction = "Calibration complete";
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration complete\"}"); });

  // API: Status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "application/json", getStatusJSON()); });

  server.begin();
  Serial.println("Web server started");
}

String getStatusJSON()
{
  String json = "{";
  json += "\"calibrated\":" + String(isCalibrated ? "true" : "false") + ",";
  json += "\"currentPosition\":" + String(currentPosition) + ",";
  json += "\"deployedPosition\":" + String(deployedPosition) + ",";
  json += "\"retractedLimit\":" + String(isRetractedLimitHit() ? "true" : "false") + ",";
  json += "\"deployedLimit\":" + String(isDeployedLimitHit() ? "true" : "false") + ",";
  json += "\"lastAction\":\"" + lastAction + "\"";
  json += "}";
  return json;
}
