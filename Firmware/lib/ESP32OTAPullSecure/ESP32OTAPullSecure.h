/*
ESP32-OTA-Pull - a library for doing "pull" based OTA ("Over The Air") firmware
updates, where the image updates are posted on the web.

MIT License
Jim Heaney, 2026
Based on 2022-3 Mikal Hart
(Modified for HTTPS, Semantic Versioning, Hardware Rollback, MD5 Hashing, New JSON Formatting, and ESP32 Core 3.x Support)
*/

#pragma once
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

class ESP32OTAPull
{
public:
    enum ActionType { DONT_DO_UPDATE, UPDATE_BUT_NO_BOOT, UPDATE_AND_BOOT };

    // Return codes from CheckForOTAUpdate (Offset to -100 to avoid native HTTPClient collision)
    enum ErrorCode { SKIPPED_BAD_VERSION = -104,UPDATE_AVAILABLE = -103, NO_UPDATE_PROFILE_FOUND = -102, NO_UPDATE_AVAILABLE = -101, UPDATE_OK = 0, HTTP_FAILED = 1, WRITE_ERROR = 2, JSON_PROBLEM = 3, OTA_UPDATE_FAIL = 4 };

private:
    void (*Callback)(int offset, int totallength) = NULL;
    ActionType Action = UPDATE_AND_BOOT;
    String TargetFilename = ""; // Formerly Config
    String CVersion   = "";
    bool DowngradesAllowed = false;
    bool SerialDebug = false;
    
    // HTTPS settings
    const char* RootCACert = nullptr;
    bool InsecureHTTPS = false;

    bool BetaChannel = false;
    bool BetaLoaded = false;

    // Returns the text after the first '-' (e.g. "beta1" for "3.2.0-beta1"), or "" if none
    static String versionSuffix(const String &v) {
        int dash = v.indexOf('-');
        if (dash < 0) return "";
        String s = v.substring(dash + 1);
        s.trim();
        return s;
    }

    // Decides whether the advertised version should be installed. Returns true to update.
    bool shouldInstall(const String &remote, const String &current) {
        if (remote.isEmpty()) return true;

        String remoteSuffix = versionSuffix(remote);
        String currentSuffix = versionSuffix(current);
        int cmp = compareVersions(remote, current); // numeric part only

        if (!isBeta()) {
            // Production devices never install pre-release builds
            if (!remoteSuffix.isEmpty()) {
                if (SerialDebug) Serial.println("OTA: Skipping beta firmware on production device.");
                return false;
            }
            // A production device running a beta build always moves to the production build
            if (!currentSuffix.isEmpty()) return true;
            return cmp > 0 || (DowngradesAllowed && cmp != 0);
        }

        if (cmp > 0) return true;
        if (cmp < 0) return DowngradesAllowed;

        // Same numeric version: install if the suffix differs (beta -> production, or beta -> other beta).
        // Never move from production to a beta of the same version.
        if (remoteSuffix == currentSuffix) return false;
        if (currentSuffix.isEmpty()) return false;
        return true;
    }

    int compareVersions(String v1, String v2) {
        if (SerialDebug) Serial.printf("OTA Math -> Comparing JSON Version: '%s' vs Current Version: '%s'\n", v1.c_str(), v2.c_str());

        // Ignore any "-suffix" for numeric comparison
        int dash1 = v1.indexOf('-');
        if (dash1 >= 0) v1 = v1.substring(0, dash1);
        int dash2 = v2.indexOf('-');
        if (dash2 >= 0) v2 = v2.substring(0, dash2);
        
        int idx1 = 0, idx2 = 0;
        
        while (idx1 >= 0 || idx2 >= 0) {
            int next1 = (idx1 >= 0) ? v1.indexOf('.', idx1) : -1;
            int next2 = (idx2 >= 0) ? v2.indexOf('.', idx2) : -1;
            
            String part1 = (idx1 >= 0) ? ((next1 == -1) ? v1.substring(idx1) : v1.substring(idx1, next1)) : "0";
            String part2 = (idx2 >= 0) ? ((next2 == -1) ? v2.substring(idx2) : v2.substring(idx2, next2)) : "0";
            
            part1.trim(); 
            part2.trim();
            
            int num1 = part1.toInt();
            int num2 = part2.toInt();
            
            if (SerialDebug) Serial.printf("  Evaluating chunk -> JSON: %d | Current: %d\n", num1, num2);
            
            if (num1 > num2) {
                if (SerialDebug) Serial.println("  Result: JSON version is NEWER. Update triggered.");
                return 1;
            }
            if (num1 < num2) {
                if (SerialDebug) Serial.println("  Result: JSON version is OLDER. Update skipped.");
                return -1;
            }
            
            idx1 = (next1 == -1) ? -1 : next1 + 1;
            idx2 = (next2 == -1) ? -1 : next2 + 1;
        }
        
        if (SerialDebug) Serial.println("  Result: Versions are IDENTICAL. Update skipped.");
        return 0; 
    }

int DoOTAUpdate(const char* URL, const char* expectedMD5, ActionType Action)
    {
        // 1. SECURE THE UPDATE BUFFER FIRST (Before the TLS handshake consumes the heap)
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            if (SerialDebug) Serial.printf("Failed to allocate Update buffer: %s\n", Update.errorString());
            return OTA_UPDATE_FAIL;
        }

        // Enforce MD5 hash validation during the flash process
        if (expectedMD5 != nullptr && strlen(expectedMD5) > 0) {
            Update.setMD5(expectedMD5);
        }

        HTTPClient http;
        WiFiClientSecure secureClient;
        WiFiClient client;
        
        NetworkClient* streamClient = &client;

        if (String(URL).startsWith("https")) {
            secureClient.setInsecure();
            secureClient.setTimeout(15); 
            streamClient = &secureClient;
        }

        if (SerialDebug) {
            Serial.printf("Attempting firmware download from: %s\n", URL);
            if (expectedMD5 != nullptr && strlen(expectedMD5) > 0) {
                Serial.printf("Enforcing MD5 hash check: %s\n", expectedMD5);
            } else {
                Serial.println("WARNING: No MD5 hash provided. Relying solely on TLS (if applicable).");
            }
        }

        http.useHTTP10(false);       
        http.begin(*streamClient, URL);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); 
        http.setTimeout(15000);

        // 2. EXECUTE THE GET REQUEST (The heavy TLS allocations happen here)
        int httpResponseCode = http.GET();

        if (SerialDebug) {
            Serial.printf("Firmware download HTTP response: %d\n", httpResponseCode);
            if (httpResponseCode < 0) {
                Serial.printf("HTTP Error Reason: %s\n", http.errorToString(httpResponseCode).c_str());
            }
        }

        if (httpResponseCode == 200)
        {
            int totalLength = http.getSize();
            uint8_t buff[1280] = { 0 };
            Stream* stream = http.getStreamPtr();

            int offset = 0;
            while (http.connected() && offset < totalLength)
            {
                size_t sizeAvail = stream->available();
                if (sizeAvail > 0)
                {
                    size_t bytes_to_read = min(sizeAvail, sizeof(buff));
                    size_t bytes_read = stream->readBytes(buff, bytes_to_read);
                    size_t bytes_written = Update.write(buff, bytes_read);
                    if (bytes_read != bytes_written)
                    {
                        if(SerialDebug) Serial.printf("Unexpected error in OTA: %d %d %d\n", bytes_to_read, bytes_read, bytes_written);
                        break;
                    }
                    offset += bytes_written;
                    if (Callback != NULL) Callback(offset, totalLength);
                }
            }

            if (offset == totalLength)
            {
                if (Update.end(true)) {
                    delay(1000);
                    if (Action == UPDATE_BUT_NO_BOOT) return UPDATE_OK;
                    ESP.restart();
                } else {
                    if (SerialDebug) Serial.printf("OTA UPDATE ABORTED: MD5 Hash Mismatch! (%s)\n", Update.errorString());
                    return WRITE_ERROR;
                }
            } else {
                // Make sure to abort if the connection dropped mid-download
                Update.abort(); 
            }
            return WRITE_ERROR;
        }

        // 3. ABORT UPDATE IF HTTP FAILED TO FREE THE RESERVED BUFFER
        Update.abort(); 
        http.end();
        return httpResponseCode;
    }

public:
    String GetVersion() { return CVersion; }
    
    // Cleaned up unused config/device/board setters
    ESP32OTAPull &SetTargetFilename(const char *filename) { TargetFilename = filename; return *this; }
    
    ESP32OTAPull &AllowDowngrades(bool allow_downgrades) { DowngradesAllowed = allow_downgrades; return *this; }
    // Beta devices also accept "x.y.z-suffix" versions. Default (false) is production only.
    // The setting is persisted in the "ota_prefs" namespace and survives reboots.
    ESP32OTAPull &SetBetaChannel(bool beta = true)
    {
        if (isBeta() != beta) {
            Preferences prefs;
            prefs.begin("ota_prefs", false);
            prefs.putBool("beta", beta);
            prefs.end();
            BetaChannel = beta;
        }
        return *this;
    }

    // True if this device is configured for beta firmware (loaded from flash on first use)
    bool isBeta()
    {
        if (!BetaLoaded) {
            Preferences prefs;
            prefs.begin("ota_prefs", false); // read-write so the namespace is created if missing
            BetaChannel = prefs.getBool("beta", false);
            prefs.end();
            BetaLoaded = true;
        }
        return BetaChannel;
    }
    ESP32OTAPull &SetCallback(void (*callback)(int offset, int totallength)) { Callback = callback; return *this; }
    void EnableSerialDebug() { SerialDebug = true; }

    ESP32OTAPull &SetCACert(const char* cert) { RootCACert = cert; return *this; }
    ESP32OTAPull &SetInsecureHTTPS(bool insecure = true) { InsecureHTTPS = insecure; return *this; }


    //Run this ASAP on boot to verify installed firmware works. 
    //Returns TRUE if verification was needed and succeeded, FALSE if verification was not needed.
    //If verification fails, the ESP32 will reboot and rollback to the previous firmware (i.e. no return). 
    bool VerifyOrRevert(const char* JSON_URL, const char* currentVersion)
    {
        // --- Hardware-level check to see if verification is actually needed ---
        esp_ota_img_states_t ota_state;
        const esp_partition_t *running_partition = esp_ota_get_running_partition();
        
        if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
            if (ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
                if (SerialDebug) Serial.println("OTA Verification skipped: Firmware is not in a pending verification state (likely USB flash or already verified).");
                // Returning FALSE indicates that verification was NOT needed
                return false; 
            }
        }
        // ---------------------------------------------------------------------------

        uint32_t startTime = millis();
        bool success = false;
        
        while (millis() - startTime < 60000) {
            HTTPClient http;
            WiFiClientSecure secureClient;
            WiFiClient client;
            
            NetworkClient* streamClient = &client;

            if (String(JSON_URL).startsWith("https")) {
                if (InsecureHTTPS) secureClient.setInsecure();
                else if (RootCACert != nullptr) secureClient.setCACert(RootCACert);
                streamClient = &secureClient;
            }

            http.begin(*streamClient, JSON_URL);
            int code = http.GET();
            http.end();

            if (code == 200) {
                success = true;
                if (SerialDebug) Serial.println("OTA Verification successful.");
                break;
            }
            
            if (SerialDebug) Serial.println("OTA Verification failed, retrying...");
            delay(2000); 
        }

        if (success) {
            esp_ota_mark_app_valid_cancel_rollback();
            if (SerialDebug) Serial.println("App marked valid at hardware level. Rollback cancelled.");
            
            // Returning TRUE indicates verification WAS needed and succeeded
            return true;
        } else {
            if (SerialDebug) Serial.println("Verification timed out. Triggering hardware rollback!");
            
            Preferences prefs;
            prefs.begin("ota_prefs", false);
            prefs.putString("bad_ver", currentVersion);
            prefs.end();

            // This function halts execution and restarts the ESP32.
            esp_ota_mark_app_invalid_rollback_and_reboot();
            
            // This return is unreachable due to the reboot, but is required by the compiler.
            return false;
        }
    }

    int CheckForOTAUpdate(const char* JSON_URL, const char *CurrentVersion, ActionType Action = UPDATE_AND_BOOT)
    {
        CurrentVersion = CurrentVersion == NULL ? "" : CurrentVersion;

        String targetURL = "";
        String targetMD5 = "";
        bool foundProfile = false;
        bool shouldUpdate = false;
        bool skippedBadVersion = false; // <-- NEW: Track if we skipped a bad version

        // SCOPE BLOCK: Fetch and Parse JSON, then destroy connection to free ~40KB of heap
        {
            HTTPClient http;
            WiFiClientSecure secureClient;
            WiFiClient client;
            
            NetworkClient* streamClient = &client;

            if (String(JSON_URL).startsWith("https")) {
                if (InsecureHTTPS) secureClient.setInsecure();
                else if (RootCACert != nullptr) secureClient.setCACert(RootCACert);
                streamClient = &secureClient;
            }

            http.useHTTP10(true);
            http.begin(*streamClient, JSON_URL);
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            
            int httpResponseCode = http.GET();
            
            if (SerialDebug) Serial.printf("Got JSON HTTP Response: %d\n", httpResponseCode);

            if (httpResponseCode != 200) {
               return httpResponseCode > 0 ? httpResponseCode : HTTP_FAILED;
            }

            // Download the payload to a String first for stability
            String payload = http.getString();

            http.end();     
            secureClient.stop();
            client.stop();

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (error) {
                if (SerialDebug) {
                    Serial.printf("deserializeJson() failed: %s\n", error.f_str());
                    Serial.println("--- START OF RECEIVED PAYLOAD ---");
                    Serial.println(payload); 
                    Serial.println("--- END OF RECEIVED PAYLOAD ---");
                }
                return JSON_PROBLEM;
            }

            Preferences prefs;
            prefs.begin("ota_prefs", true); 
            String badVer = prefs.getString("bad_ver", "");
            prefs.end();

            String _TargetName = TargetFilename.isEmpty() ? "" : TargetFilename;

            // Production lives at the JSON root; the optional "beta" object is only read by beta devices
            CVersion = doc["version"].isNull() ? "" : (const char *)doc["version"];

            String selectedVersion = "";
            JsonObject sections[2] = { doc.as<JsonObject>(), isBeta() ? doc["beta"].as<JsonObject>() : JsonObject() };

            for (int s = 0; s < 2; s++)
            {
                JsonObject section = sections[s];
                if (section.isNull()) continue;
                if (s == 1 && section["version"].isNull()) continue; // beta needs an explicit version
                if (s == 1 && versionSuffix((const char *)section["version"]).isEmpty()) {
                    if (SerialDebug) Serial.println("OTA: Ignoring beta section: version has no '-suffix' (e.g. 3.2.1-beta1).");
                    continue;
                }

                String sectionVersion = section["version"].isNull() ? "" : (const char *)section["version"];

                for (auto firmware : section["firmwares"].as<JsonArray>())
                {
                    String FName = firmware["name"].isNull() ? "" : (const char *)firmware["name"];

                    // Check for an EXACT match between the JSON name and your target name
                    if (_TargetName.isEmpty() || FName != _TargetName) continue;

                    foundProfile = true;

                    if (sectionVersion == badVer && !badVer.isEmpty()) {
                        if (SerialDebug) Serial.println("Skipping version: previously failed/reverted.");
                        skippedBadVersion = true;
                        break;
                    }

                    if (!shouldInstall(sectionVersion, String(CurrentVersion))) break;

                    // If both channels qualify, take the higher number; ties go to production (checked first)
                    if (shouldUpdate && !selectedVersion.isEmpty() && compareVersions(sectionVersion, selectedVersion) <= 0) break;

                    targetURL = firmware["url"].isNull() ? "" : (const char *)firmware["url"];
                    targetMD5 = firmware["md5"].isNull() ? "" : (const char *)firmware["md5"];
                    selectedVersion = sectionVersion;
                    shouldUpdate = true;
                    break;
                }
            }
            if (shouldUpdate) CVersion = selectedVersion;
        } // End Scope Block

        if (!foundProfile) return NO_UPDATE_PROFILE_FOUND;
        
        // --- NEW: Return our specific code if we didn't update because of a bad version ---
        if (skippedBadVersion && !shouldUpdate) return SKIPPED_BAD_VERSION; 
        
        if (!shouldUpdate) return NO_UPDATE_AVAILABLE;
        if (Action == DONT_DO_UPDATE) return UPDATE_AVAILABLE;
        
        if (SerialDebug) Serial.println("JSON parsed and memory freed. Handing off to DoOTAUpdate...");
        return DoOTAUpdate(targetURL.c_str(), targetMD5.c_str(), Action);
    }
};