#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

class WiFiManager
{
public:
  static void begin();
  static void checkConnection();
  static bool isConnected();
  static String getIPAddress();
  static void updateLastAction(const String &action);
  static String getLastAction();
  static unsigned long getLastActionTime();

private:
  static String lastAction;
  static unsigned long lastActionTime;
};

#endif // WIFI_MANAGER_H
