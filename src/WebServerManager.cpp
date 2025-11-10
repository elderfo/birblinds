#include "WebServerManager.h"
#include "MotorControl.h"
#include "WiFiManager.h"
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);

void WebServerManager::begin()
{
  setupRoutes();
  server.begin();
  Serial.println("Web server started");
}

String WebServerManager::getStatusJSON()
{
  long pos = MotorControl::getPosition();
  String json = "{";
  json += "\"calibrated\":" + String(MotorControl::isCalibrated() ? "true" : "false") + ",";
  json += "\"currentPosition\":" + String(pos) + ",";
  json += "\"deployedPosition\":" + String(MotorControl::getDeployedPosition()) + ",";
  json += "\"retractedLimit\":" + String(MotorControl::isRetractedLimitHit() ? "true" : "false") + ",";
  json += "\"deployedLimit\":" + String(MotorControl::isDeployedLimitHit() ? "true" : "false") + ",";
  json += "\"lastAction\":\"" + WiFiManager::getLastAction() + "\"";
  json += "}";
  return json;
}

void WebServerManager::setupRoutes()
{
  // Serve main HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Bird Blinds Controller</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 600px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }
        .button {
            width: 100%;
            padding: 15px;
            margin: 10px 0;
            font-size: 18px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        .deploy-btn {
            background-color: #4CAF50;
            color: white;
        }
        .deploy-btn:hover {
            background-color: #45a049;
        }
        .retract-btn {
            background-color: #2196F3;
            color: white;
        }
        .retract-btn:hover {
            background-color: #0b7dda;
        }
        .calibrate-btn {
            background-color: #ff9800;
            color: white;
        }
        .calibrate-btn:hover {
            background-color: #e68900;
        }
        .status {
            margin-top: 30px;
            padding: 20px;
            background-color: #f9f9f9;
            border-radius: 5px;
            border-left: 4px solid #2196F3;
        }
        .status-item {
            margin: 10px 0;
        }
        .status-label {
            font-weight: bold;
            color: #555;
        }
        .message {
            margin-top: 10px;
            padding: 10px;
            border-radius: 5px;
            display: none;
        }
        .success {
            background-color: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .error {
            background-color: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🦅 Bird Blinds Controller</h1>
        
        <button class="button deploy-btn" onclick="sendCommand('deploy')">Deploy Blinds</button>
        <button class="button retract-btn" onclick="sendCommand('retract')">Retract Blinds</button>
        <button class="button calibrate-btn" onclick="sendCommand('calibrate')">Calibrate</button>
        
        <div id="message" class="message"></div>
        
        <div class="status">
            <h2>Status</h2>
            <div class="status-item">
                <span class="status-label">Calibrated:</span>
                <span id="calibrated">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Current Position:</span>
                <span id="position">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Deployed Position:</span>
                <span id="deployedPos">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Retracted Limit:</span>
                <span id="retractedLimit">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Deployed Limit:</span>
                <span id="deployedLimit">Loading...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Last Action:</span>
                <span id="lastAction">Loading...</span>
            </div>
        </div>
    </div>

    <script>
        function sendCommand(cmd) {
            showMessage('Sending command...', 'success');
            
            fetch('/api/' + cmd, {method: 'POST'})
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        showMessage(data.message, 'success');
                        updateStatus();
                    } else {
                        showMessage(data.message, 'error');
                    }
                })
                .catch(error => {
                    showMessage('Error: ' + error, 'error');
                });
        }

        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('calibrated').textContent = data.calibrated ? 'Yes' : 'No';
                    document.getElementById('position').textContent = data.currentPosition + ' steps';
                    document.getElementById('deployedPos').textContent = data.deployedPosition + ' steps';
                    document.getElementById('retractedLimit').textContent = data.retractedLimit ? 'TRIGGERED' : 'Not triggered';
                    document.getElementById('deployedLimit').textContent = data.deployedLimit ? 'TRIGGERED' : 'Not triggered';
                    document.getElementById('lastAction').textContent = data.lastAction;
                })
                .catch(error => console.error('Error updating status:', error));
        }

        function showMessage(msg, type) {
            const msgDiv = document.getElementById('message');
            msgDiv.textContent = msg;
            msgDiv.className = 'message ' + type;
            msgDiv.style.display = 'block';
            setTimeout(() => {
                msgDiv.style.display = 'none';
            }, 3000);
        }

        // Update status every 2 seconds
        updateStatus();
        setInterval(updateStatus, 2000);
    </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html); });

  // API: Deploy
  server.on("/api/deploy", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!MotorControl::isCalibrated()) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated\"}");
      return;
    }
    WiFiManager::updateLastAction("Deploy command received");
    MotorControl::queueCommand(CMD_DEPLOY);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Deploy command queued\"}"); });

  // API: Retract
  server.on("/api/retract", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (!MotorControl::isCalibrated()) {
      request->send(200, "application/json", "{\"success\":false,\"message\":\"Not calibrated\"}");
      return;
    }
    WiFiManager::updateLastAction("Retract command received");
    MotorControl::queueCommand(CMD_RETRACT);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Retract command queued\"}"); });

  // API: Calibrate
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    WiFiManager::updateLastAction("Calibration started");
    MotorControl::queueCommand(CMD_CALIBRATE);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration command queued\"}"); });

  // API: Status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "application/json", getStatusJSON()); });
}
