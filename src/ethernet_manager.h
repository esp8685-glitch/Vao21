#pragma once

#include <Arduino.h>

bool ethernetConnect();
bool ethernetOK();
void ethernetTask(void *pv);
bool lockEthernetBus(TickType_t timeoutTicks = portMAX_DELAY);
void unlockEthernetBus();
IPAddress getIP();

extern byte mac[6];
