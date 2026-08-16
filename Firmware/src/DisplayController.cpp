/* 


Screen Controller
This task is responsible for talking with any connected screen.


*/

//Print the screen payload to the debug output;
//#define PRINT_SCREEN_PAYLOAD 

#include "Globals.h"
#include "DisplayController.h"
#include "TaggedSerial.h"

namespace {
TaggedSerial<decltype(::Serial)> displaySerial(::Serial, "[display] ");
}

#define Serial displaySerial

namespace {
unsigned long long checkAnnouncements = 600000;
JsonDocument announcementsDocument;
JsonDocument hoursDocument;
unsigned long todayClosingEpoch = 0;
String messageOfTheDay;
}

void sendDisplaychannelState(bool sendRarely, bool sendFrequently);
int readScreenRotation();
bool refreshAnnouncements();
bool refreshHours();
void calculateClosingEpochForToday();
void updateClosingMessageOfTheDay();
uint64_t millis64();
void sendStartupstatusMessage(String statusMessage);

void sendDisplaychannelState(bool sendRarely, bool sendFrequently){
  //Sends the common regular information the screen needs
  JsonDocument CurrentStates;
  if(sendRarely){
    //These are the things we don't need to send often, since they don't change much.
    //Send the current time
    CurrentStates["time"] = rtc.getEpoch();
    //Send WiFi credentials
    if(networkConfiguration.wifiSsid != ""){
      //We have valid WiFi credentials to send;
      CurrentStates["WiFi-Available"] = true;
      CurrentStates["WiFi-wifiSsid"] = networkConfiguration.wifiSsid;
      CurrentStates["WiFi-wifiPassword"] = networkConfiguration.wifiPassword;
      //Temp disabled for memory overrun testing;
      //CurrentStates["TLS-Cert"] = rootCertificate;
    } else{
      CurrentStates["WiFi-Available"] = false;
    }
    //Send the announcements:
    if (!announcementsDocument["list"].isNull()) {
      CurrentStates["announcements"] = announcementsDocument["list"];
    } else {
      // Optional: send an empty array if there are no announcements yet
      CurrentStates["announcements"].to<JsonArray>();
    }
    CurrentStates["makerspace"] = hmiMakerspace;
    CurrentStates["deviceName"] = hmiDeviceName;
    CurrentStates["ACSRole"] = hmiRole;
    CurrentStates["mode"] = inputMode;
    CurrentStates["url"] = networkConfiguration.serverAddress;
    //Send a "station name" to refer to a group of equipment by on multi-channel systems;
    if(channels.count == 1){
      CurrentStates["stationName"] = hmiMachineNames[1];
    } else if (channels.count == 0){
      CurrentStates["stationName"] = "";
    } else{
      CurrentStates["stationName"] = stationName;
    }
    //Send the makerspace's hours;
    if (!hoursDocument["list"].isNull()) {
      CurrentStates["hours"] = hoursDocument["list"];
    } else {
      // Send an empty array if there are no hours yet
      CurrentStates["hours"].to<JsonArray>();
    }
  }
  if(sendFrequently){
    if (welcomeMode) {
      CurrentStates["welcoming"] = true;
    } else {
      CurrentStates["welcoming"] = false;
    }
    CurrentStates["noNetwork"] = networkState.unavailable;
    CurrentStates["messageOfTheDay"] = messageOfTheDay;
    //Channel-related things;
    CurrentStates["channels"] = channels.count;
    JsonArray stateArray = CurrentStates["state"].to<JsonArray>();
    JsonArray deniedReasonArray = CurrentStates["deniedReason"].to<JsonArray>();
    JsonArray expirationArray = CurrentStates["currentAuthExpires"].to<JsonArray>();
    JsonArray machineArray = CurrentStates["deviceNames"].to<JsonArray>();
    JsonArray durationArray = CurrentStates["durations"].to<JsonArray>();
    JsonArray hobbsArray = CurrentStates["hobbsSeconds"].to<JsonArray>();
    for (int i = 0; i < channels.count; i++) {
      stateArray.add(channels.states[i]);
      deniedReasonArray.add(channels.authorizationReasons[i]);
      unsigned long TapExpirationLeft = channels.tapExpirationTimes[i] - millis64();
      if (TapExpirationLeft > 0) {
        expirationArray.add(TapExpirationLeft);
      } else {
        expirationArray.add(0);
      }
      machineArray.add(hmiMachineNames[i]);
      durationArray.add(channels.tapDurations[i] * 1000);
      hobbsArray.add(channels.hobbsSeconds[i]);
    }
    CurrentStates["denied"] = accessDenied;
    CurrentStates["faultMessage"] = faultReason;
    CurrentStates["button"] = resetLed;    //resetLed is a bool normally used for lighting animations, but it tracks with the button.
    CurrentStates["startupMessage"] = "";  //Should be no startup message by the time we make it here.
    CurrentStates["identify"] = identifyRequested;
    CurrentStates["setRotation"] = readScreenRotation();
  }

  CurrentStates["crc"] = 0; //Temporary until we calculate CRC.
  String CurrentToSend;
  serializeJson(CurrentStates, CurrentToSend);
  //Calculate the CRC with the "crc" field set to 0;
  uint32_t checksum = esp_crc32_le(0, (const uint8_t*)CurrentToSend.c_str(), CurrentToSend.length());
  CurrentStates["crc"] = checksum;
  serializeJson(CurrentStates, CurrentToSend);
#ifdef PRINT_SCREEN_PAYLOAD
  Serial.print(F("Sending to screen: "));
  Serial.println(CurrentToSend);
  Serial.flush();
#endif
  Serial0.println(CurrentToSend);
  Serial0.flush();
}

void runScreenController(void *pvParameters){
  
  unsigned long long NextScreenUpdate;


  //One time only; need to ask for all the info about the screen
  //TODO

  //Send our current information
  refreshAnnouncements();
  refreshHours();
  sendDisplaychannelState(true, true);
  delay(100);

  //Before we start regular operation, exit out of the startup screen;
  sendStartupstatusMessage("");

  while(1){

    delay(20);

    //Every 10 minutes, fetch the latest announcements and hours;
    if (checkAnnouncements <= millis64() && networkState.unavailable == false) {
      Serial.println(F("Refreshing announcements and hours from server..."));
      if(refreshAnnouncements() && refreshHours()){
        sendDisplaychannelState(true, true);
        NextScreenUpdate = millis64() + 1000;
        checkAnnouncements = millis64() + 600000;
      }
    }

    //If we are approaching closing time, send an messageOfTheDay;
    updateClosingMessageOfTheDay();

    //Check if it is time for a regular update of the screen
    if(NextScreenUpdate <= millis64()){
      updateScreen = true;
    }

    if(updateScreen){
      NextScreenUpdate = millis64() + 1000;
      sendDisplaychannelState(false, true);
      updateScreen = false;
    }
  }
}

int readScreenRotation() {
  // Read raw values from the LIS2DH12
  int16_t y = accel.getRawY();
  int16_t z = accel.getRawZ();

  // Settings for 12-bit mode (+/- 2g)
  const int minThreshold = 2500; 
  const int hysteresis = 1000; 

  // 'static' stays in memory between function calls
  static int lastValidOrientation = 5; 
  int currentCalculation = 5; 

  // 1. Determine the PHYSICAL state based on current sensor data
  if (abs(z) > (abs(y) + hysteresis) && abs(z) > minThreshold) {
    if (z < -minThreshold) {
      currentCalculation = 3; 
    } else if (z > minThreshold) {
      currentCalculation = 1;
    }
  } 
  else if (abs(y) > (abs(z) + hysteresis) && abs(y) > minThreshold) {
    if (y > minThreshold) {
      currentCalculation = 0; 
    } else if (y < -minThreshold) {
      currentCalculation = 2; 
    }
  }

  // 2. Decide what to return
  // If we are in a "messy" middle state (currentCalculation is 5), 
  // or if the state is exactly the same as before, return 5.
  if (currentCalculation == 5 || currentCalculation == lastValidOrientation) {
    return 5; 
  }

  // 3. If we reached here, it means we have a brand new valid orientation
  lastValidOrientation = currentCalculation;
  
  // Print only when the change actually happens
  Serial.print(F("Screen rotation changed to: "));
  Serial.println(lastValidOrientation);
  
  return lastValidOrientation;
}

bool refreshAnnouncements() {
  bool AnnouncementsUpdated = false;
  if (!networkState.unavailable) {
    networkclient.setCACert(rootCertificate.c_str());  
    Serial.println("Connecting to server...");

    if (networkclient.connect(networkConfiguration.serverAddress.c_str(), 443)) {
      Serial.println("Connected!");

      String payload = R"({"operationName": "GetAnnouncements", "variables": {}, "query": "query GetAnnouncements { getAllAnnouncements { title description linkText linkUrl } }"})";  

      // --- 1. SEND THE HTTP POST REQUEST MANUALLY ---
      networkclient.println("POST /graphql HTTP/1.1");          
      networkclient.print("Host: ");                            
      networkclient.println(networkConfiguration.serverAddress);
      networkclient.println("Content-Type: application/json");  
      networkclient.print("Content-Length: ");                  
      networkclient.println(payload.length());                  
      networkclient.println("Connection: close");               
      networkclient.println();                                  
      networkclient.print(payload);                             

      // --- 2. READ THE HTTP RESPONSE ---
      while (networkclient.connected() && !networkclient.available()) {  
        delay(10);                                                       
      }

      while (networkclient.connected()) {                   
        String line = networkclient.readStringUntil('\n');  
        if (line == "\r") {                                 
          break;                                            
        }
      }

      // --- 3. PARSE THE JSON BODY ---
      String responseBody = networkclient.readString();  

      // Create a temporary document for parsing the raw response
      JsonDocument tempDoc;
      DeserializationError error = deserializeJson(tempDoc, responseBody);

      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        AnnouncementsUpdated = false;
      } else {
        // Clear the global doc before adding new data
        announcementsDocument.clear();

        // Copy the announcements array from the temporary doc to the global doc
        JsonArray announcements = tempDoc["data"]["getAllAnnouncements"];

        // Add it to our global document. We wrap it in an object key for clarity,
        // but you could also just make announcementsDocument directly an array.
        announcementsDocument["list"] = announcements;

        Serial.println("Announcements updated successfully.");
        AnnouncementsUpdated = true;
      }

      networkclient.stop();  

    } else {
      Serial.println("Connection to server failed.");
      AnnouncementsUpdated = false;
    }
  }
  return AnnouncementsUpdated;
}

bool refreshHours() {
  bool HoursUpdated = false;
  if (!networkState.unavailable) {
    networkclient.setCACert(rootCertificate.c_str());
    Serial.println("Connecting to server for hours...");

    if (networkclient.connect(networkConfiguration.serverAddress.c_str(), 443)) {
      Serial.println("Connected!");

      // Construct the URL path using your variable
      String path = "/api/hours/" + String(MakerspaceNumber);

      // --- 1. SEND THE HTTP GET REQUEST MANUALLY ---
      networkclient.print("GET ");
      networkclient.print(path);
      networkclient.println(" HTTP/1.1");

      networkclient.print("Host: ");
      networkclient.println(networkConfiguration.serverAddress);

      // Tell the server to close the connection after responding
      networkclient.println("Connection: close");

      // Send a blank line (\r\n) to indicate the end of the HTTP headers
      networkclient.println();

      // --- 2. READ THE HTTP RESPONSE ---
      // Wait for the server to reply
      while (networkclient.connected() && !networkclient.available()) {
        delay(10);
      }

      // Read headers line by line until we find the empty line
      while (networkclient.connected()) {
        String line = networkclient.readStringUntil('\n');
        if (line == "\r") {
          break;  // Empty line found, headers are done
        }
      }

      // --- 3. PARSE THE JSON BODY ---
      String responseBody = networkclient.readString();

      // Create a temporary document for parsing the raw response
      JsonDocument tempDoc;
      DeserializationError error = deserializeJson(tempDoc, responseBody);

      if (error) {
        Serial.print("Hours JSON parsing failed: ");
        Serial.println(error.c_str());
        HoursUpdated = false;
      } else {
        hoursDocument.clear();

        JsonArray hoursData = tempDoc["obj"];
        
        // --- UPDATED LOGIC: STATIC DAY NAMES ---
        const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        
        // Loop through the JSON array and add the day name based purely on its index
        for (int i = 0; i < hoursData.size(); i++) {
          JsonObject dayObj = hoursData[i];
          
          if (i < 7) { // Safety check to prevent out-of-bounds
            dayObj["dayName"] = dayNames[i]; 
          }
        }
        // ---------------------------------------

        hoursDocument["list"] = hoursData;
        
        // Update the MOTD closing epoch
        calculateClosingEpochForToday(); 

        Serial.println("Hours updated successfully.");
        HoursUpdated = true;
      }

      // Clean up the connection
      networkclient.stop();

    } else {
      Serial.println("Connection to server failed for hours.");
      HoursUpdated = false;
    }
  }
  return HoursUpdated;
}

void calculateClosingEpochForToday() {
  // If no hours exist, or the array doesn't have a full week, reset and abort
  if (hoursDocument["list"].isNull() || hoursDocument["list"].size() < 7) {
    todayClosingEpoch = 0;
    return;
  }
  
  // 1. Get the current local epoch and break it down
  time_t now = rtc.getEpoch();
  struct tm * timeinfo = gmtime(&now); // RTC is local, so gmtime gives local struct
  
  // 2. Get today's day of the week (0 = Sunday, 1 = Monday, ..., 6 = Saturday)
  int todayWday = timeinfo->tm_wday;   

  // 3. Grab today's hours using the weekday index!
  JsonObject todayHours = hoursDocument["list"][todayWday];
  
  // If the shop is closed today, reset and abort
  if (todayHours["closed"].as<bool>()) {
    todayClosingEpoch = 0;
    return;
  }

  // Extract the time string (e.g., "19:00:00")
  String closeTimeStr = todayHours["close"].as<String>();
  if (closeTimeStr == "") {
    todayClosingEpoch = 0;
    return;
  }

  int closeHour = closeTimeStr.substring(0, 2).toInt();
  int closeMin  = closeTimeStr.substring(3, 5).toInt();
  int closeSec  = closeTimeStr.substring(6, 8).toInt();

  // Overwrite the hours, minutes, and seconds with the closing time
  timeinfo->tm_hour = closeHour;
  timeinfo->tm_min  = closeMin;
  timeinfo->tm_sec  = closeSec;
  
  // Re-encode back into an epoch timestamp
  todayClosingEpoch = mktime(timeinfo); 
}
void updateClosingMessageOfTheDay() {
  // If we don't have a valid closing time today (closed, or network error), do nothing.
  if (todayClosingEpoch == 0) {
    messageOfTheDay = "";
    return;
  }

  unsigned long currentEpoch = rtc.getEpoch();

  // Handle the case where we are exactly at or past closing time
  if (currentEpoch >= todayClosingEpoch) {
    unsigned long secondsSinceClose = currentEpoch - todayClosingEpoch;
    // Show the closed message for exactly 60 seconds after closing
    if (secondsSinceClose <= 240) {
      messageOfTheDay = "ALERT: The shop is now closed. Please make your way towards the exit immediately, and do not forget anything.";
    } else {
      messageOfTheDay = "";  // Revert to empty after 1 minute
    }
    return;
  }

  // Handle future closing times
  unsigned long secondsUntilClose = todayClosingEpoch - currentEpoch;

  // 1 Hour (3600 seconds) - window is 3600 down to 3540
  if (secondsUntilClose > 3400 && secondsUntilClose <= 3600) {
    messageOfTheDay = "Reminder: The shop is closing in 1 hour.";
  }
  // 30 Minutes (1800 seconds) - window is 1800 down to 1740
  else if (secondsUntilClose > 1640 && secondsUntilClose <= 1800) {
    messageOfTheDay = "Warning: The shop is closing in 30 minutes. Please start wrapping up.";
  }
  // 15 Minutes (900 seconds) - window is 900 down to 840
  else if (secondsUntilClose > 800 && secondsUntilClose <= 900) {
    messageOfTheDay = "Warning: The shop is closing in 15 minutes. Please start cleaning up your area.";
  }
  // 5 Minutes (300 seconds) - window is 300 down to 240
  else if (secondsUntilClose > 200 && secondsUntilClose <= 300) {
    messageOfTheDay = "Warning: The shop is closing in 5 minutes. Please wrap up cleaning and head towards the exits.";
  }
  // Outside of these 60-second windows, clear the message
  else {
    messageOfTheDay = "";
  }
}