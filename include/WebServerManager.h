#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

class WebServerManager
{
public:
  static void begin();
  static String getStatusJSON();

private:
  static void setupRoutes();
};

#endif // WEB_SERVER_H
