#pragma once

#include <Arduino.h>
#include <vector>

// Initialize detector storage
bool initDetectorStorage();

// Add detector address to the list (prevents duplicates)
bool addDetectorAddress(const String &address);

// Get all stored detector addresses
std::vector<String> getStoredDetectors();

// Check if address exists
bool detectorExists(const String &address);
