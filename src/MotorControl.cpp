#include "MotorControl.h"
#include "config.h"
#include "Storage.h"

// Static member initialization
SemaphoreHandle_t MotorControl::positionMutex = NULL;
SemaphoreHandle_t MotorControl::commandMutex = NULL;
long MotorControl::currentPosition = 0;
long MotorControl::retractedPosition = 0;
long MotorControl::deployedPosition = 0;
long MotorControl::safeDeployedPosition = 0;
long MotorControl::safetyBuffer = DEFAULT_SAFETY_BUFFER;
bool MotorControl::calibrated = false;
volatile MotorCommand MotorControl::pendingCommand = CMD_NONE;

void MotorControl::begin()
{
  createMutexes();
  initializePins();
}

void MotorControl::createMutexes()
{
  positionMutex = xSemaphoreCreateMutex();
  commandMutex = xSemaphoreCreateMutex();

  if (positionMutex == NULL || commandMutex == NULL)
  {
    Serial.println("FATAL: Failed to create motor control mutexes!");
    while (1)
    {
      delay(1000);
    }
  }
}

void MotorControl::initializePins()
{
  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(LIMIT_RETRACTED, INPUT_PULLUP);
  pinMode(LIMIT_DEPLOYED, INPUT_PULLUP);

  // Enable the driver (active low)
  digitalWrite(EN_PIN, LOW);
  delay(100);

  // Print diagnostics
  Serial.println("\n=== Limit Switch Diagnostics ===");
  Serial.print("LIMIT_RETRACTED (pin ");
  Serial.print(LIMIT_RETRACTED);
  Serial.print(") state: ");
  Serial.println(digitalRead(LIMIT_RETRACTED) == LOW ? "TRIGGERED (LOW)" : "NOT TRIGGERED (HIGH)");
  Serial.print("LIMIT_DEPLOYED (pin ");
  Serial.print(LIMIT_DEPLOYED);
  Serial.print(") state: ");
  Serial.println(digitalRead(LIMIT_DEPLOYED) == LOW ? "TRIGGERED (LOW)" : "NOT TRIGGERED (HIGH)");
  Serial.println("If both show TRIGGERED when switches are not pressed,");
  Serial.println("your switches may be normally-closed or wired incorrectly.");
  Serial.println("================================\n");
}

bool MotorControl::isRetractedLimitHit()
{
  return digitalRead(LIMIT_RETRACTED) == LOW;
}

bool MotorControl::isDeployedLimitHit()
{
  return digitalRead(LIMIT_DEPLOYED) == LOW;
}

long MotorControl::getPosition()
{
  long pos = 0;
  if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    pos = currentPosition;
    xSemaphoreGive(positionMutex);
  }
  return pos;
}

void MotorControl::setPosition(long pos)
{
  if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    currentPosition = pos;
    xSemaphoreGive(positionMutex);
  }
}

void MotorControl::queueCommand(MotorCommand cmd)
{
  if (xSemaphoreTake(commandMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    pendingCommand = cmd;
    xSemaphoreGive(commandMutex);
  }
}

MotorCommand MotorControl::getQueuedCommand()
{
  MotorCommand cmd = CMD_NONE;
  if (xSemaphoreTake(commandMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    cmd = pendingCommand;
    xSemaphoreGive(commandMutex);
  }
  return cmd;
}

void MotorControl::clearQueuedCommand()
{
  if (xSemaphoreTake(commandMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    pendingCommand = CMD_NONE;
    xSemaphoreGive(commandMutex);
  }
}

void MotorControl::moveSteps(int steps, bool checkLimits)
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
          saveCurrentCalibration();
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

void MotorControl::calibrate()
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

  calibrated = true;

  Serial.print("Calibration complete. Range: 0 to ");
  Serial.print(deployedPosition);
  Serial.println(" steps");
  Serial.print("Safe deployed position: ");
  Serial.print(safeDeployedPosition);
  Serial.print(" (");
  Serial.print(safetyBuffer);
  Serial.println(" steps before limit)");

  // Save calibration to EEPROM
  saveCurrentCalibration();

  // Return to retracted position
  retract();
}

void MotorControl::deploy()
{
  if (!calibrated)
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

void MotorControl::retract()
{
  if (!calibrated)
  {
    Serial.println("Error: Not calibrated. Run calibration first.");
    return;
  }

  moveToPosition(retractedPosition);
}

void MotorControl::moveToPosition(long targetPosition)
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

void MotorControl::homeToRetractedPosition()
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

bool MotorControl::loadStoredCalibration()
{
  long storedDeployed, storedBuffer;
  if (Storage::loadCalibration(storedDeployed, storedBuffer))
  {
    deployedPosition = storedDeployed;
    safetyBuffer = storedBuffer;
    safeDeployedPosition = deployedPosition - safetyBuffer;
    retractedPosition = 0;
    calibrated = true;

    Serial.print("  Safe deployed position: ");
    Serial.println(safeDeployedPosition);
    return true;
  }
  return false;
}

void MotorControl::saveCurrentCalibration()
{
  Storage::saveCalibration(deployedPosition, safetyBuffer);
}

bool MotorControl::isCalibrated()
{
  return calibrated;
}

long MotorControl::getDeployedPosition()
{
  return deployedPosition;
}
