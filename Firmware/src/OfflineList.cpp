#include "OfflineList.h"
#include <SPIFFS.h>
#include <FS.h>

std::map<String, uint32_t> offlineAccessList;

const char* LIST_FILE_PATH = "/offline_list.txt";
const uint32_t SECONDS_PER_DAY = 86400;

// Adds or updates an entry with a custom expiration date (defaults to 30 days)
void updateOfflineList(String id, uint32_t currentTimestamp, uint32_t validDays) {
    uint32_t expirationTimestamp = currentTimestamp + (validDays * SECONDS_PER_DAY);
    offlineAccessList[id] = expirationTimestamp;
}

// Check if an ID is on the list
bool checkOfflineList(String id) {
    return offlineAccessList.find(id) != offlineAccessList.end();
}

// Remove a specific user from the list (returns true if deleted, false if not found)
bool removeOfflineUser(String id) {
    return offlineAccessList.erase(id) > 0;
}

// Get total count of offline entries
size_t getOfflineListSize() {
    return offlineAccessList.size();
}

// Removes any entry whose expiration timestamp has passed
void cleanupOfflineList(uint32_t currentTimestamp) {
    for (auto it = offlineAccessList.begin(); it != offlineAccessList.end(); ) {
        // Compare current time directly against saved expiration time
        if (currentTimestamp >= it->second) {
            it = offlineAccessList.erase(it); 
        } else {
            ++it;
        }
    }
}

void deleteListFromSPIFFS() {
    Serial.println("Wiping offline list...");
    
    if (SPIFFS.exists(LIST_FILE_PATH)) {
        SPIFFS.remove(LIST_FILE_PATH);
        Serial.println("File deleted from SPIFFS.");
    } else {
        Serial.println("File did not exist in SPIFFS.");
    }
    
    offlineAccessList.clear(); 
}

void loadListFromSPIFFS() {
    Serial.println("Loading offline list from SPIFFS...");
    
    offlineAccessList.clear(); 
    
    if (!SPIFFS.exists(LIST_FILE_PATH)) {
        Serial.println("No offline list found. Starting fresh.");
        return;
    }

    File file = SPIFFS.open(LIST_FILE_PATH, FILE_READ);
    if (!file) {
        Serial.println("Error: Failed to open file for reading.");
        return;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim(); 
        
        if (line.length() > 0) {
            int commaIndex = line.indexOf(',');
            if (commaIndex > 0) {
                String id = line.substring(0, commaIndex);
                String timestampStr = line.substring(commaIndex + 1);
                
                offlineAccessList[id] = strtoul(timestampStr.c_str(), NULL, 10);
            }
        }
    }
    
    file.close();
    Serial.println("Load complete. Total entries: " + String(offlineAccessList.size()));
}

void saveListToSPIFFS() {
    Serial.println("Saving offline list to SPIFFS...");
    delay(50);
    Serial.flush();
    
    File file = SPIFFS.open(LIST_FILE_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("Error: Failed to open file for writing.");
        return;
    }

    Serial.println(F("File opened."));
    delay(50);
    Serial.flush();

    for (const auto& entry : offlineAccessList) {
        file.print(entry.first);
        file.print(",");
        file.println(entry.second);
    }
    
    file.close();
    Serial.println("Save complete.");
}