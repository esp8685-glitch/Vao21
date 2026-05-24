#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

struct DetectorInfo
{
    String address;
    String lastTimestamp;
    uint32_t eventCount;
};
// Core API
bool initDetectorStorage();
bool addOrUpdateDetector(const String &address, const String &timestamp);
bool clearDetectorList();
bool detectorExists(const String &address);
// Queries
std::vector<DetectorInfo> getStoredDetectors();
std::vector<String> readDetectorListFile();
// Reporting
void sendDetectorListEmail();