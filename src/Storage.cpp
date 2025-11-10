#include "Storage.h"
#include "config.h"
#include <EEPROM.h>

void Storage::begin()
{
  EEPROM.begin(EEPROM_SIZE);
}

bool Storage::loadCalibration(long &deployedPosition, long &safetyBuffer)
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

  Serial.println("Loaded calibration from EEPROM:");
  Serial.print("  Deployed position: ");
  Serial.println(deployedPosition);
  Serial.print("  Safety buffer: ");
  Serial.println(safetyBuffer);

  return true;
}

void Storage::saveCalibration(long deployedPosition, long safetyBuffer)
{
  Serial.println("Saving calibration to EEPROM...");
  EEPROM.writeUShort(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUMBER);
  EEPROM.writeLong(EEPROM_ADDR_DEPLOYED_POS, deployedPosition);
  EEPROM.writeLong(EEPROM_ADDR_SAFETY_BUFFER, safetyBuffer);
  EEPROM.commit();
  Serial.println("Calibration saved");
}
