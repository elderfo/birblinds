#include <Arduino.h>
#include <EEPROM.h>

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
