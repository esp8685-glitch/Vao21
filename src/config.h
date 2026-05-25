#pragma once

#include <Arduino.h>

#include <EspUsbHostSerial.h>
#include <EspUsbHostSerial_FTDI.h>
#include <vector>

#include <SPI.h>
#include <SD.h>
#include <Ethernet2.h>

#include "usb_task.h"
#define USB_BATCH_TIMEOUT 5000
#define USB_GROUP_TIMEOUT_MS 5000
#define USB_RESEND_INTERVAL 300000
#define BAUD_RATE 9600
// ======================================================
// DEVICE
// ======================================================

#define DEVICE_NAME "VAO21"
#define CONTROL_EMAIL_ENABLED true
// ================= SD =================
#define SD_CS    4
#define SD_MOSI  6
#define SD_MISO  5
#define SD_SCK   7
// ================= W5500 =================
// Define W5500 pin assignments
#define ETH_CS   14
#define ETH_RST  9
#define ETH_SCK  13
#define ETH_MISO 12
#define ETH_MOSI 11

// =============================
// QUEUE
// =============================
#define MAX_RETRY_COUNT 5
#define QUEUE_BASE_BACKOFF_MS 60000UL
#define QUEUE_MAX_BACKOFF_MS 1800000UL
#define QUEUE_STALE_INFLIGHT_MS 900000UL
#define EMAIL_TASK_INTERVAL_MS 10000UL
#define ETH_TASK_INTERVAL_MS 5000
#define SMTP_VERBOSE_DIAGNOSTICS 0

// =============================
// TASK STACKS
// =============================
#define EMAIL_TASK_STACK 65536
#define LOG_TASK_STACK 8192
#define ETH_TASK_STACK 8192
#define USB_TASK_STACK 8000

// ======================================================
// GLOBALS
// ======================================================

extern SemaphoreHandle_t sdMutex;
extern SemaphoreHandle_t ethMutex;
extern SemaphoreHandle_t emailMutex;
extern SemaphoreHandle_t spiMutex;

extern SPIClass sdSpiBus;

#include <time.h>
extern bool timeInitialized;
extern unsigned long lastTimeSync;

const unsigned long TIME_SYNC_INTERVAL = 6UL * 60UL * 60UL * 1000UL; // 6 hours
// ===== TIME FUNCTION PROTOTYPES =====
void handleHourlyCheck();
String formatHtml(String text);

bool getHttpTime(struct tm &timeinfo);
void convertToTallinn(struct tm &utc, struct tm &local);

bool syncSystemTime();
void getTallinnTimeNow(struct tm &timeinfo);

String getTimestamp();
bool getHttpTime(struct tm &timeinfo);
int lastSunday(int year, int month);
bool isDstEU(struct tm &utc);

extern volatile bool detectorEmailRequest;
extern volatile bool clearDetectorsRequest;
extern std::vector<String> mqttQueue;