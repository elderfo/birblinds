#include <Arduino.h>

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
long currentPosition = 0;   // Current position in steps
long retractedPosition = 0; // Calibrated retracted position (should be 0)
long deployedPosition = 0;  // Calibrated deployed position
bool isCalibrated = false;  // Calibration status

// Function Declarations
void moveSteps(int steps, bool checkLimits = true);
void calibrate();
void deploy();
void retract();
void moveToPosition(long targetPosition);
bool isRetractedLimitHit();
bool isDeployedLimitHit();

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

  Serial.println("Performing calibration...");

  // Perform initial calibration
  calibrate();

  Serial.println("Calibration complete!");
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
        Serial.println("Deployed limit reached");
        currentPosition = deployedPosition;
        return;
      }
      if (!forward && isRetractedLimitHit())
      {
        Serial.println("Retracted limit reached");
        currentPosition = retractedPosition;
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

  isCalibrated = true;

  Serial.print("Calibration complete. Range: 0 to ");
  Serial.print(deployedPosition);
  Serial.println(" steps");

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

  moveToPosition(deployedPosition);
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
