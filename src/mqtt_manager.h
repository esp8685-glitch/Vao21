#pragma once

void mqttSetup();
void mqttLoop();
void mqttPublish(const String &topic, const String &payload);
bool mqttConnected();