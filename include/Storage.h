#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>

class Storage
{
public:
  static void begin();
  static bool loadCalibration(long &deployedPosition, long &safetyBuffer);
  static void saveCalibration(long deployedPosition, long safetyBuffer);
};

#endif // STORAGE_H
