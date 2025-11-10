#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Command queue for thread-safe motor control
enum MotorCommand
{
  CMD_NONE,
  CMD_DEPLOY,
  CMD_RETRACT,
  CMD_CALIBRATE
};

class MotorControl
{
public:
  static void begin();
  static void initializePins();
  static void createMutexes();

  // Calibration and movement
  static void calibrate();
  static void deploy();
  static void retract();
  static void homeToRetractedPosition();

  // Position management (thread-safe)
  static long getPosition();
  static void setPosition(long pos);

  // Status queries
  static bool isCalibrated();
  static long getDeployedPosition();
  static bool isRetractedLimitHit();
  static bool isDeployedLimitHit();

  // Command queue
  static void queueCommand(MotorCommand cmd);
  static MotorCommand getQueuedCommand();
  static void clearQueuedCommand();

  // Low-level control
  static void moveSteps(int steps, bool checkLimits = true);
  static void moveToPosition(long targetPosition);

  // Load/save calibration
  static bool loadStoredCalibration();
  static void saveCurrentCalibration();

private:
  static SemaphoreHandle_t positionMutex;
  static SemaphoreHandle_t commandMutex;

  static long currentPosition;
  static long retractedPosition;
  static long deployedPosition;
  static long safeDeployedPosition;
  static long safetyBuffer;
  static bool calibrated;
  static volatile MotorCommand pendingCommand;
};

#endif // MOTOR_CONTROL_H
