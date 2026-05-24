#pragma once

#include <Arduino.h>
#include <vector>

// Initialize detector storage
bool initDetectorStorage();

// Add detector address to the list (prevents duplicates)
bool addDetectorAddress(const String &address);

// Get all stored detector addresses
std::vector<String> getStoredDetectors();

// Read detector addresses directly from the SD file
std::vector<String> readDetectorListFile();

// Check if address exists
bool detectorExists(const String &address);
void sendDetectorListEmail();
bool clearDetectorList();
