// Handles code related to the ESP-Config library.

#include "Globals.h"
#include "helperFunctions.h"

void startESPConfig();

//Callback functions:
void pingServer(bool pressed);
void pingRIT(bool pressed);
void setSSID(String newSSID);
void setWiFiPass(String newPassword);
void setWiFiChannel(String channel);
void setBSSID(String BSSID);
void disableWiFi(String answer);
void testAccess(bool pressed);
void setChannelCount(String answer);
void setInputMode(String answer);
void setStationName(String answer);
void tapdur(String answer, int channel);
void tapdur0(String answer);
void tapdur1(String answer);
void tapdur2(String answer);
void tapdur3(String answer);
void setFaultResp(String answer);
void setMakerspaceId(String answer);
void setServer(String answer);
void setAPIKey(String answer);
void addToList(String answer);
void deleteFromList(String answer);
void deleteEntireList(bool pressed);
void enableOffline(String answer);
void setDecayTime(String answer);
void managerOffline(String answer);
void checkForOta(bool pressed);
void forceRetryOTA(bool pressed);
void restartDevice(bool pressed);
void setRestartIdle(bool pressed);
void setNewPassword(String answer);
void setNewHint(String answer);
void factoryReset1(bool pressed);
void factoryReset2(bool pressed);
void enableOTA(String answer);
void setOTAURL(String answer);
void setTimezone(String answer);

void startESPConfig(){
    //This handles everything related to starting the ESP-Config system
    String configPassword = "shlug";
    String configHint = "Default password is 'shlug', change it ASAP!";
    if(settings.isKey("config.pass")){
        //Set the config password & hint
        configPassword = settings.getString("config.pass");
        configHint = settings.getString("config.hint");
    } 
    config.begin(configPassword, configHint);
    //SECTION 1: Network
    //Core Network Information
    #ifndef REDUCED_CONFIG
    config.addInformation("Network","state","Network","State","Loading...");
    config.addInformation("Network", "local-ip", "Network", "Local IP", "Loading...");
    config.addInformation("Network","disconnect-reason", "Network", "Disconnect Reason", "Loading...");
    config.addInformation("Network", "net-interface", "Network", "Interface", "Wi-Fi"); //Hard-coded as wifi for now, until we get ethernet fully online.
    //Core WiFi Information
    #endif
    config.addInformation("WiFi", "ssid", "Network", "SSID", networkConfiguration.wifiSsid, "The WiFi name we are connected to.");
    #ifndef REDUCED_CONFIG
    config.addInformation("WiFi", "bssid", "Network", "BSSID", "Loading...", "The unique identifier of the access point we are connected to.");
    config.addInformation("WiFi", "rssi", "Network", "RSSI", "Loading...", "The signal strength of the access point we are connected to.");
    config.addInformation("WiFi", "wifi-channel", "Network", "Channel", "Loading...", "The channel in the 2.4GHz range we are using to connect to the WiFi.");
    //Core TLS Information
    config.addInformation("TLS", "cert-name", "Network", "Common Name", getCertificateCommonName());
    config.addInformation("TLS", "cert-expiration", "Network", "Expires On", getCertificateExpiration());
    //Core MAC Information
    #endif
    config.addInformation("MAC", "wifi-mac", "Network", "WiFi MAC Address", getBaseMacAddress());
    #if CORE_HAS_ETHERNET
    config.addInformation("MAC", "eth-mac", "Network", "Ethernet MAC Address", getEthernetMacAddress());
    #endif
    //Core Network Commands
    #ifndef REDUCED_CONFIG
    config.addButtonCommand("Network", "ping-server", "Network", "Ping Server", pingServer, "This may take up to 10 seconds.");
    config.addButtonCommand("Network",  "ping-rit", "Network", "Ping www.google.com", pingRIT, "This may take up to 10 seconds.");
    #endif
    //Core Network Settings
    config.addStringQuestion("WiFi", "set-ssid", "Network", "Enter WiFi SSID", networkConfiguration.wifiSsid, 32, setSSID, true);
    config.addStringQuestion("WiFi", "set-pass", "Network", "Enter WiFi Password", "(Current Value Not Shown)", 32, setWiFiPass, true);
    #ifndef REDUCED_CONFIG
    config.addIntegerQuestion("WiFi", "set-channel", "Network", "Force a WiFi Channel (0 for auto)", 0, 1, 11, setWiFiChannel, true, true); //Marked unavailable
    config.addStringQuestion("WiFi", "set-bssid", "Network", "Force connection to a set Access Point's BSSID (empty for auto)", "", 64, setBSSID, true, true); //Marked unavailable
    config.addChoiceQuestion("WiFi", "disable-wifi", "Network", "Disable WiFi Interface", "Enabled", {"Disabled", "Enabled"}, disableWiFi, true, true); //Marked unavailable
    #endif

    //SECTION 2: Access Control
    //General Information
    #ifndef REDUCED_CONFIG
    if(CORE_MAX_CHANNELS > 1){
        config.addInformation("General", "station-name", "Access Control", "Station Name", stationName, "Station name is used to name the deployment when multiple pieces of equipment are attached.");
    }
    config.addInformation("General", "mode", "Access Control", "Mode", "Loading...", "How the Core handles when a card is presented/inserted.");
    config.addInformation("General", "current-card", "Access Control", "Current Card", "Waiting...", "The currently inserted/detected card in/on the Core");
    config.addInformation("General", "channel-count", "Access Control", "Channel Count", String(channels.count), "How many access control channels the Core controls.");
    //Repeat Channel Information
    for(int i = 0; i < channels.count; i++){
        //Iterate through the channels, and make an info section for each
        String source = "Channel " + String(i);
        config.addInformation(source, "channel-equipment", "Access Control", "Equipment Name", "Loading..."); //hmiMachineNames[i]
        config.addInformation(source, "channel-state", "Access Control", "State", "Loading..."); //channels.states[i]
        config.addInformation(source, "channel-reason", "Access Control", "Reason", "Loading..."); //channels.changeReasons[i]
        config.addInformation(source, "channel-hobbs", "Access Control", "Hobbs Time (hours)", "Loading...", "How long the equipment has been running for in total");
    }
    //Command
    config.addLatchCommand("", "test-access", "Access Control", "Test Force-On Access Control", testAccess, "WARNING: This will force on all attached equipment until the button is pressed again or the Core is restarted!", "Force the Core to turn on all access control channels, for testing.", false, true, false);
    //Settings
    config.addChoiceQuestion("", "input-mode", "Access Control", "Set the input mode", inputMode, {"TEMP_PRESENT", "INSERT"}, setInputMode, true);
    config.addChoiceQuestion("", "set-interrupt-mode", "Access Control", "Set the interrupt response mode", interruptResponse, {"FAULT", "LOCK_TEMP", "IDLE", "MESSAGE"}, setFaultResp, true, false);
    if(CORE_MAX_CHANNELS > 1){
        config.addIntegerQuestion("Multi-Channel", "set-channels", "Access Control", "Set the number of access control channels", channels.count, 1, CORE_MAX_CHANNELS, setChannelCount, true);
        config.addStringQuestion("Multi-Channel", "set-station-name", "Access Control", "Set the station name", stationName, 32, setStationName, true);
    }
    //Repeat Tap Duration
    if(inputMode != "INSERT"){
        //Need to go through and add questions based on number of channels manually
        //Since we have no way to pass the channel number to the function.
        config.addIntegerQuestion("Tap Durations", "0", "Access Control", "Channel 0 Tap Duration (seconds)", channels.tapDurations[0], 0, 86400, tapdur0, true);
        if(channels.count > 1){
            config.addIntegerQuestion("Tap Durations", "1", "Access Control", "Channel 1 Tap Duration (seconds)", channels.tapDurations[1], 0, 86400, tapdur1, true);
            if(channels.count > 2){
                config.addIntegerQuestion("Tap Durations", "2", "Access Control", "Channel 2 Tap Duration (seconds)", channels.tapDurations[2], 0, 86400, tapdur2, true);
                if(channels.count > 3){
                    config.addIntegerQuestion("Tap Durations", "3", "Access Control", "Channel 3 Tap Duration (seconds)", channels.tapDurations[3], 0, 86400, tapdur3, true);
                    #if CORE_MAX_CHANNELS > 4
                        #error "TOO MANY CHANNELS!"
                    #endif

                }
            }
        }
    }
    #endif

    //Section 3: MakeACS Config
    //Device Information
    #ifndef REDUCED_CONFIG
    config.addInformation("Device", "name", "MakeACS", "Device Name", "Loading...", "Server-assigned device name used to uniquely identify it");
    config.addInformation("Device", "makerspace", "MakeACS", "Makerspace", "Loading...", "The name of the makerspace the Core is assigned to.");
    #if CORE_HAS_SCREEN
    config.addInformation("Device", "makerspace-id", "MakeACS", "Makerspace ID", String(makerspaceId), "The ID of the makerspace the device is connected to, for API-fetching hours.");
    #endif
    #endif
    //API Information
    config.addInformation("API", "server", "MakeACS", "Server", networkConfiguration.serverAddress);
    #ifndef REDUCED_CONFIG
    config.addInformation("API", "api-type", "MakeACS", "API Protocol", "MQTT via WSS");
    #endif
    //Device Setting
    #if CORE_HAS_SCREEN
    config.addIntegerQuestion("Device", "set-makerspace-id", "MakeACS", "Set makerspace ID number for API-fetching hours", makerspaceId, 0, 99, setMakerspaceId, true);
    #endif
    config.addStringQuestion("API", "set-server", "MakeACS", "Set Server URL ('https://make.rit.edu')", networkConfiguration.serverAddress, 64, setServer, true);
    config.addStringQuestion("API", "set-key", "MakeACS", "Enter API key from server.", "(Not Displayed)", 256, setAPIKey, true);
    
    //Section 4: Offline List
    #ifndef REDUCED_CONFIG
    //Information
    config.addInformation("", "offline-count", "Offline List", "Total Count of IDs on Offline List", "TODO");
    config.addInformation("Current ID", "current-id", "Offline List", "Current ID", "Loading...");
    config.addInformation("Current ID", "on-list", "Offline List", "Present on offline list?", "Loading...");
    //Commands
    config.addStringCommand("", "add-list", "Offline List", "Add ID to list", addToList, 64, "", "", false, true);
    config.addStringCommand("", "remove-list", "Offline List", "Delete ID from list", deleteFromList, 64, "", "", false, true);
    config.addButtonCommand("", "purge-list", "Offline List", "Delete Entire Offline List", deleteEntireList, "Are you sure? This cannot be undone!", "", false, true);
    //Settings
    config.addChoiceQuestion("", "enable-offline", "Offline List", "Enable offline list", "Enabled", {"Enabled", "Disabled"}, enableOffline, true, false);
    config.addIntegerQuestion("", "decay-time", "Offline List", "Set offline list decay time (hours)", 168, 24, 8766, setDecayTime, true, false);
    config.addChoiceQuestion("", "manager-offline", "Offline List", "Manager/Admins added permanently to offline list", "Disabled", {"Enabled", "Disabled"}, managerOffline, true);
    #endif

    //Section 5: System
    //Hardware Information
    config.addInformation("Hardware", "version", "System", "Hardware Version", NICE_HARDWARE_NAME);
    config.addInformation("Hardware", "serial", "System", "Serial Number", serialNumber);
    //Firmware Information
    #ifndef REDUCED_CONFIG
    config.addInformation("Firmware", "version", "System", "Firmware Version", FIRMWARE_VERSION);
    config.addInformation("Firmware", "source", "System", "Firmware Source", "https://github.com/MakeACS/HW-NFC-Core");
    config.addInformation("Firmware", "last-ota", "System", "Last OTA Result", "TODO", "The result the last time the system checked for an OTA.");
    config.addInformation("Firmware", "secure-boot", "System", "Secure Boot", "[bad]DISABLED", "Secure boot ensures only valid firmware is run on the device.");
    config.addInformation("Firmware", "secure-boot-key", "System", "Secure Boot Key", "N/A", "Hash of the Secure Boot keey the system checks on firmware");
    #endif
    //Network Information
    config.addInformation("Network", "wifi-mac", "System", "WiFi MAC Address", getBaseMacAddress());
    #if CORE_HAS_ETHERNET
    config.addInformation("Network", "eth-mac", "System", "Ethernet MAC Address", getEthernetMacAddress());
    #endif
    //Uptime Information
    #ifndef REDUCED_CONFIG
    config.addInformation("Uptime", "uptime", "System", "Uptime (seconds)", String(millis64() / 1000));
    config.addInformation("Uptime", "reason", "System", "Last Restart Reason", systemState.resetReason, "What triggered the device to restart last.");
    //Time Information
    config.addInformation("Time", "time", "System", "Current System Time", rtc.getDateTime(true));
    config.addInformation("Time", "timezone", "System", "Timezone (hours)", settings.getString("system.timezone"));
    //OTA Commands
    config.addButtonCommand("OTA", "check", "System", "Check for OTA", checkForOta, "This may take up to 10 seconds.");
    //TODO: Should be available only if we skipped a bad OTA
    config.addButtonCommand("OTA", "retry", "System", "Force Retry Reverted OTA", forceRetryOTA, "This will restart the device and attempt to install the OTA.", "", true, true, true); 
    //Restart Commands
    config.addButtonCommand("Restart", "restart", "System", "Restart Device", restartDevice, "Are you sure? This will immediately restart the device.", "", true);
    config.addLatchCommand("Restart", "restart-idle", "System", "Restart Next Time Not In Use", setRestartIdle, "If the device is currently not in use, it will restart immediately!", "");
    #endif
    //Password Commands
    config.addStringCommand("Password", "new-password", "System", "Set new config password", setNewPassword, 32, "Make sure you know the password! Once this is set, you need it again to change it!", "", false, true);
    config.addStringCommand("Password", "new-hint", "System", "Set new password hint", setNewHint, 128, "", "", false, true);
    //Factory Reset
    config.addLatchCommand("Factory Reset", "reset", "System", "Factory Reset", factoryReset1, "DANGER: a factory reset will wipe all data from the device! Are you sure you want to proceed?", "Resets the device to its original factory state", false, true, false);
    config.addButtonCommand("Factory Reset", "confirm", "System", "Confirm Factory Reset", factoryReset2, "FINAL WARNING: once you confirm, the device will immediately be wiped! There is no undo!", "", true, true, true);
    //OTA Settings
    #ifndef REDUCED_CONFIG
    config.addChoiceQuestion("OTA", "enable", "System", "Enable Automatic OTA at Startup?", "Enabled", {"Enabled", "Disabled"}, enableOTA, true);
    config.addStringQuestion("OTA", "url", "System", "Set OTA JSON URL", "https://github.com/MakeACS/HW-NFC-Core/blob/main/Firmware/OTADirectory.json", 128, setOTAURL, true);
    //Time
    config.addIntegerQuestion("Time", "timezone-set", "System", "Set Timezone (Hours +/- UTC)", settings.getString("system.timezone").toInt(), -12, 12, setTimezone, true);
    #endif
}

#ifndef REDUCED_CONFIG
void updateConfig(){
    //This function called regularly will update the frontend with any changed information.
    //RSSI:
    if(!networkState.unavailable){
        config.updateInformation("WiFi", "rssi", String(WiFi.RSSI()));
    } else{
        config.updateInformation("WiFi", "rssi", "Network Unavailable");
    }
    //Hobbs Time:
    for(int i = 0; i < channels.count; i++){
        String source = "Channel " + String(i);
        float hobbsHours = channels.hobbsSeconds[i] / 3600;
        config.updateInformation(source, "channel-hobbs", String(hobbsHours));
    }
}
#endif

//Callback funcntions:
void pingServer(bool pressed){
    //Called when the order to ping the server comes in.
    if(!pressed) return;
    config.updateCommand("Network", "ping-server", "[time] Pinging server, standby...");
    config.update(); //Force an update from within the function.
    if(Ping.ping(networkConfiguration.serverAddress.c_str())){
        config.updateCommand("Network", "ping-server", "[good]Successful ping at [time].");
    } else{
        config.updateCommand("Network", "ping-server", "[bad]Failed to ping server at [time].");
    }
}
void pingRIT(bool pressed){
    //Called when the order to pin www.rit.edu (to test general network connectivity) comes in.
    if(!pressed) return;
    config.updateCommand("Network", "ping-rit", "[time] Pinging www.rit.edu, standby...");
    config.update(); //Force an update from within the function.
    if(Ping.ping("www.google.com")){
        config.updateCommand("Network", "ping-rit", "[good]Successful ping at [time].");
    } else{
        config.updateCommand("Network", "ping-rit", "[bad]Failed to ping www.rit.edu at [time].");
    }
}
void setSSID(String newSSID){
    //Called to set a new SSID
    settings.putString("net.ssid", newSSID);
    if(newSSID == settings.getString("net.ssid")){
        config.updateQuestion("WiFi", "set-ssid", newSSID, "[good]Successsully updated SSID. Restart device to apply settings.");
    } else{
        config.updateQuestion("WiFi", "set-ssid", "ERROR", "[bad]Failed to set new SSID?");
    }
}
void setWiFiPass(String newPassword){
    //Called to set a new network password
    if(newPassword.equalsIgnoreCase("(Current Value Not Shown)") || newPassword.equalsIgnoreCase("********")) return;
    settings.putString("net.password", newPassword);
    if(newPassword == settings.getString("net.password")){
        config.updateQuestion("WiFi", "set-pass", "********", "[good] Successsully updated WiFi password. Restart device to apply settings.");
    } else{
        config.updateQuestion("WiFi", "set-pass", "ERROR", "[bad] Failed to set new WiFi password?");
    }
}
void setWiFiChannel(String channel){
    //Called to force a wifi channel
    //or set to 0 for auto mode
    //Currently does nothing!
    int newChannel = channel.toInt();
}
void setBSSID(String BSSID){
    //Called to force a specific BSSID
    //or set empty for auto mode
    //Currently does nothing!
    return;
}
void disableWiFi(String answer){
    //Called to enable/disable WiFi interface
    //Currently does nothing!
    if(answer.equalsIgnoreCase("Enabled")){
        //Enable the interface
    } else if(answer.equalsIgnoreCase("Disabled")){
        //Disables the interface
    }
}
void testAccess(bool pressed){
    //Called to test the access control system, enables all channels.
    //Currently does nothing.
    if(pressed){
        //Start the test
        config.updateCommand("Test Access Control", "test-access", "[bad] DANGER: Test started at [time]. All channels online!");
    } else{
        //End the test
        config.updateCommand("Test Access Control", "test-access", "[good] Test completed. Regular operation resumed.");
    }
}
void setChannelCount(String answer){
    //Set the channel count on multi-channel devices.
    //TODO: Implement! 
    int newCount = answer.toInt();
}
void setInputMode(String answer){
    //Set the input mode
    String newMode = "error";
    if(answer.equalsIgnoreCase("tap")){
        //Tap mode
        newMode = "TEMP_PRESENT";
    }
    if(answer.equalsIgnoreCase("insert")){
        //Insert mode
        newMode = "INSERT";
    }
    if(!newMode.equalsIgnoreCase("error")){
        //Set the new value
        settings.putString("access.input", answer);
        if(newMode == settings.getString("access.input")){
            //Success!
            config.updateQuestion("", "input-mode", newMode, "[good] Updated input mode! Restart to apply changes.");
        } else{
            //Failure?
            config.updateQuestion("", "input-mode", answer, "[bad] Failed to save setting?");
        }
    } else{
        //We got a bad input?
        config.updateQuestion("", "input-mode", answer, "[bad] Got an invalid input?");
    }
}
void setStationName(String answer){
    //Set the station name
    settings.putString("station.name", answer);
    if(settings.getString("station.name") == answer){
        //Success!
        config.updateQuestion("Multi-Channel", "set-station-name", answer, "[good] Updated station name! Restart to apply changes.");
    } else{
        //Failure?
        config.updateQuestion("Multi-Channel", "set-station-name", answer, "[bad] Failed to save setting?");
    }
}
void tapdur(String answer, int channel){
    //Sets the tap duration per-channel
    int newDuration = answer.toInt();
    String key = "channels.tap" + String(channel);
    settings.putUInt(key.c_str(), newDuration);
    if(settings.getUInt(key.c_str()) == newDuration){
        //Success
        config.updateQuestion("Tap Durations", String(channel), String(newDuration), "[good] Saved new tap duration. Restart to apply settings.");
    } else{
        //Failure
        config.updateQuestion("Tap Durations", String(channel), String(newDuration), "[bad] Failed to save to memory?");
    }
}
void tapdur0(String answer){
    tapdur(answer, 0);
}
void tapdur1(String answer){
    tapdur(answer, 1);
}
void tapdur2(String answer){
    tapdur(answer, 2);
}
void tapdur3(String answer){
    tapdur(answer, 3);
}
void setFaultResp(String answer){
    //Set how we handle fault conditions
    settings.putString("access.intResp", answer);
    if(settings.getString("access.intResp") == answer){
        //Success!
        config.updateQuestion("", "set-interrupt-mode", answer, "[good] Updated interrupt response mode! Restart to apply changes.");
    } else{
        //Failure?
        config.updateQuestion("", "set-interrupt-mode", answer, "[bad] Failed to save setting?");
    }
}
void setMakerspaceId(String answer){
    //Set the makerspace ID, used for getting the hours from the website
    settings.putString("makerspace.id", answer);
    if(settings.getString("makerspace.id") == answer){
        //Success!
        config.updateQuestion("Device", "set-makerspace-id", answer, "[good] Updated makerspace ID! Restart to apply changes.");
    } else{
        //Failure?
        config.updateQuestion("Device", "set-makerspace-id", answer, "[bad] Failed to save setting?");
    }
}
void setServer(String answer){
    //Set the server address for the API to interface with
    settings.putString("net.server", answer);
    if(settings.getString("net.server") == answer){
        //Success!
        config.updateQuestion("API", "set-server", answer, "[good] Updated server! Restart to apply changes.");
    } else{
        //Failure?
        config.updateQuestion("API", "set-server", answer, "[bad] Failed to save setting?");
    }
}
void setAPIKey(String answer){
    //Set the API key
    settings.putString("net.mqttKey", answer);
    if(settings.getString("net.mqttKey") == answer){
        //Success!
        config.updateQuestion("API", "set-api-key", answer, "[good] Updated API Key! Restart to apply changes.");
    } else{
        //Failure?
        config.updateQuestion("API", "set-api-key", answer, "[bad] Failed to save setting?");
    }
}
void addToList(String answer){
    //Add this user to the offline list

}
void deleteFromList(String answer){
    //Delete this user from the offline list

}
void deleteEntireList(bool pressed){
    //Delete the entire offline list

}
void enableOffline(String answer){
    //Enable/disable the entire offline list functionality

}
void setDecayTime(String answer){
    //Set the offline list decay time, in hours

}
void managerOffline(String answer){
    //Enable/disable managers and admins never being removed from the list. 
}
void checkForOta(bool pressed){
    //Check for an OTA, and report if there is anything new.
}
void forceRetryOTA(bool pressed){
    //Force retry an invalid OTA
}
void restartDevice(bool pressed){
    //Immediately restart the device
    systemState.resetReason = "Requested (Config Software)";
    systemState.requestReset = true;
}
void setRestartIdle(bool pressed){
    //Set the "restart when unused" flag
    if(pressed){
        restartWhenUnused = true;
        config.updateCommand("Restart", "restart-idle", "[bad]WARNING: Device will restart immediately next time it is not in use!");
    } else{
        restartWhenUnused = false;
        config.updateCommand("Restart", "restart-idle", "Disabled restart when unused.");
    }
}
void setNewPassword(String answer){
    //Set a new config password
    String message = "idk";
    settings.putString("config.pass", answer);
    if(settings.getString("config.pass") == answer){
        message = "[good]Successfully set password to: ->" + answer + "<-. It will be required on next configuration.";
    } else{
        message = "[bad]ERROR: Unable to set new password?";
    }
    config.updateCommand("Password", "new-password", message);
}
void setNewHint(String answer){
    //Sets a new hint for the password
    String message = "idk";
    settings.putString("config.hint", answer);
    if(settings.getString("config.hint") == answer){
        message = "[good]New hint: '" + answer + "'.";
    } else{
        message = "[bad]ERROR: Unable to set new hint?";
    }
    config.updateCommand("Password", "new-password", message);
}
void factoryReset1(bool pressed){
    //button 1 of 2 to do a factory reset
    if(pressed){
        config.updateCommand("Factory Reset", "reset", "[bad]DANGER: Factory Reset Safety Removed! Press again to abort!");
        config.updateCommandAvailability("Factory Reset", "confirm", false);
    } else{
        config.updateCommand("Factory Reset", "reset", "[good]Factory Reset Safety Re-Engaged.");
        config.updateCommandAvailability("Factory Reset", "confirm", true);
    }
}
void factoryReset2(bool pressed){
    //button 2 of 2 of doing a factory reset
    if(pressed){
        config.updateCommand("Factory Reset", "confirm", "[bad]ALERT: Factory Reset in Progress!");
        config.updateCommandAvailability("Factory Reset", "reset", true);
        //TODO actually factory reset here.
    }
}
void enableOTA(String answer){
    //"Enabled" "Disabled" OTA on boot
}
void setOTAURL(String answer){
    //Set a new OTA URL
}
void setTimezone(String answer){
    //Set a new timezone (-12 to 12)

}