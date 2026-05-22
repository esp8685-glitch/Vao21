#pragma once

#include <Arduino.h>

void initUSBHost();
void usbTask(void *pv);
void handleSerialConsoleLine(const String &line);
