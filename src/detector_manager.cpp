#include "detector_manager.h"
#include "storage.h"
#include "config.h"
#include "logger.h"
#include "email_queue.h"

#include <SD.h>
#include <vector>
#include <algorithm>

const char *DETECTOR_LIST_FILE = "/detectors.txt";
const char *REMOVAL_REASONS_FILE = "/removal_reasons.txt";

// =====================
// RAM STATE
// =====================
std::map<String, DetectorInfo> detectorMap;

// Helper function to convert reason to string
static String reasonToString(RemovalReason reason)
{
    switch (reason)
    {
        case REASON_TEST_ALARM:
            return "TEST_ALARM";
        case REASON_REAL_FIRE:
            return "REAL_FIRE";
        default:
            return "UNKNOWN";
    }
}

// Helper function to convert string to reason
static RemovalReason stringToReason(const String &str)
{
    if (str == "TEST_ALARM")
        return REASON_TEST_ALARM;
    else if (str == "REAL_FIRE")
        return REASON_REAL_FIRE;
    else
        return REASON_UNKNOWN;
}

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

    if (!SD.exists(REMOVAL_REASONS_FILE))
    {
        File f = SD.open(REMOVAL_REASONS_FILE, FILE_WRITE);
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
            d.removalReason = REASON_UNKNOWN;

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
    String ts = timestamp;

    // If caller passed a placeholder or no timestamp, use system timestamp
    if (ts.length() == 0 || ts == "manual")
        ts = getTimestamp();

    DetectorInfo &d = detectorMap[address];

    if (d.address.length() == 0)
    {
        d.address = address;
        d.eventCount = 0;
        d.removalReason = REASON_UNKNOWN;
    }

    d.lastTimestamp = ts;
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

    writeLog("Detector updated: " + address + " @ " + ts);
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

    if (SD.exists(DETECTOR_LIST_FILE)) SD.remove(DETECTOR_LIST_FILE);
    if (SD.exists(REMOVAL_REASONS_FILE)) SD.remove(REMOVAL_REASONS_FILE);

    File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);
    if (f) f.close();

    File r = SD.open(REMOVAL_REASONS_FILE, FILE_WRITE);
    if (r) r.close();

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
// REMOVAL REASON TRACKING
// =====================
static void saveRemovalReason(const String &address, RemovalReason reason)
{
    if (!lockSD(pdMS_TO_TICKS(2000)))
        return;

    File f = SD.open(REMOVAL_REASONS_FILE, FILE_APPEND);
    if (f)
    {
        String timestamp = getTimestamp();
        f.println(address + "|" + timestamp + "|" + reasonToString(reason));
        f.close();
    }

    unlockSD();
}

// =====================
// EMAIL REPORT
// =====================
void sendDetectorListEmail()
{
    auto detectors = getSortedDetectors();

    // Count removal reasons from file
    uint32_t countTestAlarm = 0;
    uint32_t countRealFire = 0;
    uint32_t countUnknown = 0;

    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("Failed to lock SD for email report");
        return;
    }

    File f = SD.open(REMOVAL_REASONS_FILE, FILE_READ);
    if (f)
    {
        while (f.available())
        {
            String line = f.readStringUntil('\n');
            line.trim();

            int p2 = line.lastIndexOf('|');
            if (p2 > 0)
            {
                String reason = line.substring(p2 + 1);
                if (reason == "TEST_ALARM")
                    countTestAlarm++;
                else if (reason == "REAL_FIRE")
                    countRealFire++;
                else
                    countUnknown++;
            }
        }
        f.close();
    }

    unlockSD();

    String body;
    body += "VAO22 Detector Report\n";
    body += "====================\n\n";

    // Statistics section
    body += "Removal Statistics:\n";
    body += "- Test Alarms (häired): " + String(countTestAlarm) + "\n";
    body += "- Real Fires (päris tulekahju): " + String(countRealFire) + "\n";
    body += "- Unknown Reason (teadmata põhjus): " + String(countUnknown) + "\n";
    body += "\n====================\n\n";

    // Active detectors
    if (detectors.empty())
    {
        body += "No active detectors\n";
    }
    else
    {
        body += "Active Detectors (" + String(detectors.size()) + "):\n";
        for (const auto &d : detectors)
        {
            body += d.address + " | " +
                    d.lastTimestamp + " | count=" +
                    String(d.eventCount) + "\n";
        }
    }

    logInfo("VAO22 Detector List", body);
    if (queueEmail("VAO22 Detector List", body))
    {
        logInfo("Detector email queued");
    }
    else
    {
        logInfo("Detector email skipped");
    }
}

bool removeDetector(const String &address, RemovalReason reason)
{
    auto it = detectorMap.find(address);

    if (it == detectorMap.end())
        return false;

    // Save removal reason before erasing
    saveRemovalReason(address, reason);

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

    String reasonStr = reasonToString(reason);
    writeLog("Detector removed: " + address + " (reason: " + reasonStr + ")");

    return true;
}
