#pragma once

#include <Arduino.h>

bool initStorage();
String sha1String(String input);
String generateMessageID();
bool lockSPI(TickType_t timeoutTicks = portMAX_DELAY);
void unlockSPI();
bool lockSD(TickType_t timeoutTicks = portMAX_DELAY);
void unlockSD();
