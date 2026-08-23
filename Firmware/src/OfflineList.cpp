#include "OfflineList.h"
#include <SPIFFS.h>
#include <FS.h>

// 1. Actually create the map in memory here (only once!)
std::map<String, uint32_t> offlineAccessList;

// 2. Define your private constants (not accessible outside this file)
const char* LIST_FILE_PATH = "/offline_list.txt";
const uint32_t THIRTY_DAYS_SEC = 2592000;

//3. Functions:

//Save an ID to the offline user's list, with the current timestamp.
void updateOfflineList(String id, uint32_t currentTimestamp) {
    offlineAccessList[id] = currentTimestamp;
}

//Check if an ID is in the offline user's list.
bool checkOfflineList(String id) {
    return offlineAccessList.find(id) != offlineAccessList.end();
}

//Cleanup the offline user's list by removing entries older than 30 days.
void cleanupOfflineList(uint32_t currentTimestamp) {
    for (auto it = offlineAccessList.begin(); it != offlineAccessList.end(); ) {
        if ((currentTimestamp - it->second) > THIRTY_DAYS_SEC) {
            it = offlineAccessList.erase(it); 
        } else {
            ++it;
        }
    }
}

//Delete the offline list from SPIFFS and clear the in-memory map.
void deleteListFromSPIFFS() {
    Serial.println("Wiping offline list...");
    
    if (SPIFFS.exists(LIST_FILE_PATH)) {
        SPIFFS.remove(LIST_FILE_PATH);
        Serial.println("File deleted from SPIFFS.");
    } else {
        Serial.println("File did not exist in SPIFFS.");
    }
    
    // Clear the active memory map
    offlineAccessList.clear(); 
}

//Load the offline list from SPIFFS into the in-memory map.
void loadListFromSPIFFS() {
    Serial.println("Loading offline list from SPIFFS...");
    
    // Clear any existing data in the RAM map just in case
    offlineAccessList.clear(); 
    
    // Check if the file exists before trying to read it
    if (!SPIFFS.exists(LIST_FILE_PATH)) {
        Serial.println("No offline list found. Starting fresh.");
        return;
    }

    File file = SPIFFS.open(LIST_FILE_PATH, FILE_READ);
    if (!file) {
        Serial.println("Error: Failed to open file for reading.");
        return;
    }

    // Read the file line by line
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim(); // Remove any trailing carriage returns (\r)
        
        if (line.length() > 0) {
            int commaIndex = line.indexOf(',');
            if (commaIndex > 0) {
                // Split the string
                String id = line.substring(0, commaIndex);
                String timestampStr = line.substring(commaIndex + 1);
                
                // Add to the map
                offlineAccessList[id] = timestampStr.toInt();
            }
        }
    }
    
    file.close();
    Serial.println("Load complete. Total entries: " + String(offlineAccessList.size()));
}

//Save the in-memory offline list to SPIFFS.
void saveListToSPIFFS() {
    Serial.println("Saving offline list to SPIFFS...");
    
    File file = SPIFFS.open(LIST_FILE_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("Error: Failed to open file for writing.");
        return;
    }

    // Write each key-value pair as "ID,timestamp"
    for (const auto& entry : offlineAccessList) {
        file.print(entry.first);
        file.print(",");
        file.println(entry.second);
    }
    
    file.close();
    Serial.println("Save complete.");
}