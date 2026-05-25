#include "detector_manager.h"
#include "storage.h"
#include "config.h"
#include "logger.h"
#include "email_queue.h"

#include <SD.h>
#include <vector>
#include <algorithm>

const char *DETECTOR_LIST_FILE = "/detectors.txt";

// =====================
// RAM STATE
// =====================
std::map<String, DetectorInfo> detectorMap;

// =====================
// INIT STORAGE
// =====================
bool initDetectorStorage()
{
    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("Failed to lock SD for init");
        return false;
    }

    if (!SD.exists(DETECTOR_LIST_FILE))
    {
        File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);
        if (f) f.close();
    }

    File f = SD.open(DETECTOR_LIST_FILE, FILE_READ);
    if (!f)
    {
        unlockSD();
        return false;
    }

    detectorMap.clear();

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();

        int p1 = line.indexOf('|');
        int p2 = line.lastIndexOf('|');

        if (p1 > 0 && p2 > p1)
        {
            DetectorInfo d;
            d.address = line.substring(0, p1);
            d.lastTimestamp = line.substring(p1 + 1, p2);
            d.eventCount = line.substring(p2 + 1).toInt();

            detectorMap[d.address] = d;
        }
    }

    f.close();
    unlockSD();

    writeLog("Detectors loaded: " + String(detectorMap.size()));
    return true;
}

// =====================
// EXISTS CHECK
// =====================
bool detectorExists(const String &address)
{
    return detectorMap.find(address) != detectorMap.end();
}

// =====================
// NUMERIC SORT HELP
// =====================
static std::vector<DetectorInfo> getSortedDetectors()
{
    std::vector<DetectorInfo> v;

    for (const auto &p : detectorMap)
        v.push_back(p.second);

    std::sort(v.begin(), v.end(),
        [](const DetectorInfo &a, const DetectorInfo &b)
        {
            return a.address.toFloat() < b.address.toFloat();
        });

    return v;
}

// =====================
// ADD OR UPDATE
// =====================
bool addOrUpdateDetector(const String &address,
                         const String &timestamp)
{
    if (address.length() == 0)
        return false;

    DetectorInfo &d = detectorMap[address];

    if (d.address.length() == 0)
    {
        d.address = address;
        d.eventCount = 0;
    }

    d.lastTimestamp = timestamp;
    d.eventCount++;

    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("SD lock failed");
        return false;
    }

    File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);
    if (!f)
    {
        unlockSD();
        return false;
    }

    // SORTED WRITE (NUMERIC)
    auto sorted = getSortedDetectors();

    for (const auto &x : sorted)
    {
        f.println(x.address + "|" +
                  x.lastTimestamp + "|" +
                  String(x.eventCount));
    }

    f.close();
    unlockSD();

    writeLog("Detector updated: " + address + " @ " + timestamp);
    return true;
}

// =====================
// CLEAR
// =====================
bool clearDetectorList()
{
    detectorMap.clear();

    if (!lockSD(pdMS_TO_TICKS(2000)))
        return false;

    SD.remove(DETECTOR_LIST_FILE);

    File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);
    if (f) f.close();

    unlockSD();

    writeLog("Detector list cleared");
    return true;
}

// =====================
// GETTERS
// =====================
std::vector<DetectorInfo> getStoredDetectors()
{
    return getSortedDetectors();
}

std::vector<String> readDetectorListFile()
{
    std::vector<String> result;

    auto sorted = getSortedDetectors();

    for (const auto &d : sorted)
        result.push_back(d.address);

    return result;
}

// =====================
// EMAIL REPORT
// =====================
void sendDetectorListEmail()
{
    auto detectors = getSortedDetectors();

    String body;
    body += "VAO21 Detector Report\n";
    body += "====================\n\n";

    if (detectors.empty())
    {
        body += "No detectors found\n";
    }
    else
    {
        for (const auto &d : detectors)
        {
            body += d.address + " | " +
                    d.lastTimestamp + " | count=" +
                    String(d.eventCount) + "\n";
        }
    }

    queueEmail("VAO21 Detector List", body);
    writeLog("Detector email queued");
}
bool removeDetector(const String &address)
{
    auto it = detectorMap.find(address);

    if (it == detectorMap.end())
        return false;

    detectorMap.erase(it);

    if (!lockSD(pdMS_TO_TICKS(2000)))
        return false;

    File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);

    if (!f)
    {
        unlockSD();
        return false;
    }

    auto sorted = getSortedDetectors();

    for (const auto &d : sorted)
    {
        f.println(d.address + "|" +
                  d.lastTimestamp + "|" +
                  String(d.eventCount));
    }

    f.close();

    unlockSD();

    writeLog("Detector removed: " + address);

    return true;
}