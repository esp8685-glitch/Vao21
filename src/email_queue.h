#pragma once

#include <Arduino.h>

void initEmailSystem();
void recoverEmailQueue();
bool sendEmail(const char *subject, const char *body);
bool queueEmail(String subject, String body);
void emailTask(void *pv);
