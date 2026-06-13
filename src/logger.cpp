#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "storage.h"
#include "config_manager.h"   // for silentMode


String getLogFileName()
{
    return "/logs/log.txt";
}

void initLogger()
{
    if (lockSD(pdMS_TO_TICKS(2000)))
    {
        if (!SD.exists("/logs"))
            SD.mkdir("/logs");

        unlockSD();
    }
}

void writeLog(String msg)
{
    bool locked = lockSD(pdMS_TO_TICKS(2000));

    if (locked)
    {
        File f = SD.open("/logs/log.txt", FILE_APPEND);

        if (f)
        {
            f.println(msg);
            f.flush();
            f.close();
        }

        unlockSD();
    }

    logInfo(msg);
}

// void logInfo(const String &msg)
// {
//     if (!silentMode)
//         Serial.println("[INFO] " + msg);
// }
void logInfo(const String &subject, const String &body)
{
    if (!silentMode)
    {
        Serial.print("[INFO] ");
        Serial.println(subject);

        if (!body.isEmpty())
        {
            Serial.print(body);
        }
    }
}

void logWarn(const String &msg)
{
    if (!silentMode)
        Serial.println("[WARN] " + msg);
}

void logError(const String &msg)
{
    Serial.println("[ERROR] " + msg); // always print errors
}