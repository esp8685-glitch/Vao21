#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

// Removal reason enumeration
enum RemovalReason {
    REASON_UNKNOWN = 0,      // Teadmata põhjus
    REASON_TEST_ALARM = 1,   // Test/häire (false alarm)
    REASON_REAL_FIRE = 2     // Päris tulekahju
};

struct DetectorInfo
{
    String address;
    String lastTimestamp;
    uint32_t eventCount;
    RemovalReason removalReason;
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
bool removeDetector(const String &address, RemovalReason reason = REASON_UNKNOWN);
// Test Mode
void setTestMode(bool enabled);
bool getTestMode();
