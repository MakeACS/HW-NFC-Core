#pragma once

#include <Arduino.h>
#include <map>

extern std::map<String, uint32_t> offlineAccessList;

// Expiration timestamps use RTC/Unix epoch seconds; validDays defaults to 30.
void updateOfflineList(String id, uint32_t currentTimestamp, uint32_t validDays = 30);
// Checks stored membership only; call cleanupOfflineList() after time is available to remove expired entries.
bool checkOfflineList(String id);
bool removeOfflineUser(String id);
size_t getOfflineListSize();
void cleanupOfflineList(uint32_t currentTimestamp);

// SPIFFS functions
void saveListToSPIFFS();
void loadListFromSPIFFS();
void deleteListFromSPIFFS();