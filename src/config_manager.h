#pragma once
#include <Arduino.h>

bool loadConfig();

extern String mqtt_host;
extern int mqtt_port;
extern String mqtt_user;
extern String mqtt_pass;
