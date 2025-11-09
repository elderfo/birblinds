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

// Multi-threading Configuration
// Core 0: Web server and WiFi (less time-critical)
// Core 1: Motor control and serial commands (time-critical for smooth stepping)
TaskHandle_t webServerTaskHandle = NULL;
TaskHandle_t motorControlTaskHandle = NULL;
SemaphoreHandle_t positionMutex = NULL; // Protects position variables
SemaphoreHandle_t commandMutex = NULL;  // Protects command queue

// Command queue for thread-safe motor control
enum MotorCommand
{
  CMD_NONE,
  CMD_DEPLOY,
  CMD_RETRACT,
  CMD_CALIBRATE
};
volatile MotorCommand pendingCommand = CMD_NONE;

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
void webServerTask(void *parameter);
void motorControlTask(void *parameter);
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
void checkWiFiConnection();
String getStatusJSON();
void queueCommand(MotorCommand cmd);
long getPosition();         // Thread-safe position getter
void setPosition(long pos); // Thread-safe position setter

void setup()
{
  Serial.begin(115200);

  // Create mutexes for thread synchronization
  positionMutex = xSemaphoreCreateMutex();
  commandMutex = xSemaphoreCreateMutex();

  if (positionMutex == NULL || commandMutex == NULL)
  {
    Serial.println("FATAL: Failed to create mutexes!");
    while (1)
    {
      delay(1000);
    } // Halt
  }

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
  Serial.println("Multi-threaded: Core 0 = Web, Core 1 = Motor");

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

  // Create motor control task on Core 1 (time-critical)
  xTaskCreatePinnedToCore(
      motorControlTask,        // Task function
      "MotorControl",          // Task name
      8192,                    // Stack size (bytes)
      NULL,                    // Parameters
      2,                       // Priority (2 = high)
      &motorControlTaskHandle, // Task handle
      1                        // Core 1
  );

  // Create web server task on Core 0 (less critical)
  xTaskCreatePinnedToCore(
      webServerTask,        // Task function
      "WebServer",          // Task name
      8192,                 // Stack size (bytes)
      NULL,                 // Parameters
      1,                    // Priority (1 = normal)
      &webServerTaskHandle, // Task handle
      0                     // Core 0
  );

  Serial.println("\nTasks created:");
  Serial.println("  - Motor Control Task (Core 1, Priority 2)");
  Serial.println("  - Web Server Task (Core 0, Priority 1)");
}

void loop()
{
  // Main loop is now empty - all work is done in tasks
  // Keep loop alive but idle
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ========================================
// MOTOR CONTROL TASK (Core 1)
// ========================================
void motorControlTask(void *parameter)
{
  Serial.println("[Motor Task] Started on core 1");

  while (true)
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
        Serial.println(getPosition());
        Serial.print("Calibrated: ");
        Serial.println(isCalibrated ? "YES" : "NO");
        if (isCalibrated)
        {
          Serial.print("Range: ");
          Serial.print(retractedPosition);
          Serial.print(" to ");
          Serial.println(deployedPosition);
        }
        Serial.print("Running on core: ");
        Serial.println(xPortGetCoreID());
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

    // Check for queued commands from web interface
    if (xSemaphoreTake(commandMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      MotorCommand cmd = pendingCommand;
      pendingCommand = CMD_NONE;
      xSemaphoreGive(commandMutex);

      switch (cmd)
      {
      case CMD_DEPLOY:
        Serial.println("[Web] Deploying blinds...");
        deploy();
        Serial.println("[Web] Blinds deployed");
        break;

      case CMD_RETRACT:
        Serial.println("[Web] Retracting blinds...");
        retract();
        Serial.println("[Web] Blinds retracted");
        break;

      case CMD_CALIBRATE:
        Serial.println("[Web] Starting calibration...");
        calibrate();
        Serial.println("[Web] Calibration complete");
        break;

      case CMD_NONE:
        // No command pending
        break;
      }
    }

    // Small delay to prevent task from hogging CPU
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ========================================
// WEB SERVER TASK (Core 0)
// ========================================
void webServerTask(void *parameter)
{
  Serial.println("[Web Task] Started on core 0");

  // Setup WiFi and Web Server on Core 0
  setupWiFi();
  setupWebServer();

  while (true)
  {
    // Monitor WiFi connection status
    checkWiFiConnection();

    // Delay to prevent task from hogging CPU
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ========================================
// THREAD-SAFE HELPER FUNCTIONS
// ========================================
void queueCommand(MotorCommand cmd)
{
  if (xSemaphoreTake(commandMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    pendingCommand = cmd;
    xSemaphoreGive(commandMutex);
  }
}

long getPosition()
{
  long pos = 0;
  if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    pos = currentPosition;
    xSemaphoreGive(positionMutex);
  }
  return pos;
}

void setPosition(long pos)
{
  if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    currentPosition = pos;
    xSemaphoreGive(positionMutex);
  }
}

// ========================================
// MOTOR CONTROL FUNCTIONS
// ========================================
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
        long currPos = getPosition();
        if (currPos != deployedPosition)
        {
          Serial.print("Updating deployed endpoint from ");
          Serial.print(deployedPosition);
          Serial.print(" to ");
          Serial.println(currPos);

          deployedPosition = currPos;
          safeDeployedPosition = deployedPosition - safetyBuffer;
          saveCalibrationToEEPROM();
        }

        setPosition(deployedPosition);
        return;
      }
      if (!forward && isRetractedLimitHit())
      {
        Serial.println("Retracted limit reached");

        // Always reset to zero when retracted limit is hit
        long currPos = getPosition();
        if (currPos != 0)
        {
          Serial.println("Resetting position to 0");
          setPosition(0);
          retractedPosition = 0;
        }
        return;
      }
    }

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(SPEED_DELAY);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(SPEED_DELAY);

    // Update position (thread-safe)
    if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {
      currentPosition += forward ? 1 : -1;
      xSemaphoreGive(positionMutex);
    }
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
  setPosition(0);
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
  setPosition(deployedPosition);

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
  long stepsToMove = targetPosition - getPosition();

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
    setPosition(0);
    retractedPosition = 0;
    Serial.println("Home position established");
  }
  else
  {
    Serial.println("Warning: Retracted limit not found during homing!");
  }
}

void checkWiFiConnection()
{
  static unsigned long lastCheck = 0;
  static bool wasConnected = false;

  // Check WiFi status every 10 seconds
  if (millis() - lastCheck > 10000)
  {
    lastCheck = millis();

    bool isConnected = WiFi.status() == WL_CONNECTED;

    // Only log on status change
    if (isConnected != wasConnected)
    {
      wasConnected = isConnected;

      if (isConnected)
      {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        lastAction = "WiFi reconnected";
      }
      else
      {
        Serial.println("\n[WiFi] Disconnected - attempting reconnect...");
        lastAction = "WiFi disconnected";
        // Auto-reconnect should handle this, but we can force it
        WiFi.reconnect();
      }
    }
  }
}

void setupWiFi()
{
  Serial.println("\n=== WiFi Setup ===");

// ESP32-S3 specific: Add delay for USB CDC stability
#ifdef ARDUINO_USB_CDC_ON_BOOT
  Serial.println("USB CDC detected - adding initialization delay");
  delay(5000);
#endif

  // Disconnect any previous connection and clear config
  WiFi.disconnect(true);
  delay(100);

  // Disable WiFi persistence to avoid NVS conflicts
  WiFi.persistent(false);

  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // ESP32-S3 requires delay after mode change
  delay(100);

  // Disable auto-reconnect initially to have better control
  WiFi.setAutoReconnect(false);

  // Set WiFi power saving to none for reliable connection
  WiFi.setSleep(false);

  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Begin connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Wait for connection with detailed status reporting
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    delay(500);
    Serial.print(".");

    // Print status every 5 attempts
    if ((attempts + 1) % 5 == 0)
    {
      Serial.println();
      Serial.print("Status: ");
      switch (WiFi.status())
      {
      case WL_IDLE_STATUS:
        Serial.println("IDLE");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("NO SSID AVAILABLE");
        break;
      case WL_SCAN_COMPLETED:
        Serial.println("SCAN COMPLETED");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("CONNECT FAILED");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("CONNECTION LOST");
        break;
      case WL_DISCONNECTED:
        Serial.println("DISCONNECTED");
        break;
      default:
        Serial.print("UNKNOWN (");
        Serial.print(WiFi.status());
        Serial.println(")");
      }
    }
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("Channel: ");
    Serial.println(WiFi.channel());

    // Now enable auto-reconnect for stability
    WiFi.setAutoReconnect(true);

    lastAction = "WiFi connected";
    lastActionTime = millis();
  }
  else
  {
    Serial.println("\n\nWiFi connection failed!");
    Serial.print("Final status: ");
    Serial.println(WiFi.status());
    Serial.println("\nTroubleshooting:");
    Serial.println("1. Verify SSID and password in wifi_config.h");
    Serial.println("2. Check if router is on 2.4GHz (ESP32 doesn't support 5GHz)");
    Serial.println("3. Try moving ESP32 closer to router");
    Serial.println("4. Check if router has MAC filtering enabled");
    Serial.println("\nController will still work via serial commands");
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
    queueCommand(CMD_DEPLOY);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Deploy command queued\"}"); });

  // API: Retract
  server.on("/api/retract", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!isCalibrated) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated\"}");
      return;
    }
    lastAction = "Retract command received";
    lastActionTime = millis();
    queueCommand(CMD_RETRACT);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Retract command queued\"}"); });

  // API: Calibrate
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    lastAction = "Calibration started";
    lastActionTime = millis();
    queueCommand(CMD_CALIBRATE);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration command queued\"}"); });

  // API: Status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "application/json", getStatusJSON()); });

  server.begin();
  Serial.println("Web server started");
}

String getStatusJSON()
{
  long pos = getPosition();
  String json = "{";
  json += "\"calibrated\":" + String(isCalibrated ? "true" : "false") + ",";
  json += "\"currentPosition\":" + String(pos) + ",";
  json += "\"deployedPosition\":" + String(deployedPosition) + ",";
  json += "\"retractedLimit\":" + String(isRetractedLimitHit() ? "true" : "false") + ",";
  json += "\"deployedLimit\":" + String(isDeployedLimitHit() ? "true" : "false") + ",";
  json += "\"lastAction\":\"" + lastAction + "\"";
  json += "}";
  return json;
}
