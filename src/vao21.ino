#include "config.h"
#include "LittleFS.h"
#include "config_manager.h"
#include "storage.h"
#include "logger.h"
#include "email_queue.h"
#include "ethernet_manager.h"
#include "usb_task.h"
#include "detector_manager.h"
#include "mqtt_manager.h"

SemaphoreHandle_t sdMutex = NULL;
SemaphoreHandle_t ethMutex = NULL;
SemaphoreHandle_t emailMutex = NULL;
SemaphoreHandle_t spiMutex = NULL;

SPIClass sdSpiBus(HSPI);

void setup()
{
    Serial.begin(115200);

    delay(2000);

    pinMode(SD_CS, OUTPUT);
    pinMode(ETH_CS, OUTPUT);

    digitalWrite(SD_CS, HIGH);
    digitalWrite(ETH_CS, HIGH);

    SPI.begin(
        ETH_SCK,
        ETH_MISO,
        ETH_MOSI,
        ETH_CS
    );

    sdSpiBus.begin(
        SD_SCK,
        SD_MISO,
        SD_MOSI,
        SD_CS
    );

    sdMutex = xSemaphoreCreateMutex();
    ethMutex = xSemaphoreCreateMutex();
    emailMutex = xSemaphoreCreateMutex();
    spiMutex = xSemaphoreCreateMutex();

    if (!sdMutex || !ethMutex || !emailMutex || !spiMutex)
    {
        Serial.println("MUTEX FAIL");

        while(true)
            delay(1000);
    }
    if (!loadConfig())
    {
        Serial.println("[VAO21] Config load failed!");
        while (1) delay(1000);
    }
    if (!initStorage())
    {
        while(true)
            delay(1000);
    }
    recoverEmailQueue();
    if (!initDetectorStorage()){
        writeLog("WARNING: Failed to initialize detector storage");
    }
    ethernetConnect();
    initEmailSystem();
    mqttSetup();

    xTaskCreatePinnedToCore(
        ethernetTask,
        "ethernetTask",
        ETH_TASK_STACK,
        NULL,
        1,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        emailTask,
        "emailTask",
        EMAIL_TASK_STACK,
        NULL,
        1,
        NULL,
        1
    );

xTaskCreatePinnedToCore(
    usbTask,
    "usbTask",
    8192,
    NULL,
    1,
    NULL,
    1
);
/*
    queueEmail(
        "VAO21 START " + String(millis()),
        "System boot successful " + String((uint32_t)esp_random(), HEX)
    );
*/
sendDetectorListEmail();
}

void loop()
{
    mqttLoop();
    if (Serial.available())
    {
        String line = Serial.readStringUntil('\n');
        if (line.length() > 0)
        {
            if (line.endsWith("\r"))
                line.remove(line.length() - 1);
            handleSerialConsoleLine(line);
        }
    }
    processPendingCommands();
    vTaskDelay(
        1000 / portTICK_PERIOD_MS
    );
}

void processPendingCommands(){
    if (detectorEmailRequest){
        detectorEmailRequest = false;
        sendDetectorListEmail();
    }
    if (clearDetectorsRequest){
        clearDetectorsRequest = false;
        clearDetectorList();
    }
}