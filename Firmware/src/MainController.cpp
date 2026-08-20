/*

----- COREDUINO -----

Coreduino is an Arduino-based firmware for the MakeACS accessEnabled Control Core. 

It is meant to run in the PIOArduino framework, allowing building for all variants of the Core.

Licensed CERN-OHL-S 2.0 (https://ohwr.org/cern/ohl-s-2.0)

More info: https://github.com/MakeACS/HW-NFC-Core

*/

#include "version.h" //Includes the version information, pulls from Github tag on release.

#include <Arduino.h>
#include "Globals.h"
#include "AudioVisualController.h"
#include "ConfigurationController.h"
#include "DisplayController.h"
#include "FrontendController.h"
#include "MachineStateController.h"
#include "TaggedSerial.h"

namespace {
TaggedSerial<decltype(::Serial)> mainSerial(::Serial, "[main] ");
}

#define Serial mainSerial

//Libraries:
  //TODO: Go through and put all of these into the proper library system for PIOArduino.
  #include <OneWire.h>              //Replacing for WSACS API update...
  #include <ArduinoJson.h>          //Version 7.3.0 | Source: https://github.com/bblanchon/ArduinoJson
  #include <ArduinoJson.hpp>        //Version 7.3.0 | Source: https://github.com/bblanchon/ArduinoJson
  #include <WiFiClientSecure.h>     //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <HTTPClient.h>           //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <Preferences.h>          //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <esp_wifi.h>             //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <FS.h>                   //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <SPIFFS.h>               //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <Update.h>               //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <WiFi.h>                 //Version 3.1.1 | Inherent to ESP32 Arduino
  #include "esp_timer.h"            //Version 3.1.1 | Inherent to ESP32 Arduino
#if CONFIG_IDF_TARGET_ESP32S3
  #include "esp32s3/rom/rtc.h"      //Version 3.1.1 | Inherent to ESP32 Arduino
#endif
  #include <nvs_flash.h>            //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <ESP32Time.h>            //Version 2.0.6 | Source: https://github.com/fbiego/ESP32Time
  #include <WebSocketsClient.h>     //Version 2.6.1 | Source: https://github.com/Links2004/arduinoWebSockets
  #include <ESP32Ping.h>            //Version 1.6   | Source: https://github.com/marian-craciunescu/ESP32Ping
  #include <ping.h>                 //Version 1.6   | Source: https://github.com/marian-craciunescu/ESP32Ping
  #include "esp_ota_ops.h"          //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <MQTTPubSubClient.h> 
  #include <SPI.h>
#if CORE_NFC_READER_MFRC630
  #include <mfrc630.h>
#endif
#if CORE_HAS_LOCAL_AUDIO_VISUAL
  #include <Adafruit_NeoPixel.h>
#endif
  #include "USB.h"
  #include <esp_system.h>
  #include <esp_mac.h>
  #include "esp_efuse.h"
  #include "esp_efuse_table.h"
#if CORE_HAS_ACCELEROMETER
  #include "SparkFun_LIS2DH12.h" //Click here to get the library: http://librarymanager/All#SparkFun_LIS2DH12
#endif
  #include <Wire.h>
  #include <mbedtls/md.h>          //Inherent to ESP32
  #include <stdio.h>
  #include <esp_crc.h>  // ESP32 built-in CRC header
  #include "esp_core_dump.h"

  #include "Device.h" //Struct definition in a header so it can be used in multiple places and in function calls

//Function Prototypes:
String readNfcCardId();
bool anyChannelMatcheschannelState(String targetState);

void sendDisplaychannelState(bool sendRarely, bool sendFrequently);
int readScreenRotation();
bool refreshAnnouncements();
bool refreshHours();
void calculateClosingEpochForToday();
void updateClosingMessageOfTheDay();

void handleOtaProgress(int offset, int totallength);
const char *getOtaErrorText(int code);
uint64_t millis64();
void connectNetwork();
#if CORE_HAS_ETHERNET
bool initializeEthernet();
String getEthernetMacAddress();
#endif
String getActiveNetworkInterface();
void publishMqttstatusMessage(String Topic, String Payload);
void mfrc630_SPI_transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
void mfrc630_SPI_select();
void mfrc630_SPI_unselect();
void applyConfigurationJson(const String& json);
void processConfigurationCommand(String command);
void printConfigurationHelp();
String getBaseMacAddress();
void sendStartupstatusMessage(String statusMessage);
String calculateSha256(String input);
void migrateLegacySettings();

//Objects:
  Preferences settings;
  JsonDocument ConfigJson;
  ESP32OTAPull ota;
  ESP32Time rtc;
  WebSocketsClient socket;
  MQTTPubSub::PubSubClient<1536> mqtt;
  OneWire ds(PIN_ONE_WIRE); 
#if CORE_NFC_READER_PN532
  Adafruit_PN532 nfc(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NFC_CS);
#endif
#if CORE_HAS_LOCAL_AUDIO_VISUAL
  Adafruit_NeoPixel CBI(1, PIN_LED, NEO_RGB + NEO_KHZ800);
#endif
#if CORE_HAS_ACCELEROMETER
  SPARKFUN_LIS2DH12 accel;
#endif
  //USBCDC Serial;

extern "C" bool verifyRollbackLater() {
  //This code is run to verify the OTA before actual setup.
  //Since we are handling OTA verification ourselves, we just return true.
  return true;
}

NetworkClientSecure networkclient;
#if !CORE_HAS_LOCAL_AUDIO_VISUAL
HardwareSerial frontend(1);
bool frontendButtonPressed = false;
bool frontendCardDetect1 = false;
bool frontendCardDetect2 = false;
#endif

String rootCertificate; //Stores the root certificate loaded from SPIFFS

//Variables - Inter-Task Communication
bool gamerMode = 1;  //Set to 0 to disable gamer mode, i.e. cycle RGB. Used during boot.
SystemState systemState;
bool accessEnabled = 0; //Used to tell the frontend to enable the access signal to the bus. 
String currentUserUid; //Stores the currentUserUid of the user currently using the machine.
String detectedUid = ""; //Stores the last-found currentUserUid
bool faultBeepRequested = 0; //We use the 3 beep normally for fault to indicate cannot welcome/auth due to no network, to differentiate from welcome/auth denied.

int MakerspaceNumber = 36;  // number from the makerspace's URL. We need to hard-code this for now.

//Variables - System channels.states
bool identifyRequested = 0; //Set to 1 to play an identification alarm/buzzer.
String inputMode = "INSERT"; //Stores how we ingest cards.
String defaultInputMode = "INSERT"; //Stores how we should ingest cards, when not in welcome mode.
bool pendingApproval = 0; //Set to 1 when we have a card present that hasn't been authed yet, this is used for LED animations. 
bool accessDenied = 0; //Set to 1 when a card is present but has been denied, for LED animations. 
bool cardPresent = 0; //Used to track if there is a card present in the machine.
bool lockWhenIdle = 0;
bool restartWhenUnused = 0;
bool welcomeMode = 0; //If 1, we are acting as a welcome reader and not a normal reader.
NetworkState networkState;
String tapUid; //Stores the currentUserUid between cycles for comparison when in tap mode.
bool userWelcomed = 0;

//Variables - Config
String serialNumber;
NetworkConfiguration networkConfiguration;
int makerspaceId;

MqttState mqttState;

Device sensorList[10];

//Variables - Inter-Task Communication
bool configOneWire = 0; //Flag to see if we should apply a config to the attached onewire device
bool sealBroken = 0;  //Set to 1 if there is an incorrect OneWire device on the bus. 
bool reSealBus = 0;
bool overTemp = 0;    //Set to 1 if there is a device overtemperature on the bus, so we can fault. 
byte liveAddresses[5][8];      //OneWire addresses of what is currently connected, for server reporting.
int liveAddressCount = 0;                //Number of currently connected devices on the bus, for server reporting.
byte deviceCount = 0; //Tracks the number of OneWire devices found on the bus.

//Variables - Inter-Task Communication (Inside Frontend)
byte redLed = 0; //Tracks the red channel light intensity
byte greenLed = 0; //Tracks the green channel light intensity
byte blueLed = 0; //Tracks the blue channel light intensity
unsigned int buzzerTone = 0; //Set to the tone that the buzzer should play.
bool resetLed = 0; //Set to 1 to take priority over the LED controller, to indicate the restart is imminent.
bool unlockedBeep = 0;
bool singleBeep = 0;

ChannelState channels;

//Interrupt Response Mode:
//The device can respond to an interrupt in a few different ways;
//1: "FAULT" - Immediately put the device into a fault state.
  //This is the normal operation of the interrupt pin
//2: "LOCK_TEMP" - Immediately set all channels to locked, but return to IDLE once we are no longer interrupted.
//3: "IDLE" - Put any unlocked channels into an idle state.
  //This lets the interrupt pin be used more like a "log out" button.
//4: "MESSAGE" - Simply notify the server a fauly occurred, but don't do anything.
  //Useful for situtations where interrupt is used to convey info, but not necessarily shut down access.
String interruptResponse = "FAULT";
bool isInterrupted = false; //Tracks if we are in a maintained interrupt mode, so we do not constantly re-assert states.
byte interruptCount = 0; //Counts how many times we read an interrupt as we cycle, for debouncing.

//Variables related to any connected screen;
bool updateScreen = false;
String faultReason = "";
String hmiMachineNames[4] = {"","","",""};
String hmiMakerspace;
String hmiDeviceName;
String hmiRole;
String stationName;

void setup() {
  // put your setup code here, to run once:

  //In case we crashed, immediately turn off buzzer and set LED red;
#if CORE_HAS_LOCAL_AUDIO_VISUAL
  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);
  CBI.begin();
  CBI.setBrightness(30);
  CBI.setPixelColor(0, 255, 0, 0);
  CBI.show();

  pinMode(PIN_BUTTON, INPUT); 
#endif

#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
  pinMode(PIN_ACCESS, OUTPUT);
  pinMode(PIN_IODIR_1, OUTPUT);
  digitalWrite(PIN_IODIR_1, HIGH);
  pinMode(PIN_IODIR_2, OUTPUT);
  digitalWrite(PIN_IODIR_2, HIGH);
  pinMode(PIN_IODIR_3, OUTPUT);
  digitalWrite(PIN_IODIR_3, HIGH);
  pinMode(PIN_IODIR_4, OUTPUT);
  digitalWrite(PIN_IODIR_4, HIGH);
  pinMode(PIN_GPIO_1, OUTPUT);
  pinMode(PIN_GPIO_2, OUTPUT);
  pinMode(PIN_GPIO_3, OUTPUT);
  pinMode(PIN_GPIO_4, OUTPUT);
#endif
  pinMode(PIN_INTERRUPT, INPUT_PULLUP);

#if USE_INTERNAL_USB_CDC
  //Set all USB-related settings, including VID/PID, product name, etc.
  USB.productName("Access Control Core");
  USB.manufacturerName("MakeACS");
  //Anything using internal CDC will use the proper internal serial number;

  uint8_t unique_id[16]; 
  
  // ESP_EFUSE_OPTIONAL_UNIQUE_ID is the constant defined in the IDF table
  esp_err_t usberr = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, unique_id, 128);

  if (usberr == ESP_OK) {
    for (int i = 0; i < 16; i++) {
      if (unique_id[i] < 0x10) serialNumber += "0"; // Lead with zero if byte < 16
      serialNumber += String(unique_id[i], HEX);
    }
    serialNumber.toUpperCase();
  }
  USB.serialNumber(serialNumber.c_str());
  USB.PID(0x82D0); //PID for Access Control Core
  USB.begin();
#endif
  Serial.begin(115200);
#if CORE_HAS_SCREEN
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
#endif
#if !CORE_HAS_LOCAL_AUDIO_VISUAL
  frontend.begin(115200, SERIAL_8N1, PIN_FRONTEND_RX, PIN_FRONTEND_TX);
  frontend.println("B 0"); //Set buzzer to 0
  frontend.println("L 0,0,255"); //Set LED to blue
#endif

  Serial.println(F("STARTUP"));
  Serial.flush();
  delay(500);

  sendStartupstatusMessage("Starting Tasks...");

  xTaskCreate(runAudioVisualController, "runAudioVisualController", 4096, NULL, 5, NULL);
  xTaskCreate(watchRestartButton, "watchRestartButton", 2048, NULL, 5, NULL);

  //Start i2C
#if CORE_HAS_ACCELEROMETER
  Wire.begin(PIN_SDA, PIN_SCL);
  accel.begin();
  accel.setScale(LIS2DH12_2g);
  accel.setDataRate(LIS2DH12_ODR_10Hz);
#endif

  //Start SPIFFS:
  if(!SPIFFS.begin(1)){
    Serial.println(F("SPIFFS Mount Failed!"));
    delay(1000);
    ESP.restart();
  }

  //Load the TLS cert from SPIFFS
  File file = SPIFFS.open("/cert.txt", FILE_READ);
  if(!file){
    Serial.println(F("No cert found in SPIFFS!"));
    rootCertificate = "Nothing here!";
  } else{
    rootCertificate = "";
    while(file.available()){
      rootCertificate += (char)file.read();
    }
  }

  //Load settings from memory
  settings.begin("settings", false);

  //Get our serial number;

  //Older devices use their onewire ID as a serial numer;
  if(settings.isKey("SerialNumber")){
    //Use old-style serial number
    serialNumber = settings.getString("SerialNumber");
    Serial.print(F("Loaded OneWire-based serial number: "));
    Serial.println(serialNumber);
  } else{
    //Otherwise, get our actual hardware ID number;

    // The ID is 128 bits = 16 bytes
    uint8_t unique_id[16]; 
    
    // ESP_EFUSE_OPTIONAL_UNIQUE_ID is the constant defined in the IDF table
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, unique_id, 128);

    if (err == ESP_OK) {
      Serial.print("Serial Number: ");
      for (int i = 0; i < 16; i++) {
        if (unique_id[i] < 0x10) serialNumber += "0"; // Lead with zero if byte < 16
        serialNumber += String(unique_id[i], HEX);
      }
      serialNumber.toUpperCase();
      Serial.print(serialNumber);
      Serial.println();
    } else {
      Serial.printf("Error reading eFuse: 0x%X\n", err);
      Serial.println("Note: This ID may not exist on original ESP32 (Non-S2/S3) models.");
    }
  }



  //Get our MAC address for printing, in V3.0.0 hardware this is our base MAC
  Serial.print(F("WiFi MAC Address: "));
  Serial.println(getBaseMacAddress());
#if CORE_HAS_ETHERNET
  Serial.print(F("Ethernet MAC Address: "));
  Serial.println(getEthernetMacAddress());
#endif

  migrateLegacySettings();
  xTaskCreate(runConfigurationController, "Configuration", 4096, NULL, 5, NULL);

  if(!settings.isKey("net.server")){
    //We don't have a valid config?
    sendStartupstatusMessage("ERROR: Missing Config!");
    printConfigurationHelp();
    while(1){
      delay(100);
    }
  }

  if(!settings.isKey("channels.count")){
    //channels.count is new in 2.1.4, set to 1 if no value
    settings.putString("channels.count", "1");
  }
  channels.count = settings.getString("channels.count").toInt();

  if(!settings.isKey("channels.tap0")){
    //Tap Duration is new in 2.1.4, set to 0 if no value.
    settings.putUInt("channels.tap0", 0);
    settings.putUInt("channels.tap1", 0);
    settings.putUInt("channels.tap2", 0);
    settings.putUInt("channels.tap3", 0);
  }
  for(int i = 0; i < 4; i++){
    String key = "channels.tap" + String(i);
    channels.tapDurations[i] = settings.getUInt(key.c_str());
  }

  if(!settings.isKey("access.input")){
    //inputMode is new in 2.1.4, set to "INSERT" if no value.
    settings.putString("access.input", "INSERT");
  }
  defaultInputMode = settings.getString("access.input");
  inputMode = defaultInputMode;

  if(!settings.isKey("access.intResp")){
    //Interrupt Response is new in 2.1.4, set to "FAULT" if no value.
    settings.putString("access.intResp", "FAULT");
  }
  interruptResponse = settings.getString("access.intResp");

  if(!settings.isKey("station.name")){
    //stationName is new in 2.1.4, set to "Generic ACS" if no value.
    settings.putString("station.name", "Generic MakeACS");
  }
  stationName = settings.getString("station.name");

  if(!settings.isKey("makerspace.num")){
    //MakerspaceNumber (SpaceNum) is new in 2.1.4, set to 36 (Atrium Makerspace) if no value.
    settings.putInt("makerspace.num", 36);
  }
  MakerspaceNumber = settings.getInt("makerspace.num");

  //Get the reset reason;
  if(!settings.isKey("system.reset")){
    //We don't know why we reset?

  } else{
    systemState.resetReason = settings.getString("system.reset");
    settings.remove("system.reset"); //So we know we read it.
  }

  networkConfiguration.serverAddress = settings.getString("net.server");
  networkConfiguration.wifiPassword = settings.getString("net.password");
  if(networkConfiguration.wifiPassword.equalsIgnoreCase("null")){
    //Use a real NULL password.
    networkConfiguration.wifiPassword = "";
  }
  networkConfiguration.wifiSsid = settings.getString("net.ssid");
  networkConfiguration.serverAddress = settings.getString("net.server");
  networkConfiguration.mqttKey = settings.getString("net.mqttKey");
  int TimezoneHr;
  if(settings.isKey("system.timezone")){
    TimezoneHr = settings.getString("system.timezone").toInt();
  } else{
    TimezoneHr = -4; //Hardcoded EST
  }
  rtc.offset = TimezoneHr * 3600;
  makerspaceId = settings.getString("makerspace.id").toInt();

  Serial.println(F("Settings loaded."));
  Serial.flush();

  sendStartupstatusMessage("Settings Loaded.");

  Serial.println(F("Started Tasks."));
  Serial.flush();

  //Start SPI here, in case we want to use Ethernet in the future. 
  //SCK, MISO, MOSI, SS
  pinMode(PIN_NFC_CS, OUTPUT);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

#if CORE_HAS_ETHERNET
  const bool ethernetReady = initializeEthernet();
#else
  const bool ethernetReady = false;
#endif

  Serial.println(F("Started SPI."));
  Serial.flush();

  if (!ethernetReady) {
    sendStartupstatusMessage("Starting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(networkConfiguration.wifiSsid, networkConfiguration.wifiPassword);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    unsigned long WiFiStart = millis64();

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      Serial.println(F("Waiting for first WiFi connect"));
      while (WiFi.status() != WL_CONNECTED && millis64() - WiFiStart < 15000) {
        Serial.print(".");
        delay(500);
      }
    }
  }

  bool otaVerified = false;

  if (ethernetReady || WiFi.status() == WL_CONNECTED) {
    if (ethernetReady) {
      networkState.transport = NetworkState::Transport::Ethernet;
      Serial.println(F("Ethernet connected."));
      sendStartupstatusMessage("Ethernet Started.");
    } else {
      networkState.transport = NetworkState::Transport::WiFi;
      Serial.println(F("WiFi connected."));
      sendStartupstatusMessage("WiFi Started.");
    }

#if CORE_HAS_SCREEN
    // Also get rid of "No NET" on screen
    JsonDocument NoNetStart;
    NoNetStart["noNetwork"] = false;
    String NoNetToSend;
    serializeJson(NoNetStart, NoNetToSend);
    Serial0.println(NoNetToSend);
#endif

    delay(500);

    // --- OTA LOGIC STARTS HERE ---
    // We only verify and check for updates if we are actually online.
    sendStartupstatusMessage("Checking for OTA...");
    Serial.println(F("Checking for OTA..."));

    // 1. Configure all OTA settings first
    ota.EnableSerialDebug();
    //We use the same cert on our server as Github does.
    ota.SetCACert(rootCertificate.c_str());
    ota.SetCallback(handleOtaProgress);
    String targetFilename = "firmware_" + String(PIOENV_NAME) + ".bin";
    Serial.print(F("OTA Target Filename: "));
    Serial.println(targetFilename);
    ota.SetTargetFilename(targetFilename.c_str());

    // 2. Verify the current firmware can reach the JSON (or rollback)
    const char* jsonUrl = "https://raw.githubusercontent.com/MakeACS/HW-NFC-Core/main/Firmware/OTADirectory.json";
    otaVerified = ota.VerifyOrRevert(jsonUrl, FIRMWARE_VERSION);

    // 3. Check for a new update before we continue;
      int otaresp = ota.CheckForOTAUpdate(jsonUrl, FIRMWARE_VERSION);
      Serial.print(F("OTA Response: "));
      Serial.println(getOtaErrorText(otaresp));

      if(otaresp == ESP32OTAPull::SKIPPED_BAD_VERSION){
        //Important one; this is a failed OTA that was reverted. We should report it.
        String revertMessage = "OTA Reverted! Version: ";
        //Close the standard preferences.
        settings.end();
        delay(10);
        settings.begin("ota_prefs", true);
        String badVer = settings.getString("bad_ver", "");
        settings.end();
        //Re-open the main settings folder;
        settings.begin("settings", false);
        revertMessage += badVer;
        revertMessage += " failed boot tests. Reverted to ";
        revertMessage += FIRMWARE_VERSION;
        mqttState.messageToSend = true;
        mqttState.statusMessage = revertMessage;
        Serial.println(revertMessage);
      }

    // --- OTA LOGIC ENDS HERE ---

  } else {
    // Device is offline. We skip OTA checks entirely to avoid false rollbacks.
    Serial.println(F("No network interface connected. Booting offline."));
    sendStartupstatusMessage("Network failed to start?");
  }

  // If we made it past the OTA (or skipped it because offline),
  // then we are ready for normal operation.

  //Before we continue, let's figure out why we restarted.
  Serial.println(F("Checking reset reason..."));
  if(otaVerified){
    //We should report to the server that we updated.
    mqttState.messageToSend = true;
    mqttState.statusMessage = "OTA Update Successful: " + String(PIOENV_NAME) + " v" + FIRMWARE_VERSION;
    Serial.println(F("Reset Reason: OTA Update Successful."));
  } else{
    //We restart for something other than an OTA
    JsonDocument ResetDoc;
    esp_reset_reason_t reason = esp_reset_reason();
    if(reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT){
      //Watchdog reset
      mqttState.logToSend = true;
      mqttState.logType = "reset-log";
      ResetDoc["reset-reason"] = reason;
      ResetDoc["report"] = "Watchdog Reset. Source Unknown?";
      Serial.println(F("Reset Reason: Watchdog Reset. Source Unknown?"));
    }
    else if(reason == ESP_RST_BROWNOUT || reason == ESP_RST_PWR_GLITCH){
      //Power-related reset
      mqttState.logToSend = true;
      mqttState.logType = "reset-log";
      ResetDoc["reset-reason"] = reason;
      ResetDoc["report"] = "Power Issue Reset.";
      mqttState.messageToSend = true;
      mqttState.statusMessage = "Device restarted due to power anomaly. Check wiring and ensure properly-sized power supply is used.";
      Serial.println(F("Reset Reason: Power Issue Reset."));
    }
    else if(reason == ESP_RST_CPU_LOCKUP || reason == ESP_RST_PANIC){
      //CPU lockup or panic reset
      mqttState.logToSend = true;
      mqttState.logType = "reset-log";
      ResetDoc["reset-reason"] = reason;
      ResetDoc["version"] = FIRMWARE_VERSION;
      ResetDoc["hardware"] = PIOENV_NAME;
      Serial.println(F("Reset Reason: CPU Lockup or Panic Reset. Attempting to get core dump summary..."));
      // Get the core dump summary
      esp_core_dump_summary_t summary;
      esp_err_t crasherr = esp_core_dump_get_summary(&summary);
      if (crasherr != ESP_OK) {
        Serial.println(F("Failed to get core dump summary?"));
        ResetDoc["report"] = "CPU Lockup or Panic Reset, with no core dump summary available.";
        Serial.println(F("Failed to get core dump summary?"));
      } else{
        ResetDoc["report"] = "CPU Lockup or Panic Reset, core dump summary attached.";
        Serial.println(F("Core dump summary retrieved successfully. Sending to server..."));
        JsonObject dumpDoc = ResetDoc["core-dump-summary"].to<JsonObject>();
        //Task ID
        dumpDoc["task"] = summary.exc_task;
        //Firmware SHA (Convert the 32-byte array to a 64-character hex string)
        char sha_str[65] = {0};
        for (int i = 0; i < 32; i++) {
            sprintf(&sha_str[i * 2], "%02x", summary.app_elf_sha256[i]);
        }
        dumpDoc["firmware_sha"] = sha_str;

        //Exception Registers (Formatted as Hex strings so they are easy to read)
        char hex_buf[20];
        sprintf(hex_buf, "0x%08lx", (unsigned long)summary.exc_pc);
        dumpDoc["pc"] = hex_buf;
        sprintf(hex_buf, "0x%04lx", (unsigned long)summary.ex_info.exc_cause);
        dumpDoc["exc_cause"] = hex_buf;
        sprintf(hex_buf, "0x%08lx", (unsigned long)summary.ex_info.exc_vaddr);
        dumpDoc["exc_vaddr"] = hex_buf;

        //backtrace info
        JsonObject btDoc = dumpDoc["backtrace"].to<JsonObject>();
        esp_core_dump_bt_info_t bt_info = summary.exc_bt_info;
        btDoc["depth"] = bt_info.depth;
        btDoc["corrupted"] = bt_info.corrupted;
        JsonArray frames = btDoc["frames"].to<JsonArray>();
        for (uint32_t i = 0; i < bt_info.depth; i++) {
            char bt_hex[20];
            sprintf(bt_hex, "0x%08lx", (unsigned long)bt_info.bt[i]);
            frames.add(bt_hex);
        }
        //After reading the core dump summary, we should clear it so we don't keep sending it.
        esp_core_dump_image_erase();
      }
    }
    else{
      //All other reset reasons are considered nominal.
      Serial.println(F("Nominal reset reason, no need to send a report."));
    }
    if(mqttState.logToSend){
      //We found a notable reset reason to send, let's package it to send out.
      String resetPayload;
      serializeJson(ResetDoc, resetPayload);
      mqttState.logMessage = resetPayload;
    }
  }

  sendStartupstatusMessage("Connecting MQTT...");

  mqtt.begin(socket); //Enable MQTT on the websocket

  connectNetwork();
  
  //If we cared about why we restarted, this'd be the place to handle it.

  //Start the NFC reader, make sure it is working as expected. 
 #if CORE_NFC_READER_MFRC630
  mfrc630_AN1102_recommended_registers(MFRC630_PROTO_ISO14443A_106_MILLER_MANCHESTER);
  mfrc630_write_reg(0x28, 0x8E);
  mfrc630_write_reg(0x29, 0x15);
  mfrc630_write_reg(0x2A, 0x11);
  mfrc630_write_reg(0x2B, 0x06);
#elif CORE_NFC_READER_PN532
  pinMode(PIN_NFC_POWER, OUTPUT);
  pinMode(PIN_NFC_RST, OUTPUT);
  digitalWrite(PIN_NFC_POWER, HIGH);
  digitalWrite(PIN_NFC_RST, HIGH);
  nfc.begin();
  nfc.SAMConfig();
#endif

  //We should initialize the OneWire bus here, check for the right devices, etc.
  //TODO will enable onewire in future version, needs more testing to be reliable. 

  //Initialize a precise timer for the Hobbs Timer
  Serial.println(F("Starting Critical Timer for Hobbs Time..."));
  const esp_timer_create_args_t timer_args = {
    .callback = &updateHobbsCounter,  // The function to run
    .arg = NULL,                   // Arguments passed to the function (optional)
    .name = "one_second_timer"     // Name for debugging
  };
  esp_timer_handle_t periodic_timer;
  esp_err_t err = esp_timer_create(&timer_args, &periodic_timer);
  if (err == ESP_OK) {
    //Start the timer to repeat every 1,000,000 microseconds (1 second)
    esp_timer_start_periodic(periodic_timer, 1000000);
    Serial.println("Timer started successfully!");
  } else {
    Serial.printf("Timer creation failed with error: %d\n", err);
  }

  //Time to loop!
  xTaskCreate(runFrontendController, "Frontend", 2048, NULL, 5, NULL);
  xTaskCreate(runMachineStateLoop, "runMachineStateLoop", 4096, NULL, 5, NULL);
  gamerMode = 0; //Disable the startup lighting

#if CORE_HAS_SCREEN
  xTaskCreate(runScreenController, "ScreenController", 4096, NULL, 5, NULL);
#endif

}

void loop() {
  // put your main code here, to run repeatedly:

  delay(10);

  //Step 0: Call the MQTT updater;
  mqtt.update();

  //Step 4: Communicate with the server

  //Only do all this if we have a connection
  if(mqtt.isConnected() && !networkState.unavailable){

    if(networkState.unavailable){
      networkState.unavailable = false;
      updateScreen = true;
    }

    JsonDocument outgoing; //Json to construct the outgoing message in

     //Step 4.1: See if we have any outgoing messages, and send them.

    if(mqttState.messageToSend){
      //Send a message to the history
      mqttState.messageToSend = false;
      outgoing["auditLog"] = true; //Print in the history
      outgoing["message"] = mqttState.statusMessage;
      outgoing["category"] = "message";
      String MessagePayload;
      serializeJson(outgoing, MessagePayload);
      outgoing.clear(); //Clear so other sends can use it
      String MessageTopic = mqttState.baseTopic + "/log";
      publishMqttstatusMessage(MessageTopic, MessagePayload);
    }
    if(mqttState.logToSend){
      //Send a log to the audit logs (not the user-visible history)
      mqttState.logToSend = false;
      outgoing["auditLog"] = false; //Don't print in the history
      outgoing["message"] = mqttState.logMessage;
      outgoing["type"] = mqttState.logType;
      String LogPayload;
      serializeJson(outgoing, LogPayload);
      outgoing.clear();
      String LogTopic = mqttState.baseTopic + "/log";
      publishMqttstatusMessage(LogTopic, LogPayload);
      mqttState.logType = "message"; //Default value unless we say otherwise.
    }
    if(mqttState.sendAuth){
      //Send an auth request to the server
      mqttState.sendAuth = false;
      outgoing["state"] = "UNLOCKED";
      outgoing["cardTagID"] = currentUserUid;
      String AuthPayload;
      serializeJson(outgoing, AuthPayload);
      outgoing.clear();
      String AuthTopic = mqttState.baseTopic + "/authTo/request";
      publishMqttstatusMessage(AuthTopic, AuthPayload);
    }
    if(mqttState.stateChange){
      //Send report of a changed state
      mqttState.stateChange = false;
      if(!welcomeMode){
        //We don't report state change when we are in welcoming.
        JsonArray stateChannels = outgoing["channels"].to<JsonArray>();
        for( int i = 0; i < channels.count; i++){
          if(channels.states[i] != channels.reportedStates[i]){
            JsonObject stateObject = stateChannels.createNestedObject();
            stateObject["channelID"] = i;
            stateObject["fromState"] = channels.reportedStates[i];
            stateObject["toState"] = channels.states[i];
            //The server doesn't recognize the "LOCK_TEMP" state change reason
            //So we replace if with "LOCAL":
            if(channels.changeReasons[i] == "LOCK_TEMP"){
              stateObject["reason"] = "LOCAL";
            } else{
              stateObject["reason"] = channels.changeReasons[i];
            }
            //Update the preserved last state;
            channels.reportedStates[i] = channels.states[i];
          }
        }
        outgoing["currentCardTag"] = currentUserUid;
        String StateChangePayload;
        serializeJson(outgoing, StateChangePayload);
        outgoing.clear();
        String StateChangeTopic = mqttState.baseTopic + "/stateChange";
        publishMqttstatusMessage(StateChangeTopic, StateChangePayload);
        //At the end, set change reason to nothing:
      }
    }
    if(mqttState.reportConfig){
      //Report the current configuration
      mqttState.reportConfig = false;
      JsonArray configChannels = outgoing["channels"].to<JsonArray>();
      for(int i = 0; i < channels.count; i++){
        JsonObject configObject = configChannels.createNestedObject();
        configObject["channelID"] = i;
        configObject["tempDuration"] = channels.tapDurations[i];
      }
      //Temp disable network inteface reporting, as it is not yet implemented on the server side.
      //outgoing["networkInterface"] = getActiveNetworkInterface();
      outgoing["inputMode"] = inputMode;
      JsonObject configDeployment = outgoing["deployment"].to<JsonObject>();
      configDeployment["SN"] = serialNumber;
      JsonArray configComponents = configDeployment["components"].to<JsonArray>();
      //Iterate through and add every component on the bus to the components array;
      for(int i = 0; i < liveAddressCount; i++){
        JsonObject deviceObj = configComponents.createNestedObject();
        // Convert the 8-byte address to a Hex String for JSON
        char addrStr[17]; 
        snprintf(addrStr, sizeof(addrStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
        liveAddresses[i][0], liveAddresses[i][1], liveAddresses[i][2], liveAddresses[i][3],
        liveAddresses[i][4], liveAddresses[i][5], liveAddresses[i][6], liveAddresses[i][7]);
        deviceObj["SN"] = String(addrStr);
        for(int j = 0; j < deviceCount; j++) {
          if(memcmp(liveAddresses[i], sensorList[j].address, 8) == 0) {
            // Here we grab the deviceMode and other data from the struct
            deviceObj["type"] = sensorList[j].deviceMode; 
            deviceObj["identifier"] = sensorList[j].deviceID; //serverAddress doesn't expect this yet, but we should send it
            break;
          }
        }
      }
      JsonObject flags = outgoing["flags"].to<JsonObject>();
      flags["lockWhenIdle"] = lockWhenIdle;
      flags["restartWhenUnused"] = restartWhenUnused;
      flags["welcoming"] = welcomeMode;
      String FWVer = "CoreDuino " + String(FIRMWARE_VERSION);
      outgoing["firmware"] = FWVer;
      //TODO verify this is the right key
      /*
      #ifdef NICE_HARDWARE_NAME
      outgoing["hardware"] = NICE_HARDWARE_NAME;
      #else
      outgoing["hardware"] = HARDWARE_VERSION;
      #endif
      */
      String ConfigPayload;
      serializeJson(outgoing, ConfigPayload);
      outgoing.clear();
      String ConfigTopic = mqttState.baseTopic + "/config/report";
      publishMqttstatusMessage(ConfigTopic, ConfigPayload);
    }
    if(mqttState.requestInfo){
      //Request information from the server
      mqttState.requestInfo = false;
      JsonArray infoFields = outgoing["fields"].to<JsonArray>();
      infoFields.add("TIME");
      //Check if any of the states or HobbsTimers are unknown;
      bool AskForStates = false;
      bool AskForHobbs = false;
      for(int i = 0; i < channels.count; i++){
        if(channels.states[i] == "UNKNOWN"){
          AskForStates = true;
        }
        if(channels.hobbsSeconds[i] == 0){
          AskForHobbs = true;
        }
      }
      if(AskForStates){
        //We don't know what state we should be in, so request it. 
        infoFields.add("STATE");
      }
      if(AskForHobbs){
        //We do not know what the Hobbs timer should be at, let's request that.
        infoFields.add("HOBBS_TIME");
      }
      infoFields.add("FLAGS"); //Check our flags, mostly for welcoming
#ifdef CORE_HAS_SCREEN
      infoFields.add("HMI"); //Request human-readable info for any attached interface.
#endif
      String InfoPayload;
      serializeJson(outgoing, InfoPayload);
      outgoing.clear();
      String InfoTopic = mqttState.baseTopic + "/info/request";
      publishMqttstatusMessage(InfoTopic, InfoPayload);
    }
    if(mqttState.sendStatus && !anyChannelMatcheschannelState("UNKNOWN")){
      //Send our current status to the server, we do not send it if we do not know our state. 
      mqttState.sendStatus = false;
      JsonArray statusChannels = outgoing["channels"].to<JsonArray>();
      if(!welcomeMode){
        //We don't send this in welcoming mode
        for(int i = 0; i < channels.count; i++){
          JsonObject statusObject = statusChannels.createNestedObject();
          statusObject["channelID"] = i;
          statusObject["state"] = channels.states[i];
          statusObject["hobbsTime"] = channels.hobbsSeconds[i];
        }
      }
      outgoing["currentCardTag"] = currentUserUid;
      String StatusPayload;
      serializeJson(outgoing, StatusPayload);
      outgoing.clear();
      String StatusTopic = mqttState.baseTopic + "/status";
      publishMqttstatusMessage(StatusTopic, StatusPayload);
    }
    if(mqttState.sendWelcome){
      //Send a welcome message to the server
      mqttState.sendWelcome = false;
      outgoing["cardTagID"] = currentUserUid;
      String WelcomePayload;
      serializeJson(outgoing, WelcomePayload);
      outgoing.clear();
      String WelcomeTopic = mqttState.baseTopic + "/welcome/request";
      publishMqttstatusMessage(WelcomeTopic, WelcomePayload);
    }

    JsonDocument incoming; //Json doucment to parse the incoming
    
    //Step 4.3: Process any incoming messages

    if(mqttState.newAuth){
      //Process a response to an auth request.
      mqttState.newAuth = false;
      deserializeJson(incoming, mqttState.authResponse);
      String AuthID = incoming["cardTagID"].as<String>();
      pendingApproval = false;
      bool SendUnlockedBeep = false;
      bool SendAccessDenied = false;
      for(JsonVariant v : incoming["channels"].as<JsonArray>()){
        int ch = v["channelID"] | 0;
        if(ch >= 0 && ch < channels.count){
          bool IsAuthed = v["approved"].as<bool>();
          channels.authorizationReasons[ch] = v["reason"].as<String>();
          
          if(channels.states[ch] == "IDLE" || (channels.states[ch] == "UNLOCKED" && inputMode == "TEMP_PRESENT")){ //Unlock only if idle, or re-up unlocked channels if in tap-present mode.
            if(IsAuthed){
              Serial.println(F("accessEnabled Granted!"));
              if(AuthID == currentUserUid){
                Serial.println(F("UIDs match. Unlocking."));
                channels.states[ch] = "UNLOCKED";
                channels.changeReasons[ch] = "AUTHED"; 
                SendUnlockedBeep = true;
                if(inputMode == "TEMP_PRESENT"){
                  //Add all the times now;
                  channels.tapExpirationTimes[ch] = channels.tapDurations[ch] * 1000 + millis64();
                }
              }
            } else{
              Serial.println(F("accessEnabled Denied!"));
              if(cardPresent){
                SendAccessDenied = 1;
              }
            }
          } else{
            Serial.println(F("Ignoring auth due to improper state."));
          }
        }
      }
      if(SendUnlockedBeep){
        //We do it this way so we don't trigger the beep 4 times
        unlockedBeep = true;
      }
      if(SendAccessDenied){
        accessDenied = true;
      }
      updateScreen = true;
    }
    if(mqttState.newInfo){
      //Process a response to an info request.
      mqttState.newInfo = false;
      deserializeJson(incoming, mqttState.infoResponse);
      //Set the state;
      if (incoming["state"].is<JsonArray>()) {
        for (JsonObject item : incoming["state"].as<JsonArray>()) {
          int id = item["id"] | -1; // Default to -1 if missing
          
          // Bounds check to avoid crashing the MCU with array out-of-bounds
          if (id >= 0 && id < channels.count) {
            channels.states[id] = item["state"].as<String>();
            channels.changeReasons[id] = "COMMANDED";
            if(channels.states[id] == "FAULT"){
              //We don't go back to a fault state;
              channels.states[id] = "LOCKED_OUT";
            }
            if (channels.states[id] == "UNLOCKED" || channels.states[id] == "ALWAYS_ON") {
              //We don't go back to an unlocked state;
              channels.states[id] = "IDLE";
            }
          }
        }
        singleBeep = 1;
      }

      // Process the "hobbsTime" array
      if (incoming["hobbsTime"].is<JsonArray>()) {
        for (JsonObject item : incoming["hobbsTime"].as<JsonArray>()) {
          // Notice this uses "channelID" instead of "id"
          int ch = item["channelID"] | -1; 
          
          if (ch >= 0 && ch < channels.count) {
            channels.hobbsSeconds[ch] = item["hobbsTime"].as<unsigned long>(); 
          }
        }
      }
      //Process the HMI info:
      if(incoming.containsKey("hmi")){
        hmiRole = incoming["hmi"]["role"].as<String>();
        hmiDeviceName = incoming["hmi"]["deviceName"].as<String>();
        hmiMakerspace = incoming["hmi"]["makerspace"].as<String>();
        JsonArray channels = incoming["hmi"]["channels"];
        for (JsonObject channel : channels){
          int channelID = channel["channelID"];
          hmiMachineNames[channelID] = channel["pairedEntity"].as<String>();
        }
      }
      //Set the time;
      if(incoming.containsKey("time")){
        unsigned long long millisecondTime = incoming["time"];
        rtc.setTime(millisecondTime/1000);
        Serial.print(F("Time set to: "));
        Serial.println(rtc.getDateTime(true));
      }
      //Set flags:
      if(incoming.containsKey("flags")){
        JsonObject flagObj = incoming["flags"].as<JsonObject>();
        if(flagObj.containsKey("lockWhenIdle")){
          lockWhenIdle = flagObj["lockWhenIdle"].as<bool>();
          Serial.print(F("serverAddress set lockWhenIdle to: "));
          Serial.println(lockWhenIdle);
        }
        if(flagObj.containsKey("restartWhenUnused")){
          restartWhenUnused = flagObj["restartWhenUnused"].as<bool>();
          Serial.print(F("serverAddress set restartWhenUnused to: "));
          Serial.println(restartWhenUnused);
        }
        if(flagObj.containsKey("welcoming")){
          if(welcomeMode != flagObj["welcoming"].as<bool>()){
            welcomeMode = flagObj["welcoming"].as<bool>();
            if(welcomeMode){
            Serial.println(F("serverAddress flag set to enter welcoming mode."));
            } else{
              Serial.println(F("serverAddress flag unset for welcoming mode. Entering state 'UNKNOWN'"));
              welcomeMode = false;
              for(int i = 0; i < channels.count; i++){
                channels.states[i] = "UNKNOWN";
                channels.changeReasons[i] = "SERVER_COMMANDED";
              }
              //We should ask what state we should be in
              mqttState.requestInfo = true;
            }
          }
        }
      }
      if(incoming.containsKey("hobbsTime")){
        for(int i = 0; i < channels.count; i++){
          channels.hobbsSeconds[i] = incoming["hobbsTime"][i]["hobbsTime"];
          Serial.print(F("Hobbs timer for channel "));
          Serial.print(i);
          Serial.print(F(" set to: "));
          Serial.print(channels.hobbsSeconds[i]);
          Serial.println(F(" seconds."));
        }
      }
      mqttState.reportConfig = true; //Once we get some info, we should send our configuration.
      mqttState.sendStatus = true; //Once we get some info, we should send our status.
      updateScreen = true;
    }
    if(mqttState.newCommand){
      //Process an incoming command.
      mqttState.newCommand = false;
      deserializeJson(incoming, mqttState.commandResponse);
      //channels.states change command
      if(incoming["toState"].is<JsonArray>()){
        JsonArray toStateArray = incoming["toState"].as<JsonArray>();
        for (JsonVariant v : toStateArray) {
          int ch = v["id"] | -1;
          if (ch >= 0 && ch < channels.count) {
            channels.states[ch] = v["state"] | "UNKNOWN";
            if(channels.states[ch] == "UNLOCKED" && !cardPresent){
              channels.states[ch] = "IDLE";
            }
            channels.changeReasons[ch] = "COMMANDED";
          }
        }
        singleBeep = 1;
      }
      //Set flags
      if(incoming.containsKey("flags")){
        JsonObject flagObj = incoming["flags"].as<JsonObject>();
        if(flagObj.containsKey("lockWhenIdle")){
          lockWhenIdle = flagObj["lockWhenIdle"].as<bool>();
          Serial.print(F("serverAddress set lockWhenIdle to: "));
          Serial.println(lockWhenIdle);
        }
        if(flagObj.containsKey("restartWhenUnused")){
          restartWhenUnused = flagObj["restartWhenUnused"].as<bool>();
          Serial.print(F("serverAddress set restartWhenUnused to: "));
          Serial.println(restartWhenUnused);
        }
        if(flagObj.containsKey("welcoming")){
          if(welcomeMode != flagObj["welcoming"].as<bool>()){
            welcomeMode = flagObj["welcoming"].as<bool>();
            if(welcomeMode){
            Serial.println(F("serverAddress flag set to enter welcoming mode."));
            } else{
              Serial.println(F("serverAddress flag unset for welcoming mode. Entering state 'UNKNOWN'"));
              for(int i = 0; i < channels.count; i++){
                channels.states[i] = "UNKNOWN";
                channels.changeReasons[i] = "SERVER_COMMANDED";
              }
              //We should ask what state we should be in
              mqttState.requestInfo = true;
            }
          }
        }
      }
      //Set HobbsTime
      if(incoming.containsKey("hobbsTime")){
        JsonArray hobbsTimeArray = incoming["hobbsTime"].as<JsonArray>();
        for (JsonVariant v : hobbsTimeArray) {
          int ch = v["hobbsTime"] | 0;
          if (ch >= 0 && ch < channels.count) {
            channels.hobbsSeconds[ch] = v["channelID"] | 0;
            Serial.print(F("Hobbs timer for channel "));
            Serial.print(ch);
            Serial.print(F(" set to: "));
            Serial.print(channels.hobbsSeconds[ch]);
            Serial.println(F(" seconds."));
          }
        }
      }
      //Action to do something
      if(incoming.containsKey("action")){
        if(incoming["action"] == "RESTART"){
          Serial.println(F("serverAddress commanded restart!"));
          Serial.flush();
          systemState.resetReason = "serverAddress Ordered";
          systemState.requestReset = true;
        }
        if((incoming["action"] == "SEAL") && sealBroken){
          Serial.println(F("serverAddress commanded bus integrity re-seal."));
          reSealBus = true;
        }
        if(incoming["action"] == "IDENTIFY"){
          Serial.println(F("serverAddress commanded identify."));
          identifyRequested = !identifyRequested;
          if(!identifyRequested){
            //Play a single beep to end the identify command.
            singleBeep = true;
          }
        }
        if(incoming["action"] == "SCHEDULED_RESTART"){
          Serial.println(F("serverAddress indicated it is time for a scheduled restart."));
          systemState.scheduledRestart = true;
        }

      }
      updateScreen = true;
    }
    if(mqttState.newWelcome){
      //Response to welcoming a user
      mqttState.newWelcome = false;
      deserializeJson(incoming, mqttState.welcomeResponse);
      bool IsWelcomed = incoming["welcomed"];
      String WelcomeID = incoming["cardTagID"];
      String WelcomeReason = incoming["reason"];
      if(IsWelcomed){
        //User was welcomed into the space properly.
        Serial.println(F("User welcomed!"));
        if(currentUserUid == WelcomeID){
          //The user's card is still here, so beep and light up.
          userWelcomed = 1;
        } else{
          Serial.println(F("But their card isn't here anymore, so we will skip the lights/sounds."));
        }
      } else{
        //User was denied entry into the space.
        Serial.print(F("User denied! Reason: "));
        Serial.println(WelcomeReason);
        accessDenied = 1; //Act like we denied the user access
      }
      updateScreen = true;
    }
    
    //Step 4.4: Send a ping if requested
    if(mqttState.sendPing){
      String PingTopic = mqttState.baseTopic + "/ping";
      publishMqttstatusMessage(PingTopic, "Ping!");
      //Serial.println(F("Ping sent."));
      mqttState.sendPing = false;
      mqttState.nextPingTime = millis64() + 1000;
    }
    
  } else{
    Serial.println(F("No network?"));
    networkState.unavailable = true;
    updateScreen = true;
    connectNetwork();
  }

}

void handleOtaProgress(int offset, int totallength) {
  //Used to display percentage of OTA installation

  static int prev_percent = -1;
  int percent = 100 * offset / totallength;
  if (percent != prev_percent) {
    Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
    prev_percent = percent;
    //We should also send it to any attached screen;
    JsonDocument CoreOTA;
    CoreOTA["coreOta"] = percent;
    String CoreOTAString;
    serializeJson(CoreOTA, CoreOTAString);
  #if CORE_HAS_SCREEN
    Serial0.println(CoreOTAString);
  #endif
  }
}

const char *getOtaErrorText(int code) {
  //Deciphers OTA code response
  switch (code) {
    case ESP32OTAPull::UPDATE_AVAILABLE:
      return "An update is available but wasn't installed";
    case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
      return "No profile matches";
    case ESP32OTAPull::NO_UPDATE_AVAILABLE:
      return "Profile matched, but update not applicable";
    case ESP32OTAPull::UPDATE_OK:
      return "An update was done, but no reboot";
    case ESP32OTAPull::HTTP_FAILED:
      return "HTTP GET failure";
    case ESP32OTAPull::WRITE_ERROR:
      return "Write error";
    case ESP32OTAPull::JSON_PROBLEM:
      return "Invalid JSON";
    case ESP32OTAPull::OTA_UPDATE_FAIL:
      return "Update fail (no OTA partition?)";
    default:
      if (code > 0)
        return "Unexpected HTTP response code";
      break;
  }
  return "Unknown error";
}

uint64_t millis64(){
  //This simple function replaces the 32 bit default millis. Means that overflow now occurs in 290,000 years instead of 50 days
  //Timer runs in microseocnds, so divide by 1000 to get millis.
  return esp_timer_get_time() / 1000;
}

void connectNetwork(){
  byte TLSRetryCount = 0; //Tracks how many failed TLS attempts we had in a row.
  retryNetwork:
#if CORE_HAS_ETHERNET
  if (ETH.hasIP()) {
    networkState.transport = NetworkState::Transport::Ethernet;
    Serial.println(F("Using Ethernet connection."));
  } else
#endif
  {
    networkState.transport = NetworkState::Transport::WiFi;
    WiFi.mode(WIFI_STA);
    WiFi.begin(networkConfiguration.wifiSsid, networkConfiguration.wifiPassword);
  if(WiFi.status() != WL_CONNECTED){
    WiFi.reconnect(); //Force a manual connect attempt
    Serial.println(F("No WiFi? Waiting for reconnect"));
    unsigned long long WiFiTime = millis64() + 15000;
    while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(500);
      if(WiFiTime <= millis64()){
        Serial.println(F("Failed to connect to WiFi! Retrying..."));
        goto retryNetwork;
      }
    }
    Serial.println(F(" Connected!"));
  } else{
    Serial.println(F("Already had WiFi connection, skipping to websocket connection."));
  }
  }
  
  //Start our websocket connection
  socket.disconnect();
  const char* global_ca_pointer = rootCertificate.c_str();
  socket.beginSslWithCA(networkConfiguration.serverAddress.c_str(), 443, "/mqtt", global_ca_pointer, "mqtt");
  //Give the socket some time to stabilize:
  unsigned long long wsTimeout = millis64() + 5000;
  while(!socket.isConnected() && millis64() <= wsTimeout){
    socket.loop();
    delay(2);
  }
  //Did the socket work?
  if(!socket.isConnected()){
    Serial.println(F("Websocket connection failed..."));
    socket.disconnect();
    //Is the server alive?
    if(!Ping.ping(networkConfiguration.serverAddress.c_str())){
      //serverAddress is not responding?
      Serial.println(F("Cannot ping the server. No network?"));
      networkState.unavailable = true;
      return;
    } else{
      Serial.println(F("serverAddress is online. Bad TLS cert?"));
      Serial.println(F("Trying TLS certs again to make sure..."));
      if(TLSRetryCount <=5){
        Serial.print(F("That was attempt: "));
        Serial.print(TLSRetryCount);
        Serial.println(F("/5 attempts before we get new certs."));
        delay(1000);
        TLSRetryCount++;
        goto retryNetwork;
      }
      Serial.println(F("Getting new TLS certs from server."));
      networkclient.setInsecure();
      networkclient.connect(networkConfiguration.serverAddress.c_str(), 443);
      
      networkclient.print("GET /api/rootCA HTTP/1.1\r\n");
      networkclient.print("Host: ");
      networkclient.print(networkConfiguration.serverAddress.c_str());
      networkclient.print("\r\n");

      networkclient.print("shlug-sn: ");
      networkclient.print(serialNumber.c_str());
      networkclient.print("\r\n");
      
      networkclient.print("Connection: close\r\n");
      
      // End of headers boundary
      networkclient.print("\r\n");

      while (networkclient.connected()) {
        String line = networkclient.readStringUntil('\n');
        if (line == "\r") {
          Serial.println("Headers received, body:");
          break;
        }
      }
      unsigned long timeout = millis();
      while (networkclient.available() == 0) {
        if (millis() - timeout > 5000) { // 5 second timeout
          Serial.println("!!! Client Timeout awaiting body! !!!");
          networkclient.stop();
          return;
        }
        delay(10); 
        networkState.unavailable = true;
        goto retryNetwork;
      }

      // The body is a JSON, let's capture it in a string.
      String TLSPayload;
      while(networkclient.available()){
        char c = networkclient.read();
        TLSPayload += c;
      }
      
      Serial.println(TLSPayload);
      networkclient.stop(); // Always close the socket when finished!

      //Parse the JSON payload
      JsonDocument TLSJson;
      deserializeJson(TLSJson, TLSPayload);
      //Before we accept the new cert, we should check the SHA-256
      String SHATLS = TLSJson["sha"];
      String NewCert = TLSJson["cert"];
      //The SHA is the hash of "[serialNumber]:[wifiPassword]:[Cert]""
      Serial.print(F("JSON Hash:       ")); Serial.println(SHATLS);
      Serial.print(F("Calculated Hash: ")); Serial.println(calculateSha256(serialNumber + ":" + networkConfiguration.mqttKey + ":" + NewCert));
      if(SHATLS.equalsIgnoreCase(calculateSha256(serialNumber + ":" + networkConfiguration.mqttKey + ":" + NewCert))){
       //The hashes match!
       Serial.println(F("TLS cert was verified. Saving to memory..."));
       SPIFFS.remove("/cert.txt");
       File file = SPIFFS.open("/cert.txt", FILE_WRITE);
       //Need to change the written /n to an actual newline, clean up any other oddities in the file:
       NewCert.replace("\\n","\n");
       NewCert.replace("\r","");
       NewCert.replace("\"","");
       NewCert.trim();
       NewCert += "\n";
       if(file.print(NewCert)){
        file.close();
        rootCertificate = NewCert;
        Serial.println(F("New cert has been saved. Regular operation can now resume."));
        Serial.println(F("Our new cert is:"));
        Serial.println(rootCertificate);
        Serial.flush();
        delay(10);
        goto retryNetwork;
       } else{
        networkState.unavailable = true;
        Serial.println(F("Unknown error, could not write new cert to file?"));
        goto retryNetwork;
       }

      } else{
        //The hashes did not match, potental attack in progress!
        networkState.unavailable = true;
        faultReason = "TLS hash does not match!";
        Serial.println(F("CRITICAL ERROR: ATTEMPT WAS MADE TO LOAD BAD TLS CERTS!"));
        //statusMessage = "Attmpted to load cert with bad hash?";
        //messageToSend = true;
        delay(1000);
        goto retryNetwork;
      }
    }
  } //If not this, the connection worked and we can continue.
  socket.setReconnectInterval(2000); //Attempt to reconnect every 2 seconds if we lose connection
  Serial.println(F("Connecting to MQTT Broker"));
  unsigned long long SocketTime = millis64() + 15000;
  while(!mqtt.connect(serialNumber, serialNumber, networkConfiguration.mqttKey)){ //Use serial number as unique ID, username, and key as password.
    Serial.print(".");
    socket.loop();
    delay(500);
    if(SocketTime <= millis64()){
      Serial.println(F("Failed to connect to websocket! Retrying network altogether..."));
      goto retryNetwork;
    }
  } 
  Serial.println(F(" MQTT Connected!"));
  networkState.unavailable = false;

  //Subscribe to all MQTT topics relevant to us;
  mqttState.baseTopic = "makerspace/device/" + serialNumber;
  String SubAuth = mqttState.baseTopic + "/authTo/response";
  mqtt.subscribe(SubAuth, 2, [](const String& payload, const size_t size) {
    Serial.print(F("AuthTo Response: "));
    Serial.println(payload);
    mqttState.authResponse = payload;
    mqttState.newAuth = true;
  });
  String SubInfo = mqttState.baseTopic + "/info/response";
  mqtt.subscribe(SubInfo, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Info Response: "));
    Serial.println(payload);
    mqttState.infoResponse = payload;
    mqttState.newInfo = true;
  });
  String SubCommand = mqttState.baseTopic + "/command";
  mqtt.subscribe(SubCommand, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Command Input: "));
    Serial.println(payload);
    mqttState.commandResponse = payload;
    mqttState.newCommand = true;
  });
  String SubWelcome = mqttState.baseTopic + "/welcome/response";
  mqtt.subscribe(SubWelcome, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Welcome Response: "));
    Serial.println(payload);
    mqttState.welcomeResponse = payload;
    mqttState.newWelcome = true;
  });
  String SubPing = mqttState.baseTopic + "/ping";
  mqtt.subscribe(SubPing, 2, [](const String& payload, const size_t size) {
    //Serial.println(F("Ping Loopback."));
    mqttState.newPing = true;
  });

  networkState.unavailable = false;
  updateScreen = true;

  //We should request and report things when we (re)connect
  mqttState.reportConfig = true;
  mqttState.requestInfo = true;
  mqttState.sendPing = true;
  mqttState.nextPingTime = millis64() + 1000;
  JsonDocument NetConnect;
  NetConnect["status"] = "connected";
  //Check the interface in use
  bool usingWifi = false;
  #if CORE_HAS_ETHERNET
  if(ETH.hasIP()){
    NetConnect["interface"] = "ethernet";
    //TODO add ethernet IP to payload
  } else{
    NetConnect["interface"] = "wifi";
  }
  #else
    NetConnect["interface"] = "wifi";
    usingWifi = true;
  #endif
  //If we are using wifi, add more info;
  if(usingWifi){
    NetConnect["ssid"] = networkConfiguration.wifiSsid;
    NetConnect["bssid"] = WiFi.BSSIDstr();
    NetConnect["rssi"] = WiFi.RSSI();
    NetConnect["channel"] = WiFi.channel();
    NetConnect["ip"] = WiFi.localIP();
  }
  NetConnect["uptime"] = millis64() / 1000;
  String netPayload;
  serializeJson(NetConnect, netPayload);
  //If there is another log pending to send, do not overwrite it;
  if(!mqttState.logToSend){
    mqttState.logType = "network-info";
    mqttState.logMessage = netPayload;
    mqttState.logToSend = true;
  }

}

#if CORE_HAS_ETHERNET
void deriveEthernetMac(uint8_t macAddress[6]) {
  esp_read_mac(macAddress, ESP_MAC_WIFI_STA);
  for (int index = 5; index >= 0; index--) {
    if (++macAddress[index] != 0) {
      break;
    }
  }
}

String formatMacAddress(const uint8_t macAddress[6]) {
  char macString[18];
  snprintf(
    macString,
    sizeof(macString),
    "%02X:%02X:%02X:%02X:%02X:%02X",
    macAddress[0], macAddress[1], macAddress[2], macAddress[3], macAddress[4], macAddress[5]
  );
  return String(macString);
}

void setW5500MacAddress(const uint8_t macAddress[6]) {
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_ETH_CS, LOW);
  SPI.transfer(0x00);
  SPI.transfer(0x09);
  SPI.transfer(0x04);
  for (int index = 0; index < 6; index++) {
    SPI.transfer(macAddress[index]);
  }
  digitalWrite(PIN_ETH_CS, HIGH);
  SPI.endTransaction();
}

bool initializeEthernet() {
  uint8_t ethernetMac[6];
  deriveEthernetMac(ethernetMac);

  if (!ETH.begin(ETH_PHY_W5500, 1, PIN_ETH_CS, PIN_ETH_INT, PIN_ETH_RST, SPI)) {
    Serial.println(F("W5500 initialization failed; using WiFi."));
    return false;
  }

  setW5500MacAddress(ethernetMac);
  const unsigned long long ethernetDeadline = millis64() + 5000;
  while (!ETH.hasIP() && millis64() < ethernetDeadline) {
    delay(50);
  }

  if (!ETH.hasIP()) {
    Serial.println(F("W5500 has no IP address; using WiFi."));
    return false;
  }

  Serial.print(F("W5500 IP address: "));
  Serial.println(ETH.localIP());
  return true;
}

String getEthernetMacAddress() {
  uint8_t ethernetMac[6];
  deriveEthernetMac(ethernetMac);
  return formatMacAddress(ethernetMac);
}
#endif

String getActiveNetworkInterface() {
  if (networkState.transport == NetworkState::Transport::Ethernet) {
    return "Ethernet";
  }
  if (networkState.transport == NetworkState::Transport::WiFi) {
    return "WiFi: " + networkConfiguration.wifiSsid;
  }
  return "None";
}

void publishMqttstatusMessage(String Topic, String Payload){
  if(Payload != "Ping!"){
    //No point in printing the ping payload constantly
    Serial.print(F("Publishing "));
    Serial.print(Payload);
    Serial.print(F(" to topic "));
    Serial.println(Topic);
  }
  mqtt.publish(Topic, Payload, false, 2); //Send not retained at QoS 2
}

// Implement the HAL functions on an Arduino compatible system.
void mfrc630_SPI_transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) {
  for (uint16_t i=0; i < len; i++){
    rx[i] = SPI.transfer(tx[i]);
  }
}

// Select the chip and start an SPI transaction.
void mfrc630_SPI_select() {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));  // gain control of SPI bus
  digitalWrite(PIN_NFC_CS, LOW);
}

// Unselect the chip and end the transaction.
void mfrc630_SPI_unselect() {
  digitalWrite(PIN_NFC_CS, HIGH);
  SPI.endTransaction();    // release the SPI bus
}

void applyConfigurationJson(const String& json) {
    String USBConfig = json;
    JsonDocument ConfigJson;
    DeserializationError error = deserializeJson(ConfigJson, USBConfig);
    if (error) {
      Serial.print(F("Configuration JSON parsing failed: "));
      Serial.println(error.c_str());
      return;
    }
    //Is this a Core Config JSON, or a OneWire Config JSON?
    if(ConfigJson["Type"] == "OneWire"){
      //This is a onewire json, let's tell the bus manager to handle it.
      configOneWire = true;
      return;
    }
    String NewSSID = ConfigJson["wifiSsid"];
    if(ConfigJson["wifiSsid"].is<String>()){
      Serial.print(F("Set WiFi wifiSsid to: "));
      Serial.println(NewSSID);
      settings.putString("net.ssid", NewSSID);
    } else{
      Serial.println(F("Kept old WiFi wifiSsid."));
    }
    String NewPassword = ConfigJson["wifiPassword"];
    if(ConfigJson["wifiPassword"].is<String>()){
      Serial.print(F("Set WiFi password to: "));
      Serial.println(NewPassword);
      settings.putString("net.password", NewPassword);
    } else{
      Serial.println(F("Kept old WiFi password."));
    }
    String NewServer = ConfigJson["serverAddress"];
    if(ConfigJson["serverAddress"].is<String>()){
      Serial.print(F("Set server to: "));
      Serial.println(NewServer);
      settings.putString("net.server", NewServer);
    } else{
      Serial.println(F("Kept old server."));
    }
    String NewKey = ConfigJson["mqttKey"];
    if(ConfigJson["mqttKey"].is<String>()){
      Serial.println(F("Set a new key (not printed for security)"));
      settings.putString("net.mqttKey", NewKey);
    } else{
      Serial.println(F("Kept old key."));
    }
    String NewTimezone = ConfigJson["Timezone"];
    if(ConfigJson["Timezone"].is<String>()){
      Serial.print(F("Set timezone to: "));
      Serial.println(NewTimezone);
      settings.putString("system.timezone", NewTimezone);
    } else{
      Serial.println(F("Kept old timezone."));
    }
    String NewMakerspaceID = ConfigJson["makerspaceId"];
    if(ConfigJson["makerspaceId"].is<String>()){
      Serial.print(F("Set makerspace ID to: "));
      Serial.println(NewMakerspaceID);
      settings.putString("makerspace.id", NewMakerspaceID);
    } else{
      Serial.println(F("Kept old makerspace ID."));
    }
    String NewChannelCount = ConfigJson["channelCount"];
    if(ConfigJson["channelCount"].is<String>()){
      Serial.print(F("Set Channel Count to: "));
      Serial.println(NewChannelCount);
      settings.putString("channels.count", NewChannelCount);
    } else{
      Serial.println(F("Kept old channelCount."));
    }
    String NewInputMode = ConfigJson["inputMode"];
    if(ConfigJson["inputMode"].is<String>()){
      Serial.print(F("Set inputMode to: "));
      Serial.println(NewInputMode);
      settings.putString("access.input", NewInputMode);
    } else{
      Serial.println(F("Kept old inputMode"));
    }
    String NewStationName = ConfigJson["stationName"];
    if(ConfigJson["stationName"].is<String>()){
      Serial.print(F("Set Station Name to: "));
      Serial.println(NewStationName);
      settings.putString("station.name", NewStationName);
    } else{
      Serial.println(F("Kept old stationName"));
    }
    int NewMakerspaceNumber = ConfigJson["MakerspaceNumber"];
    if(ConfigJson["MakerspaceNumber"].is<int>()){
      Serial.println(F("Set makerspace number to: "));
      Serial.println(NewMakerspaceNumber);
      settings.putInt("makerspace.num", NewMakerspaceNumber);
    } else{
      Serial.println(F("Kept old MakerspaceNumber."));
    }
    String NewInterruptResponse = ConfigJson["interruptResponse"];
    if(ConfigJson["interruptResponse"].is<String>()){
      Serial.print(F("Set interrupt response to: "));
      Serial.println(NewInterruptResponse);
      settings.putString("access.intResp", NewInterruptResponse);
    } else{
      Serial.println(F("Kept old interruptResponse."));
    }
    if (ConfigJson["tapDuration"].is<JsonArray>()) {
      JsonArray durations = ConfigJson["tapDuration"].as<JsonArray>();

      if (durations.size() == 4) {
        Serial.print(F("Set Tap Durations (seconds) to: ["));
        for (int i = 0; i < 4; i++) {
          uint32_t dur = durations[i].as<uint32_t>();
          
          // Unique key for each channel (e.g. "TapDur0", "TapDur1"...)
          // Note: ESP32 Preferences keys must be 15 characters or less
          String key = "channels.tap" + String(i);
          settings.putUInt(key.c_str(), dur);

          Serial.print(dur);
          if (i < 3) Serial.print(F(", "));
        }
        Serial.println(F("]"));
      } else {
        Serial.println(F("Error: tapDuration must contain exactly 4 values. Kept old values."));
      }
    } else {
      Serial.println(F("Kept old tapDuration."));
    }
    Serial.println(F("Above settings have been saved to memory. Restart device to apply settings."));
}

void printConfigurationHelp() {
  Serial.println(F("Configuration commands: h, help, ?, restart, or a JSON object."));
  Serial.print(F("Device serial number: "));
  Serial.println(serialNumber);
  Serial.print(F("Device WiFi MAC address: "));
  Serial.println(getBaseMacAddress());
  Serial.println(F("JSON format:"));
  Serial.println(F("{\"wifiSsid\":\"network\",\"wifiPassword\":\"password\",\"serverAddress\":\"host\",\"mqttKey\":\"key\",\"Timezone\":\"-4\",\"makerspaceId\":\"36\",\"channelCount\":\"1\",\"inputMode\":\"INSERT\",\"stationName\":\"Machine\",\"MakerspaceNumber\":36,\"interruptResponse\":\"FAULT\",\"tapDuration\":[0,0,0,0]}"));
}

void processConfigurationCommand(String command) {
  command.trim();
  if (command.length() == 0) {
    return;
  }

  String normalizedCommand = command;
  normalizedCommand.toLowerCase();
  if (normalizedCommand == "h" || normalizedCommand == "help" || normalizedCommand == "?") {
    printConfigurationHelp();
    return;
  }

  if (normalizedCommand == "restart") {
    systemState.resetReason = "Serial Configuration Command";
    systemState.requestReset = true;
    Serial.println(F("Restart requested."));
    return;
  }

  if (command.startsWith("{")) {
    applyConfigurationJson(command);
    return;
  }

  Serial.println(F("Unknown configuration command. Send h for help."));
}

void runConfigurationController(void *pvParameters) {
  String input;
  unsigned long long lastInputTime = 0;

  Serial.println(F("Configuration controller started."));
  while (true) {
    while (Serial.available() > 0) {
      char character = static_cast<char>(Serial.read());
      if (character == '\n' || character == '\r') {
        processConfigurationCommand(input);
        input = "";
      } else {
        input += character;
        lastInputTime = millis64();
      }
    }

    if (input.length() > 0 && millis64() - lastInputTime >= 50) {
      processConfigurationCommand(input);
      input = "";
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void migrateLegacySettings() {
  struct LegacySetting {
    const char* currentKey;
    const char* legacyKey;
  };

  const LegacySetting settingsToMigrate[] = {
    {"net.server", "Server"},
    {"net.server", "serverAddress"},
    {"net.password", "Password"},
    {"net.password", "wifiPassword"},
    {"net.ssid", "SSID"},
    {"net.ssid", "wifiSsid"},
    {"net.mqttKey", "Key"},
    {"net.mqttKey", "mqttKey"},
    {"channels.count", "ChannelCount"},
    {"channels.count", "channelCount"},
    {"hardware.ver", "HWVer"},
    {"access.input", "InputMode"},
    {"access.input", "inputMode"},
    {"access.intResp", "IntResp"},
    {"access.intResp", "InterruptResponse"},
    {"station.name", "StationName"},
    {"station.name", "stationName"},
    {"system.timezone", "Timezone"},
    {"system.timezone", "timezone"},
    {"makerspace.id", "MakerspaceID"},
    {"makerspace.id", "makerspaceId"},
    {"system.reset", "ResetReason"},
    {"system.reset", "resetReason"},
  };

  for (const LegacySetting& setting : settingsToMigrate) {
    if (!settings.isKey(setting.currentKey) && settings.isKey(setting.legacyKey)) {
      settings.putString(setting.currentKey, settings.getString(setting.legacyKey));
    }
  }

  if (!settings.isKey("makerspace.num") && settings.isKey("SpaceNum")) {
    settings.putInt("makerspace.num", settings.getInt("SpaceNum"));
  }
  if (!settings.isKey("makerspace.num") && settings.isKey("MakerspaceNumber")) {
    settings.putInt("makerspace.num", settings.getInt("MakerspaceNumber"));
  }

  for (int channel = 0; channel < ChannelState::kMaximumChannels; channel++) {
    String currentKey = "channels.tap" + String(channel);
    String legacyKey = "TapDur" + String(channel);
    if (!settings.isKey(currentKey.c_str()) && settings.isKey(legacyKey.c_str())) {
      settings.putUInt(currentKey.c_str(), settings.getUInt(legacyKey.c_str()));
    }
    legacyKey = "TapDuration" + String(channel);
    if (!settings.isKey(currentKey.c_str()) && settings.isKey(legacyKey.c_str())) {
      settings.putUInt(currentKey.c_str(), settings.getUInt(legacyKey.c_str()));
    }
  }
}

String getBaseMacAddress() {
  uint8_t baseMac[6];
  char macStr[18]; 

  // We use (esp_mac_type_t) to force compatibility with the interface constant
  // If ESP_IF_WIFI_STA still fails, you can try 0 (which is the index for STA)
  if (esp_read_mac(baseMac, (esp_mac_type_t)ESP_IF_WIFI_STA) == ESP_OK) {
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             baseMac[0], baseMac[1], baseMac[2], 
             baseMac[3], baseMac[4], baseMac[5]);
    return String(macStr);
  } else {
    return String("00:00:00:00:00:00");
  }
}

void sendStartupstatusMessage(String statusMessage){
#if CORE_HAS_SCREEN
  JsonDocument Startup;
  Startup["startupMessage"] = statusMessage;
  String StartMessageString;
  serializeJson(Startup, StartMessageString);
  Serial0.println(StartMessageString);
  Serial0.flush();
#else
  (void)statusMessage;
#endif
}

String calculateSha256(String input) {
  // Create a buffer to hold the 32-byte (256-bit) hash output
  byte shaResult[32];
  
  // Initialize the mbedTLS message digest context
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  
  // Provide the input string and its length to the hash function
  mbedtls_md_update(&ctx, (const unsigned char*) input.c_str(), input.length());
  
  // Finalize the hash computation and store it in shaResult
  mbedtls_md_finish(&ctx, shaResult);
  mbedtls_md_free(&ctx);
  
  // Convert the 32-byte binary hash into a readable Hex String
  String hashStr = "";
  for(int i=0; i<32; i++) {
    if(shaResult[i] < 16) {
      hashStr += "0"; // Add leading zero for single-digit hex values
    }
    hashStr += String(shaResult[i], HEX);
  }
  
  return hashStr;
}

void IRAM_ATTR updateHobbsCounter(void* arg) {
  //This is called in an ISR to increment the Hobbs timer very precisely!
  for(int i = 0; i < channels.count; i++){
    if(channels.access[i]){
      channels.hobbsSeconds[i] = channels.hobbsSeconds[i] + 1;
    }
  }
}

