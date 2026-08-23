#pragma once

#include <Arduino.h>
#include <map>

// Declare the map as extern so other files know it exists,
// but we wait to actually create it in the .cpp file.
extern std::map<String, uint32_t> offlineAccessList;

// Core functions
void updateOfflineList(String id, uint32_t currentTimestamp);
bool checkOfflineList(String id);
void cleanupOfflineList(uint32_t currentTimestamp);

// SPIFFS functions
void saveListToSPIFFS();
void loadListFromSPIFFS();
void deleteListFromSPIFFS();
