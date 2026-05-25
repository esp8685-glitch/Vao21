#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "logger.h"

String mqtt_host;
int mqtt_port;
String mqtt_user;
String mqtt_pass;

String smtp_host;
int smtp_port;
String smtp_user;
String smtp_pass;

String FROM_EMAIL;
String RECIPIENT_EMAIL;
String RECIPIENT_EMAIL2;
bool silentMode = false;

bool loadConfig()
{
    if (!LittleFS.begin(true))
    {
        logError("[CONFIG] LittleFS mount failed");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f)
    {
        logError("[CONFIG] config.json not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        logError("[CONFIG] JSON parse failed");
        return false;
    }

    mqtt_host = doc["mqtt_host"].as<String>();
    mqtt_port = doc["mqtt_port"];
    mqtt_user = doc["mqtt_user"].as<String>();
    mqtt_pass = doc["mqtt_pass"].as<String>();

    smtp_host = doc["smtp_host"].as<String>();
    smtp_port = doc["smtp_port"];
    smtp_user = doc["smtp_user"].as<String>();
    smtp_pass = doc["smtp_pass"].as<String>();

    FROM_EMAIL = doc["FROM_EMAIL"].as<String>();
    RECIPIENT_EMAIL = doc["RECIPIENT_EMAIL"].as<String>();
    RECIPIENT_EMAIL2 = doc["RECIPIENT_EMAIL2"].as<String>();

logInfo("=== CONFIG DUMP START ===");

logInfo("MQTT HOST: " + mqtt_host);
logInfo("MQTT PORT: " + String(mqtt_port));
logInfo("MQTT USER: " + mqtt_user);

logInfo("SMTP HOST: " + smtp_host);
logInfo("SMTP PORT: " + String(smtp_port));
logInfo("SMTP USER: " + smtp_user);

logInfo("FROM EMAIL: " + FROM_EMAIL);
logInfo("RECIPIENT 1: " + RECIPIENT_EMAIL);
logInfo("RECIPIENT 2: " + RECIPIENT_EMAIL2);

logInfo("Silent mode: " + String(silentMode ? "ON" : "OFF"));

logInfo("=== CONFIG DUMP END ===");
    
    return true;
}