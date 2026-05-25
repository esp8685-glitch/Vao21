#include "config.h"
#include "logger.h"

bool timeInitialized = false;
unsigned long lastTimeSync = 0;

bool syncSystemTime() {
    struct tm utc;

    logInfo("Syncing time...");

    if (!getHttpTime(utc)) {
        logError("Time sync FAILED");
        return false;
    }

    time_t t = mktime(&utc);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);

    timeInitialized = true;
    lastTimeSync = millis();

    logInfo("Time sync OK");
    writeLog("Time synchronized: " + getTimestamp());
    return true;
}
void getTallinnTimeNow(struct tm &tallinn) {
    time_t now = time(nullptr);
    struct tm utc = *gmtime(&now);
    convertToTallinn(utc, tallinn);
}
String getTimestamp() {
    if (!timeInitialized) return "[NO TIME]";

    char ts[32];
    struct tm tallinn;
    getTallinnTimeNow(tallinn);

    strftime(ts, sizeof(ts), "%d/%m %H:%M", &tallinn);
    return String(ts);
}

bool getHttpTime(struct tm &timeinfo) {
    EthernetClient client;

    if (!client.connect("google.com", 80)) {
        logError("Connection failed");
        return false;
    }

    client.print("HEAD / HTTP/1.1\r\nHost: google.com\r\nConnection: close\r\n\r\n");

    while (client.connected()) {
        String line = client.readStringUntil('\n');

        if (line.startsWith("Date:")) {
            int day = line.substring(11, 13).toInt();
            String monthStr = line.substring(14, 17);
            int year = line.substring(18, 22).toInt();

            int hour = line.substring(23, 25).toInt();
            int min  = line.substring(26, 28).toInt();
            int sec  = line.substring(29, 31).toInt();

            const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            int month = 1;
            for (int i = 0; i < 12; i++) {
                if (monthStr == months[i]) {
                    month = i + 1;
                    break;
                }
            }

            timeinfo = {};
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon  = month - 1;
            timeinfo.tm_mday = day;
            timeinfo.tm_hour = hour;
            timeinfo.tm_min  = min;
            timeinfo.tm_sec  = sec;

            client.stop();
            return true;
        }

        if (line == "\r") break;
    }

    client.stop();
    return false;
}

int lastSunday(int year, int month) {
    struct tm t = {};
    for (int d = 31; d >= 25; d--) {
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = d;
        mktime(&t);
        if (t.tm_wday == 0) return d;
    }
    return 25;
}

bool isDstEU(struct tm &t) {
    int year = t.tm_year + 1900;
    int month = t.tm_mon + 1;
    int day = t.tm_mday;

    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;

    int ls = lastSunday(year, month);

    if (month == 3) return day >= ls;
    if (month == 10) return day < ls;

    return false;
}

void convertToTallinn(struct tm &utc, struct tm &local) {
    time_t raw = mktime(&utc);

    bool dst = isDstEU(utc);
    int offset = dst ? 3 : 2;

    raw += offset * 3600;
    local = *localtime(&raw);
}