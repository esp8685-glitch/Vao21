#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

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
    
    smtp_host = doc["smtp_host"].as<String>();
    smtp_port = doc["smtp_port"];
    smtp_user = doc["smtp_user"].as<String>();
    smtp_pass = doc["smtp_pass"].as<String>();

    FROM_EMAIL = doc["FROM_EMAIL"].as<String>();
    RECIPIENT_EMAIL = doc["RECIPIENT_EMAIL"].as<String>();
    RECIPIENT_EMAIL2 = doc["RECIPIENT_EMAIL2"].as<String>();

    return true;
}