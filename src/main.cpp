#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
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
#define EN_PIN 25   // LOW: Driver enabled, HIGH: Driver disabled
#define STEP_PIN 26 // Step on the rising edge
#define DIR_PIN 27  // Direction control

// Limit Switch Pins
#define LIMIT_RETRACTED 32 // Limit switch for fully retracted position
#define LIMIT_DEPLOYED 33  // Limit switch for fully deployed position

// Motor Configuration
#define SPEED_DELAY 500 // Delay in microseconds between steps (controls speed)

// Calibration and Position Tracking
long currentPosition = 0;                  // Current position in steps
long retractedPosition = 0;                // Calibrated retracted position (should be 0)
long deployedPosition = 0;                 // Calibrated deployed position (actual limit switch position)
long safeDeployedPosition = 0;             // Safe deployed position (before limit switch)
long safetyBuffer = DEFAULT_SAFETY_BUFFER; // Steps to stop before deployed limit
bool isCalibrated = false;                 // Calibration status

// Web Server
AsyncWebServer server(80);

// Function Declarations
void moveSteps(int steps, bool checkLimits = true);
void calibrate();
void deploy();
void retract();
void moveToPosition(long targetPosition);
bool isRetractedLimitHit();
bool isDeployedLimitHit();
void setupWiFi();
void setupWebServer();
String getStatusJSON();
void saveCalibrationToEEPROM();
bool loadCalibrationFromEEPROM();
void homeToRetractedPosition();

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

  // Setup WiFi and Web Server
  setupWiFi();
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
  Serial.print("Connecting to WiFi: ");
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

    // Setup mDNS
    if (MDNS.begin("birdblinds"))
    {
      Serial.println("mDNS responder started");
      Serial.println("Access at: http://birdblinds.local");
      MDNS.addService("http", "tcp", 80);
    }
    else
    {
      Serial.println("Error setting up mDNS responder!");
    }
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
    Serial.println("Check credentials in wifi_config.h");
  }
  Serial.println("==================\n");
}

void setupWebServer()
{
  // Serve the main HTML page
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
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 20px;
      padding: 40px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 500px;
      width: 100%;
    }
    h1 {
      color: #333;
      text-align: center;
      margin-bottom: 10px;
      font-size: 28px;
    }
    .subtitle {
      text-align: center;
      color: #666;
      margin-bottom: 30px;
      font-size: 14px;
    }
    .status {
      background: #f5f5f5;
      padding: 20px;
      border-radius: 10px;
      margin-bottom: 30px;
    }
    .status-item {
      display: flex;
      justify-content: space-between;
      padding: 8px 0;
      border-bottom: 1px solid #ddd;
    }
    .status-item:last-child { border-bottom: none; }
    .status-label {
      font-weight: 600;
      color: #555;
    }
    .status-value {
      color: #667eea;
      font-weight: 600;
    }
    .controls {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
      margin-bottom: 20px;
    }
    .btn {
      padding: 15px 20px;
      border: none;
      border-radius: 10px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s;
      color: white;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 5px 15px rgba(0,0,0,0.2);
    }
    .btn:active {
      transform: translateY(0);
    }
    .btn-deploy {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    }
    .btn-retract {
      background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
    }
    .btn-calibrate {
      background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);
      grid-column: span 2;
    }
    .btn-test {
      background: linear-gradient(135deg, #43e97b 0%, #38f9d7 100%);
    }
    .btn-status {
      background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);
    }
    .message {
      margin-top: 20px;
      padding: 15px;
      border-radius: 10px;
      text-align: center;
      display: none;
      font-weight: 600;
    }
    .message.success {
      background: #d4edda;
      color: #155724;
      display: block;
    }
    .message.error {
      background: #f8d7da;
      color: #721c24;
      display: block;
    }
    .message.info {
      background: #d1ecf1;
      color: #0c5460;
      display: block;
    }
    @media (max-width: 480px) {
      .container { padding: 25px; }
      h1 { font-size: 24px; }
      .btn { padding: 12px 15px; font-size: 14px; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🦅 Bird Blinds Controller</h1>
    <p class="subtitle">ESP32 Stepper Motor Control</p>
    
    <div class="status" id="statusDisplay">
      <div class="status-item">
        <span class="status-label">Calibrated:</span>
        <span class="status-value" id="calibrated">Loading...</span>
      </div>
      <div class="status-item">
        <span class="status-label">Current Position:</span>
        <span class="status-value" id="position">-</span>
      </div>
      <div class="status-item">
        <span class="status-label">Range:</span>
        <span class="status-value" id="range">-</span>
      </div>
      <div class="status-item">
        <span class="status-label">Retracted Limit:</span>
        <span class="status-value" id="limitRetracted">-</span>
      </div>
      <div class="status-item">
        <span class="status-label">Deployed Limit:</span>
        <span class="status-value" id="limitDeployed">-</span>
      </div>
    </div>
    
    <div class="controls">
      <button class="btn btn-deploy" onclick="sendCommand('deploy')">Deploy</button>
      <button class="btn btn-retract" onclick="sendCommand('retract')">Retract</button>
      <button class="btn btn-calibrate" onclick="sendCommand('calibrate')">Calibrate</button>
      <button class="btn btn-test" onclick="sendCommand('test')">Test Motor</button>
      <button class="btn btn-status" onclick="updateStatus()">Refresh Status</button>
    </div>
    
    <div class="message" id="message"></div>
  </div>

  <script>
    function showMessage(msg, type) {
      const msgEl = document.getElementById('message');
      msgEl.textContent = msg;
      msgEl.className = 'message ' + type;
      setTimeout(() => {
        msgEl.className = 'message';
      }, 5000);
    }

    async function sendCommand(cmd) {
      try {
        showMessage('Sending command...', 'info');
        const response = await fetch('/api/' + cmd, { method: 'POST' });
        const data = await response.json();
        showMessage(data.message, data.success ? 'success' : 'error');
      } catch (error) {
        showMessage('Error: ' + error.message, 'error');
      }
    }

    async function updateStatus() {
      try {
        const response = await fetch('/api/status');
        const data = await response.json();
        
        document.getElementById('calibrated').textContent = data.calibrated ? 'YES' : 'NO';
        document.getElementById('position').textContent = data.position;
        document.getElementById('range').textContent = data.calibrated ? 
          data.retractedPos + ' to ' + data.safeDeployedPos + ' (safe: ' + data.deployedPos + ')' : 'Not calibrated';
        document.getElementById('limitRetracted').textContent = data.limitRetracted ? 'TRIGGERED' : 'Not triggered';
        document.getElementById('limitDeployed').textContent = data.limitDeployed ? 'TRIGGERED' : 'Not triggered';
      } catch (error) {
        console.error('Failed to update status:', error);
      }
    }

    // Update status every 5 seconds (reduced from 2 to lower ESP32 load)
    setInterval(updateStatus, 5000);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html); });

  // API endpoint: Deploy
  server.on("/api/deploy", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!isCalibrated) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated. Run calibration first.\"}");
      return;
    }
    Serial.println("Web: Deploying blinds...");
    deploy();
    Serial.println("Web: Blinds deployed");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Blinds deployed successfully\"}"); });

  // API endpoint: Retract
  server.on("/api/retract", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!isCalibrated) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated. Run calibration first.\"}");
      return;
    }
    Serial.println("Web: Retracting blinds...");
    retract();
    Serial.println("Web: Blinds retracted");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Blinds retracted successfully\"}"); });

  // API endpoint: Calibrate
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    Serial.println("Web: Starting calibration...");
    calibrate();
    Serial.println("Web: Calibration complete");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration completed successfully\"}"); });

  // API endpoint: Test motor
  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    Serial.println("Web: Test - Moving 100 steps forward...");
    digitalWrite(DIR_PIN, HIGH);
    for (int i = 0; i < 100; i++)
    {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(SPEED_DELAY);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(SPEED_DELAY);
    }
    Serial.println("Web: Test complete");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Test movement completed\"}"); });

  // API endpoint: Get status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String json = getStatusJSON();
    request->send(200, "application/json", json); });

  // Start server
  server.begin();
  Serial.println("Web server started on port 80");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Access the interface at: http://");
    Serial.println(WiFi.localIP());
  }
}

String getStatusJSON()
{
  String json = "{";
  json += "\"calibrated\":" + String(isCalibrated ? "true" : "false") + ",";
  json += "\"position\":" + String(currentPosition) + ",";
  json += "\"retractedPos\":" + String(retractedPosition) + ",";
  json += "\"deployedPos\":" + String(deployedPosition) + ",";
  json += "\"safeDeployedPos\":" + String(safeDeployedPosition) + ",";
  json += "\"safetyBuffer\":" + String(safetyBuffer) + ",";
  json += "\"limitRetracted\":" + String(isRetractedLimitHit() ? "true" : "false") + ",";
  json += "\"limitDeployed\":" + String(isDeployedLimitHit() ? "true" : "false");
  json += "}";
  return json;
}
