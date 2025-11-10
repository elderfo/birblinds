#include <Arduino.h>
#include "config.h"
#include "MotorControl.h"
#include "Storage.h"
#include "WiFiManager.h"
#include "WebServerManager.h"

// Multi-threading Configuration
// Core 0: Web server and WiFi (less time-critical)
// Core 1: Motor control and serial commands (time-critical for smooth stepping)
TaskHandle_t webServerTaskHandle = NULL;
TaskHandle_t motorControlTaskHandle = NULL;

// Task function declarations
void webServerTask(void *parameter);
void motorControlTask(void *parameter);

void setup()
{
  Serial.begin(115200);

  Serial.println("Bird Blinds Controller Started");
  Serial.println("TMC2209 in standalone mode (STEP/DIR control)");
  Serial.println("Multi-threaded: Core 0 = Web, Core 1 = Motor");

  // Initialize storage
  Storage::begin();

  // Initialize motor control (creates mutexes and sets up pins)
  MotorControl::begin();

  // Try to load stored calibration
  if (MotorControl::loadStoredCalibration())
  {
    Serial.println("Using stored calibration");

    // Home to retracted position
    MotorControl::homeToRetractedPosition();

    Serial.println("Ready! System is calibrated and homed.");
  }
  else
  {
    Serial.println("No stored calibration found. Performing full calibration...");
    MotorControl::calibrate();
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
        MotorControl::deploy();
        Serial.println("Blinds deployed");
        break;

      case 'r':
      case 'R':
        Serial.println("Retracting blinds...");
        MotorControl::retract();
        Serial.println("Blinds retracted");
        break;

      case 'c':
      case 'C':
        Serial.println("Starting calibration...");
        MotorControl::calibrate();
        Serial.println("Calibration complete");
        break;

      case 's':
      case 'S':
        // Status command
        Serial.println("\n=== Current Status ===");
        Serial.print("LIMIT_RETRACTED: ");
        Serial.println(MotorControl::isRetractedLimitHit() ? "TRIGGERED" : "NOT TRIGGERED");
        Serial.print("LIMIT_DEPLOYED: ");
        Serial.println(MotorControl::isDeployedLimitHit() ? "TRIGGERED" : "NOT TRIGGERED");
        Serial.print("Current position: ");
        Serial.println(MotorControl::getPosition());
        Serial.print("Calibrated: ");
        Serial.println(MotorControl::isCalibrated() ? "YES" : "NO");
        if (MotorControl::isCalibrated())
        {
          Serial.print("Deployed position: ");
          Serial.println(MotorControl::getDeployedPosition());
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
    MotorCommand cmd = MotorControl::getQueuedCommand();
    if (cmd != CMD_NONE)
    {
      MotorControl::clearQueuedCommand();

      switch (cmd)
      {
      case CMD_DEPLOY:
        Serial.println("[Web] Deploying blinds...");
        MotorControl::deploy();
        Serial.println("[Web] Blinds deployed");
        break;

      case CMD_RETRACT:
        Serial.println("[Web] Retracting blinds...");
        MotorControl::retract();
        Serial.println("[Web] Blinds retracted");
        break;

      case CMD_CALIBRATE:
        Serial.println("[Web] Starting calibration...");
        MotorControl::calibrate();
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
  WiFiManager::begin();
  WebServerManager::begin();

  while (true)
  {
    // Monitor WiFi connection status
    WiFiManager::checkConnection();

    // Delay to prevent task from hogging CPU
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
