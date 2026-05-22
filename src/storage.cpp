#include "config.h"
#include "storage.h"

bool lockSPI(TickType_t timeoutTicks)
{
    if (spiMutex != NULL && xSemaphoreTake(spiMutex, timeoutTicks) != pdTRUE)
        return false;
    digitalWrite(SD_CS, HIGH);
    digitalWrite(ETH_CS, HIGH);
    return true;
}

void unlockSPI()
{
    digitalWrite(SD_CS, HIGH);
    digitalWrite(ETH_CS, HIGH);
    if (spiMutex != NULL)
        xSemaphoreGive(spiMutex);
}

bool lockSD(TickType_t timeoutTicks)
{
    if (sdMutex != NULL && xSemaphoreTake(sdMutex, timeoutTicks) != pdTRUE)
        return false;
    if (!lockSPI(timeoutTicks))
    {
        if (sdMutex != NULL)
            xSemaphoreGive(sdMutex);
        return false;
    }
    return true;
}

void unlockSD()
{
    digitalWrite(SD_CS, HIGH);
    unlockSPI();
    if (sdMutex != NULL)
        xSemaphoreGive(sdMutex);
}

bool initStorage()
{
    digitalWrite(ETH_CS, HIGH);
    if (!SD.begin(SD_CS, sdSpiBus, 10000000))
    {
        Serial.println("SD FAIL");
        return false;
    }
    Serial.println("SD OK");
    if (!lockSD())
        return false;
    const char *dirs[] = {"/logs", "/queue", "/sent", "/failed", "/hash"};
    for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        if (!SD.exists(dirs[i]))
            SD.mkdir(dirs[i]);
    }
    File test = SD.open("/test.txt", FILE_WRITE);
    if (test)
    {
        test.println("TEST");
        test.close();

        Serial.println("SD WRITE OK");
    }
    else
    {
        Serial.println("SD WRITE FAIL");
    }
    unlockSD();
    return true;
}

String generateMessageID(){return String((uint32_t)esp_random(), HEX) + "_" + String(millis());}

String sha1String(String input)
{
    uint32_t h = 0;
    for (int i = 0; i < input.length(); i++)
    {
        h = (h * 31) + input[i];
    }
    return String(h, HEX);
}
