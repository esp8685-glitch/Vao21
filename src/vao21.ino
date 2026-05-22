#include "config.h"
#include "storage.h"
#include "logger.h"
#include "email_queue.h"
#include "ethernet_manager.h"
#include "usb_task.h"

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

    if (!initStorage())
    {
        while(true)
            delay(1000);
    }

    writeLog("BOOT");
    recoverEmailQueue();

    ethernetConnect();
    initEmailSystem();

    Serial.println("BOOT 6");
    writeLog("vao21 boot");

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

    queueEmail(
        "VAO21 START " + String(millis()),
        "System boot successful " + String((uint32_t)esp_random(), HEX)
    );
}

void loop()
{
    vTaskDelay(
        1000 / portTICK_PERIOD_MS
    );
}
