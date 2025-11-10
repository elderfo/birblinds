#include "WiFiManager.h"
#include "wifi_config.h"
#include <WiFi.h>

// Static member initialization
String WiFiManager::lastAction = "System started";
unsigned long WiFiManager::lastActionTime = 0;

void WiFiManager::begin()
{
  Serial.println("\n=== WiFi Setup ===");

// ESP32-S3 specific: Add delay for USB CDC stability
#ifdef ARDUINO_USB_CDC_ON_BOOT
  Serial.println("USB CDC detected - adding initialization delay");
  delay(5000);
#endif

  // Disconnect any previous connection and clear config
  WiFi.disconnect(true);
  delay(100);

  // Disable WiFi persistence to avoid NVS conflicts
  WiFi.persistent(false);

  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // ESP32-S3 requires delay after mode change
  delay(100);

  // Disable auto-reconnect initially to have better control
  WiFi.setAutoReconnect(false);

  // Set WiFi power saving to none for reliable connection
  WiFi.setSleep(false);

  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Begin connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Wait for connection with detailed status reporting
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    delay(500);
    Serial.print(".");

    // Print status every 5 attempts
    if ((attempts + 1) % 5 == 0)
    {
      Serial.println();
      Serial.print("Status: ");
      switch (WiFi.status())
      {
      case WL_IDLE_STATUS:
        Serial.println("IDLE");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("NO SSID AVAILABLE");
        break;
      case WL_SCAN_COMPLETED:
        Serial.println("SCAN COMPLETED");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("CONNECT FAILED");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("CONNECTION LOST");
        break;
      case WL_DISCONNECTED:
        Serial.println("DISCONNECTED");
        break;
      default:
        Serial.print("UNKNOWN (");
        Serial.print(WiFi.status());
        Serial.println(")");
      }
    }
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("Channel: ");
    Serial.println(WiFi.channel());

    // Now enable auto-reconnect for stability
    WiFi.setAutoReconnect(true);

    lastAction = "WiFi connected";
    lastActionTime = millis();
  }
  else
  {
    Serial.println("\n\nWiFi connection failed!");
    Serial.print("Final status: ");
    Serial.println(WiFi.status());
    Serial.println("\nTroubleshooting:");
    Serial.println("1. Verify SSID and password in wifi_config.h");
    Serial.println("2. Check if router is on 2.4GHz (ESP32 doesn't support 5GHz)");
    Serial.println("3. Try moving ESP32 closer to router");
    Serial.println("4. Check if router has MAC filtering enabled");
    Serial.println("\nController will still work via serial commands");
    lastAction = "WiFi connection failed";
    lastActionTime = millis();
  }
  Serial.println("==================\n");
}

void WiFiManager::checkConnection()
{
  static unsigned long lastCheck = 0;
  static bool wasConnected = false;

  // Check WiFi status every 10 seconds
  if (millis() - lastCheck > 10000)
  {
    lastCheck = millis();

    bool isConnected = WiFi.status() == WL_CONNECTED;

    // Only log on status change
    if (isConnected != wasConnected)
    {
      wasConnected = isConnected;

      if (isConnected)
      {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        lastAction = "WiFi reconnected";
      }
      else
      {
        Serial.println("\n[WiFi] Disconnected - attempting reconnect...");
        lastAction = "WiFi disconnected";
        // Auto-reconnect should handle this, but we can force it
        WiFi.reconnect();
      }
    }
  }
}

bool WiFiManager::isConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIPAddress()
{
  return WiFi.localIP().toString();
}

void WiFiManager::updateLastAction(const String &action)
{
  lastAction = action;
  lastActionTime = millis();
}

String WiFiManager::getLastAction()
{
  return lastAction;
}

unsigned long WiFiManager::getLastActionTime()
{
  return lastActionTime;
}
