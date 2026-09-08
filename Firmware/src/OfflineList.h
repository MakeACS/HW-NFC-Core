#pragma once

#include <Arduino.h>
#include <map>

extern std::map<String, uint32_t> offlineAccessList;

// Default parameter for validDays must be in the header declaration
void updateOfflineList(String id, uint32_t currentTimestamp, uint32_t validDays = 30);
bool checkOfflineList(String id);
bool removeOfflineUser(String id);
size_t getOfflineListSize();
void cleanupOfflineList(uint32_t currentTimestamp);

// SPIFFS functions
void saveListToSPIFFS();
void loadListFromSPIFFS();
void deleteListFromSPIFFS();