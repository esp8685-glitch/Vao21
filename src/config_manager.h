#pragma once
#include <Arduino.h>

bool loadConfig();

extern String mqtt_host;
extern int mqtt_port;
extern String mqtt_user;
extern String mqtt_pass;

extern String smtp_host;
extern int smtp_port;
extern String smtp_user;
extern String smtp_pass;

extern String FROM_EMAIL;
extern String RECIPIENT_EMAIL;
extern String RECIPIENT_EMAIL2;
