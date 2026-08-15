/*
The Bus Manager is responsible for everything OneWire related
  * Getting bus temperatures
  * Checking for bus integrity
*/

#include "Globals.h"

//Busmanager has been disabled, will re-visit in future hardware that has stronger OneWire drivers.

/*

#include <Arduino.h>

void BusManager(void *pvParameters){
  unsigned long long OneWireTime = 0;      //Next time we should check the bus.

  while(1){
    //Step 0: Process any OneWire config we have;
    if(configOneWire){
      provisionDeviceViaJSON(ConfigJson);
      configOneWire = 0;
    }
    //Step 1: Check if we are commanded to seal the bus;
    if(reSealBus){
      reSealBus = false;
      discoverDevices();
      saveInventoryToFile();
      sealBroken = false;
      //Set state lockout;
      for(int i = 0; i < channels.count; i++){
        if(channels.states[i] == "FAULT"){
          channels.states[i] = "LOCKED_OUT";
          channels.changeReasons[i] = "LOCAL";
        }
      }
      //Immediately re-run the bus scan;
      OneWireTime = 0;
    }
    //Step 2: Check if it is time to check the bus
    if(OneWireTime <= millis64()){
      OneWireTime = millis64() + 10000; //Check again in 10 seconds
      //Step 2: Scan the bus for devices, make sure they are all there
      checkBusHealth();
      if(sealBroken){
        refreshLiveAddressBuffer();
      }
      //Step 3: Check for any overtemp devices
      updateBusTemperatures();
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void loadInventoryFromFile() {
  if (!SPIFFS.exists("/inventory.dat")) {
    Serial.println("No inventory file found. System is uninitialized.");
    deviceCount = 0;
    return;
  }

  File file = SPIFFS.open("/inventory.dat", FILE_READ);
  int storedCount = file.read();
  
  // Validation: Check if file is empty (-1) or suspiciously large
  if (storedCount < 0) {
    Serial.println("Error: Inventory file is empty or corrupted.");
    deviceCount = 0;
    file.close();
    return;
  }

  deviceCount = (byte)storedCount; 
  
  for (int i = 0; i < deviceCount; i++) {
    // Check if there is actually enough data left in the file for an 8-byte address
    if (file.available() < 8) break; 

    file.read(sensorList[i].address, 8);
    
    // ... rest of your code ...
  }

  file.close();
  Serial.print("Loaded "); Serial.print((int)deviceCount); Serial.println(" devices from SPIFFS.");
}

void parseDeviceMetadata(byte* scratchpad, Device* dev) {
  // 1. High Temp Limit (Byte 2)
  dev->highTempLimit = scratchpad[2];
  if(dev->highTempLimit < 10) {
    Serial.println(F("Failsafe: Overriding OneWire temperature limit."));
    dev->highTempLimit = 50; 
  }

  // 2. Device Mode (Byte 3, Bits 5-3)
  dev->deviceMode = (scratchpad[3] >> 3) & 0x07;

  // 3. Device ID (19 bits total)
  uint32_t id = 0;
  id |= ((uint32_t)scratchpad[7] << 11);
  id |= ((uint32_t)scratchpad[6] << 3);
  id |= (scratchpad[3] & 0x07);
  dev->deviceID = id;
}

void discoverDevices() {
  byte addr[8];
  deviceCount = 0;
  ds.reset_search();
  Serial.println("\n--- Scanning Bus ---");

  while (ds.search(addr) && deviceCount < 10) {
    if (OneWire::crc8(addr, 7) != addr[7]) continue;
    
    // Copy address into struct
    for (int i = 0; i < 8; i++) {
        sensorList[deviceCount].address[i] = addr[i];
    }
    
    // NEW: Read the scratchpad to populate deviceMode, deviceID, and highTempLimit
    byte data[9];
    if (readScratchpad(addr, data)) {
      parseDeviceMetadata(data, &sensorList[deviceCount]);
    } else {
      Serial.println(F("Warning: Failed to read scratchpad during discovery."));
    }
    
    Serial.print("["); Serial.print(deviceCount); Serial.print("] Address: ");
    printAddress(sensorList[deviceCount].address);
    Serial.print(" | Mode: "); Serial.print(sensorList[deviceCount].deviceMode);
    Serial.print(" | ID: "); Serial.println(sensorList[deviceCount].deviceID);
    
    deviceCount++;
  }

  if (deviceCount == 0) {
    Serial.println("No devices found. Bus is empty?");
  } else {
    Serial.print("Scan complete. Found: "); Serial.println(deviceCount);

    //NEW: Set the number of channels based on what type of device was found.
    //This is not working, need to figure out why later.

    for(int i = 0; i < deviceCount; i++){
      //Iterate through each device
      if(sensorList[i].deviceMode == 1){
        //1 channel switch
        if(channels.count < 1){
          channels.count = 1;
        }
      }
      if(sensorList[i].deviceMode == 2){
        //2 channel switch
        if(channels.count < 2){
          channels.count = 2;
        }
      }
      if(sensorList[i].deviceMode == 3){
        //3 channel switch
        if(channels.count < 3){
          channels.count = 3;
        }
      }
      if(sensorList[i].deviceMode == 4){
        //4 channel switch
        if(channels.count = 4){
          channels.count = 4;
        }
      }
    }

  }
}

void saveInventoryToFile() {
  //Saves the current list of debices to SPIFFs, treating it as a valid deployment. 
  File file = SPIFFS.open("/inventory.dat", FILE_WRITE);
  if (!file) {
    Serial.println("Error: Could not create inventory file!");
    return;
  }

  // Record how many devices we are saving
  file.write((byte)deviceCount);
  
  // Save each 8-byte address
  for (int i = 0; i < deviceCount; i++) {
    file.write(sensorList[i].address, 8);
  }
  
  file.close();
  Serial.println("Inventory locked to SPIFFS.");
  sealBroken = false; //Reset the seal
}

void checkBusHealth() {
  byte addr[8];
  bool unexpectedDeviceFound = false;

  // --- 0. Physical Presence Check ---
  byte busEmpty = ds.reset();
  if (busEmpty == 0) {
    // If the bus is empty, there are definitely no unexpected devices.
    if (sealBroken) {
      Serial.println("Bus Health Restored. No unexpected devices present.");
    }
    return;
  }

  // --- 1. Full Bus Scan & Address Matching ---
  ds.reset_search();

  while (ds.search(addr)) {
    if (OneWire::crc8(addr, 7) == addr[7]) {
      bool addressMatched = false;

      // Compare found address against our known inventory
      for (int i = 0; i < deviceCount; i++) {
        if (memcmp(addr, sensorList[i].address, 8) == 0) {
          addressMatched = true;
          break;
        }
      }

      if (!addressMatched) {
        Serial.print("SECURITY ALERT: Unexpected device detected: ");
        printAddress(addr);
        Serial.println();
        unexpectedDeviceFound = true;
      }
    } else {
      Serial.println("Warning: Communication noise detected (CRC Mismatch)");
    }
  }

  // --- 2. channels.states Management ---
  if (unexpectedDeviceFound) {
    // Immediate failure for unexpected hardware
    if (!sealBroken) {
      Serial.println("BUS SEAL RUPTURED! (Unexpected hardware detected)");
      sealBroken = true;
    }
  } else {
    // Perfect scan: No unexpected devices found
    if (sealBroken) {
      Serial.println("Bus Health Restored? All active devices verified.");
    }
  }
}

// Example JSON input: {"mode": 2, "id": 12345, "highTemp": 50}
void provisionDeviceViaJSON(JsonDocument doc) {

  byte mode = doc["mode"] | 0;
  uint32_t id = doc["id"] | 0;
  byte highTemp = doc["highTemp"] | 50;

  // 1. Verify a device is present (Bus must have exactly 1 device)
  if (ds.reset() == 0) {
    Serial.println(F("Provisioning Error: No device found on bus."));
    return;
  }

  // 2. Calculate Byte 3 (T_L)
  // Bit 7: forced to 1 (sign bit, ensures temp is negative to disable alarm)
  // Bit 6: forced to 0
  // Bits 5-3: Mode
  // Bits 2-0: ID LSBs
  byte tl = 0x80 | ((mode & 0x07) << 3) | (id & 0x07);
  
  // 3. Extract Bytes 6 and 7 from the ID
  byte byte6 = (id >> 3) & 0xFF;
  byte byte7 = (id >> 11) & 0xFF;

  // 4. Write data to the Scratchpad
  ds.reset();
  ds.skip();                 // Target the only device attached
  ds.write(0x4E);            // Write Scratchpad Command
  ds.write(highTemp);        // Byte 2: T_H
  ds.write(tl);              // Byte 3: T_L
  ds.write(0x7F);            // Byte 4: Config (12-bit resolution)
  ds.write(0xFF);            // Byte 5: Reserved (Write default 0xFF)
  ds.write(byte6);           // Byte 6: EEPROM 1
  ds.write(byte7);           // Byte 7: EEPROM 2

  // 5. Commit Scratchpad to EEPROM
  ds.reset();
  ds.skip();
  ds.write(0x48);            // Copy Scratchpad Command
  
  // Clones typically require 10-20ms to commit to non-volatile memory
  vTaskDelay(50 / portTICK_PERIOD_MS); 

  Serial.println(F("Device provisioned and locked to EEPROM successfully."));
}

void updateBusTemperatures() {
  //Scans through the bus, gets the temperature of every device.
  ds.reset();
  ds.skip();
  ds.write(0x44); 
  delay(750); 

  //Start by assuming everything is fine. 
  overTemp = false; 

  for (int i = 0; i < deviceCount; i++) {
    byte data[9];
    if (readScratchpad(sensorList[i].address, data)) {
      parseDeviceMetadata(data, &sensorList[i]);
      sensorList[i].isOnline = true; 

      int16_t raw = (data[1] << 8) | data[0];
      sensorList[i].currentTemp = (float)raw / 16.0;
      
      if (sensorList[i].currentTemp >= (float)sensorList[i].highTempLimit) {
        sensorList[i].isAlarming = true;
        //If this specific sensor is alarming, the whole system is Overtemp
        overTemp = true; 
        Serial.println(F("Overtemperature detected!"));
        Serial.print("Device ");
        for(int j = 0; j < 8; j++){
          Serial.print(sensorList[i].address[j]);
          Serial.print(":");
        }
        Serial.print(F(" is at "));
        Serial.print(sensorList[i].currentTemp);
        Serial.print(" with a limit of ");
        Serial.print(sensorList[i].highTempLimit);
        Serial.println("!");
      } else {
        sensorList[i].isAlarming = false;
      }
    } else {
      sensorList[i].isOnline = false; 
    }
  }
}

void refreshLiveAddressBuffer() {
  //Makes a list of all live device addresses for reporting.
  liveAddressCount = 0; // Reset the count
  for (int i = 0; i < deviceCount; i++) {
    // Only include if it's in our SPIFFS inventory AND currently responding
    if (sensorList[i].isOnline) {
      memcpy(liveAddresses[liveAddressCount], sensorList[i].address, 8);
      liveAddressCount++;
    }
  }
}

void printAddress(byte addr[8]) {
  for (int i = 0; i < 8; i++) {
    if (addr[i] < 16) Serial.print("0");
    Serial.print(addr[i], HEX);
    if (i < 7) Serial.print(":");
  }
}

bool readScratchpad(byte addr[8], byte* buffer) {
  ds.reset();
  ds.select(addr);
  ds.write(0xBE); 
  for (int i = 0; i < 9; i++) buffer[i] = ds.read();
  return (OneWire::crc8(buffer, 8) == buffer[8]);
}

*/
