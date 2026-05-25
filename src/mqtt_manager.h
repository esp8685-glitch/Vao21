#pragma once
#include <Arduino.h>
void mqttSetup();
void mqttLoop();
void mqttPublish(const String &topic, const String &payload);
bool mqttConnected();
void processMqttCommand(const String &cmd);
bool mqttReconnect();
void mqttDisconnect();