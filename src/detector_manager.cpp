#include "detector_manager.h"
#include "storage.h"
#include "config.h"
#include "logger.h"
#include <set>

// In-memory cache of detector addresses
std::set<String> detectorCache;
bool detectorCacheInitialized = false;

const char *DETECTOR_LIST_FILE = "/detectors.txt";

bool initDetectorStorage()
{
    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("Failed to lock SD for detector initialization");
        return false;
    }

    // Create detectors list file if it doesn't exist
    if (!SD.exists(DETECTOR_LIST_FILE))
    {
        File f = SD.open(DETECTOR_LIST_FILE, FILE_WRITE);
        if (f)
        {
            f.close();
            writeLog("Created detector list file");
        }
        else
        {
            writeLog("Failed to create detector list file");
            unlockSD();
            return false;
        }
    }

    // Load existing detectors into cache
    File f = SD.open(DETECTOR_LIST_FILE, FILE_READ);
    if (f)
    {
        while (f.available())
        {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() > 0)
            {
                detectorCache.insert(line);
            }
        }
        f.close();
        detectorCacheInitialized = true;
        writeLog("Loaded " + String(detectorCache.size()) + " detectors from SD");
    }
    else
    {
        writeLog("Could not open detector list file for reading");
        unlockSD();
        return false;
    }

    unlockSD();
    return true;
}

bool addDetectorAddress(const String &address)
{
    if (address.length() == 0)
        return false;

    // Check if already exists
    if (detectorCache.find(address) != detectorCache.end())
    {
        return true; // Already exists, not an error
    }

    // Add to cache
    detectorCache.insert(address);

    // Write to SD card
    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("Failed to lock SD for detector write");
        return false;
    }

    File f = SD.open(DETECTOR_LIST_FILE, FILE_APPEND);
    if (f)
    {
        f.println(address);
        f.close();
        writeLog("Added detector: " + address);
        unlockSD();
        return true;
    }
    else
    {
        writeLog("Failed to open detector list file for writing");
        unlockSD();
        return false;
    }
}

std::vector<String> getStoredDetectors()
{
    std::vector<String> result;
    for (const auto &addr : detectorCache)
    {
        result.push_back(addr);
    }
    return result;
}

std::vector<String> readDetectorListFile()
{
    std::vector<String> result;
    if (!lockSD(pdMS_TO_TICKS(2000)))
    {
        writeLog("Failed to lock SD for reading detectors list");
        return result;
    }

    File f = SD.open(DETECTOR_LIST_FILE, FILE_READ);
    if (!f)
    {
        writeLog("Failed to open detector list file for reading");
        unlockSD();
        return result;
    }

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0)
        {
            result.push_back(line);
        }
    }
    f.close();
    unlockSD();
    return result;
}

bool detectorExists(const String &address)
{
    return detectorCache.find(address) != detectorCache.end();
}
