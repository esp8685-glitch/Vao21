#include "config.h"
#include "logger.h"
#include "storage.h"

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

    Serial.println(msg);
}
