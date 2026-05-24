#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

String mqtt_host;
int mqtt_port;
String mqtt_user;
String mqtt_pass;

bool loadConfig()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("[CONFIG] LittleFS mount failed");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f)
    {
        Serial.println("[CONFIG] config.json not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        Serial.println("[CONFIG] JSON parse failed");
        return false;
    }

    mqtt_host = doc["mqtt_host"].as<String>();
    mqtt_port = doc["mqtt_port"];
    mqtt_user = doc["mqtt_user"].as<String>();
    mqtt_pass = doc["mqtt_pass"].as<String>();

    Serial.println("[CONFIG] Loaded MQTT config OK");
    return true;
}