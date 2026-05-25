#pragma once
#include <Arduino.h>

void initLogger();
void writeLog(String msg);
void logInfo(const String &msg);
void logWarn(const String &msg);
void logError(const String &msg);
