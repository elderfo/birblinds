#ifndef CONFIG_H
#define CONFIG_H

// Pin Definitions
#define EN_PIN 4   // LOW: Driver enabled, HIGH: Driver disabled
#define STEP_PIN 5 // Step on the rising edge
#define DIR_PIN 6  // Direction control

// Limit Switch Pins
#define LIMIT_RETRACTED 15 // Limit switch for fully retracted position
#define LIMIT_DEPLOYED 16  // Limit switch for fully deployed position

// Motor Configuration
#define SPEED_DELAY 500 // Delay in microseconds between steps (controls speed)

// Safety Configuration
#define DEFAULT_SAFETY_BUFFER 200 // Steps to stop before deployed limit switch

// EEPROM Configuration
#define EEPROM_SIZE 512
#define EEPROM_MAGIC_NUMBER 0xBD01 // Bird Blinds v1
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_DEPLOYED_POS 4
#define EEPROM_ADDR_SAFETY_BUFFER 8

#endif // CONFIG_H
