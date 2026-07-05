//ACS V3.0.0 Hardware, running CoreDuino code.

#define Version "2.0.8"
#define Hardware "3.0.0"

//How often you send a status message, in milliseconds
#define STATUS_INTERVAL 15000 

//Pin Definitions:
  #define SCREEN4 35
  #define IODIR1 36
  #define ETHCS 37
  #define ETHRST 38
  #define GPIO2 39
  #define GPIO1 40
  #define DEBUGLED 41
  #define IODIR2 42
  #define TX 39
  #define RX 40
  #define IODIR4 45
  #define ACCESS 46
  #define SCREEN5 48
  #define SCREEN6 34
  #define NFCCS 33
  #define DET1 47
  #define DET2 26
  #define INTERRUPT 21
  #define ONEWIRE 18
  #define SCREEN1 17
  #define SCREEN2 16
  #define SCREEN3 15
  #define ETHINT 14
  #define SCKPin 13
  #define MOSIPin 12
  #define MISOPin 11
  #define GPIO3 10
  #define SDA 9
  #define VARIANT 8
  #define LED 7
  #define IRQ 6
  #define IODIR3 5
  #define PDOWN 4
  #define SCL 3
  #define GPIO4 2
  #define BUZZER 1
  #define BUTTON 0

//Libraries:
  #include <OneWire.h>              //Replacing for WSACS API update...
  #include <ArduinoJson.h>          //Version 7.3.0 | Source: https://github.com/bblanchon/ArduinoJson
  #include <ArduinoJson.hpp>        //Version 7.3.0 | Source: https://github.com/bblanchon/ArduinoJson
  //WARNING: The original ESP32-OTA-Pull will not work with this code. Use the forked version linked below!
  #include <ESP32OTAPull.h>         //Version 1.0.1 | Source: https://github.com/JimSHED/ESP32-OTA-Pull-GitHub
  #include <WiFiClientSecure.h>     //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <HTTPClient.h>           //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <Preferences.h>          //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <esp_wifi.h>             //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <FS.h>                   //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <SPIFFS.h>               //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <Update.h>               //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <WiFi.h>                 //Version 3.1.1 | Inherent to ESP32 Arduino
  #include "esp_timer.h"            //Version 3.1.1 | Inherent to ESP32 Arduino
  #include "esp32s3/rom/rtc.h"      //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <nvs_flash.h>            //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <ESP32Time.h>            //Version 2.0.6 | Source: https://github.com/fbiego/ESP32Time
  #include <WebSocketsClient.h>     //Version 2.6.1 | Source: https://github.com/Links2004/arduinoWebSockets
  #include <ESP32Ping.h>            //Version 1.6   | Source: https://github.com/marian-craciunescu/ESP32Ping
  #include <ping.h>                 //Version 1.6   | Source: https://github.com/marian-craciunescu/ESP32Ping
  #include "esp_ota_ops.h"          //Version 3.1.1 | Inherent to ESP32 Arduino
  #include <MQTTPubSubClient.h> 
  #include <SPI.h>
  #include <mfrc630.h>
  #include <Adafruit_NeoPixel.h>
  #include "USB.h"
  #include <esp_system.h>
  #include <esp_mac.h>
  #include "esp_efuse.h"
  #include "esp_efuse_table.h"
  #include "SparkFun_LIS2DH12.h" //Click here to get the library: http://librarymanager/All#SparkFun_LIS2DH12
  #include <Wire.h>

//Objects:
  Preferences settings;
  WiFiClientSecure client;
  JsonDocument usbjson;
  HTTPClient http;
  ESP32OTAPull ota;
  ESP32Time rtc;
  WebSocketsClient socket;
  MQTTPubSub::PubSubClient<256> mqtt;
  OneWire ds(ONEWIRE); 
  Adafruit_NeoPixel CBI(1, LED, NEO_RGB + NEO_KHZ800);
  SPARKFUN_LIS2DH12 accel;  
  //USBCDC Serial;

extern "C" bool verifyRollbackLater() {
  //This code is run to verify the OTA before actual setup.
  //Since we are handling OTA verification ourselves, we just return true.
  return true;
}

//SSL Certificate. the ISRG X1 cert that encompasses R12 and R13 for let's encrypt. Expires in 2030?
const char *root_ca = R"literal(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)literal";

//Variables - Inter-Task Communication
bool GamerMode = 1;  //Set to 0 to disable gamer mode, i.e. cycle RGB. Used during boot.
bool RequestReset = 0; //Other tasks can set this to 1 to tell the ResetController to start a restart. 
String ResetReason; //Tells the RestartController why we are restarting.
bool Access = 0; //Used to tell the frontend to enable the access signal to the bus. 
String UID; //Stores the UID of the user currently using the machine.
String FoundUID = ""; //Stores the last-found UID
bool ChangeBeep = 0; //Lets the frontend know to beep.
bool FaultBeep = 0; //We use the 3 beep normally for fault to indicate cannot welcome/auth due to no network, to differentiate from welcome/auth denied.

//Variables - System State
bool Identify = 0; //Set to 1 to play an identification alarm/buzzer.
String State = "UNKNOWN"; //State of the machine, uses the WSACS API standard wording (IDLE, UNLOCKED, ALWAYS_ON, LOCKED_OUT, FAULT, UNKNOWN)
String StateChangeReason; //What caused the state to change
String InputMode = "INSERT"; //Stores how we ingest cards.
bool PendingApproval = 0; //Set to 1 when we have a card present that hasn't been authed yet, this is used for LED animations. 
bool AccessDenied = 0; //Set to 1 when a card is present but has been denied, for LED animations. 
bool CardPresent = 0; //Used to track if there is a card present in the machine.
String LastState = "UNKNOWN"; //Stores what the state was previously, to detect changes.
String PreservedLastState = "UNKNOWN";
bool LockWhenIdle = 0;
bool RestartWhenUnused = 0;
bool WelcomeFlag = 0;
bool NoNetwork = 1;
bool ScheduledRestart = false; //Used to indicate it is time for a regular restart. 
unsigned long long ScheduledRestartTime = 0; //Used to give the user some breathing room before a shutdown occurs.
bool ImminentShutdown = false; //Used to let the frontend know to play a flashing warning light
String TapUID; //Stores the UID between cycles for comparison when in tap mode.
bool UserWelcomed = 0;
unsigned long long NextStatusTime = 0;

//Variables - Config
String SerialNumber;
String Password;
String SSID;
String Server;
String Key;
int MakerspaceID;

//Variables - MQTT Incoming
String AuthResponse; 
bool NewAuth = 0;
String InfoResponse;
bool NewInfo = 0;
String CommandResponse;
bool NewCommand = 0;
bool NewPing = 1;
bool SendPing = 0;
unsigned long long NextPingTime = 0;
bool OTAValid = 0;
bool OTALocked = 0;
bool NewWelcome = 0;
String WelcomeResponse;
bool WelcomingPending = 0;

//Variables - MQTT Outgoing
String BaseTopic; //Used to store the root topic that all others are appended to.
String Message; //Info for the history tab
bool MessageToSend = 0;
bool LogToSend = 0;
String Log; //Non-message log to send to the server
String LogType;
bool SendAuth = 0;
bool StateChange = 0;
bool ReportConfig = 0;
bool RequestInfo = 0;
bool SendStatus = 0;
bool SendWelcome = 0;

//Variables - Inter-Task Communication
bool SealBroken = 0;  //Set to 1 if there is an incorrect OneWire device on the bus. 
bool ReSealBus = 0;
bool OverTemp = 0;    //Set to 1 if there is a device overtemperature on the bus, so we can fault. 
byte liveAddresses[5][8];      //OneWire addresses of what is currently connected, for server reporting.
int liveAddressCount = 0;                //Number of currently connected devices on the bus, for server reporting.
byte deviceCount = 0; //Tracks the number of OneWire devices found on the bus.

//Variables - Inter-Task Communication (Inside Frontend)
bool NewLED = 0; //Set to 1 when there is new valid data to send to the frontend for the LEDs.
byte Red = 0; //Tracks the red channel light intensity
byte Green = 0; //Tracks the green channel light intensity
byte Blue = 0; //Tracks the blue channel light intensity
bool NewBuzzer = 0; //Set to 1 when there is a new valid buzzer tone to send.
unsigned int Tone = 0; //Set to the tone that the buzzer should play.
bool ResetLED = 0; //Set to 1 to take priority over the LED controller, to indicate the restart is imminent.
bool UnlockedBeep = 0;
bool SingleBeep = 0;

//For handling OneWire devices
struct Device {
  byte address[8];
  byte deviceType;         
  byte highTempLimit;      
  uint32_t classification; 
  float currentTemp;       
  bool isAlarming;         
  bool isOnline;           // <--- Track if it's actually responding
};

Device sensorList[10];

//Variables related to any connected screen;
bool UpdateScreen = false;
String AuthReason = "";
String FaultReason = "";

void setup() {
  // put your setup code here, to run once:

  //In case we crashed, immediately turn off buzzer and set LED red;
  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);
  CBI.begin();
  CBI.setBrightness(30);
  CBI.setPixelColor(0, 255, 0, 0);
  CBI.show();

  pinMode(ACCESS, OUTPUT);
  pinMode(INTERRUPT, INPUT);

  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, 44, 43);

  Serial.println(F("STARTUP"));
  Serial.flush();
  delay(500);

  sendStartup("Starting Tasks...");

  xTaskCreate(AVControl, "AVControl", 2048, NULL, 5, NULL);
  xTaskCreate(RestartController, "RestartController", 2048, NULL, 5, NULL);

  //Start i2C
  Wire.begin(SDA, SCL);
  accel.begin();
  accel.setScale(LIS2DH12_2g);
  accel.setDataRate(LIS2DH12_ODR_10Hz);

  //Start SPIFFS:
  if(!SPIFFS.begin(1)){
    Serial.println(F("SPIFFS Mount Failed!"));
    delay(1000);
    ESP.restart();
  }

  //Load settings from memory

  //Get our serial number;
  // The ID is 128 bits = 16 bytes
  uint8_t unique_id[16]; 
  
  // ESP_EFUSE_OPTIONAL_UNIQUE_ID is the constant defined in the IDF table
  esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, unique_id, 128);

  if (err == ESP_OK) {
    Serial.print("Serial Number: ");
    for (int i = 0; i < 16; i++) {
      if (unique_id[i] < 0x10) SerialNumber += "0"; // Lead with zero if byte < 16
      SerialNumber += String(unique_id[i], HEX);
    }
    SerialNumber.toUpperCase();
    Serial.print(SerialNumber);
    Serial.println();
  } else {
    Serial.printf("Error reading eFuse: 0x%X\n", err);
    Serial.println("Note: This ID may not exist on original ESP32 (Non-S2/S3) models.");
  }

  //Get our MAC address for printing, in V3.0.0 hardware this is our base MAC
  Serial.print(F("WiFi MAC Address: "));
  Serial.println(getBaseMacAddress());

  settings.begin("settings", false);

  Serial.println(F("Have a config for me? I'll wait a second..."));
  delay(1000);
  CheckforConfig();

  if(!settings.isKey("Server")){
    //We don't have a valid config?
    sendStartup("ERROR: Missing Config!");
    while(1){
      Serial.println(F("Missing config. Please provide a JSON with the following keys: "));
      Serial.println(F("WiFi SSID, WiFi Password, Server, Server Key, Timezone, MakerspaceID"));
      Serial.print(F("Device WiFi MAC Address: "));
      Serial.println(getBaseMacAddress());
      Serial.print(F("Device Serial Number: "));
      Serial.println(SerialNumber);
      CheckforConfig();
      delay(1000);
    }
  }
  Server = settings.getString("Server");
  Password = settings.getString("Password");
  if(Password.equalsIgnoreCase("null")){
    //Use a real NULL password.
    Password = "";
  }
  SSID = settings.getString("SSID");
  Server = settings.getString("Server");
  Key = settings.getString("Key");
  int TimezoneHr;
  if(settings.isKey("Timezone")){
    TimezoneHr = settings.getString("Timezone").toInt();
  } else{
    TimezoneHr = -4; //Hardcoded EST
  }
  rtc.offset = TimezoneHr * 3600;
  MakerspaceID = settings.getString("MakerspaceID").toInt();

  Serial.println(F("Settings loaded."));
  Serial.flush();

  sendStartup("Settings Loaded.");

  Serial.println(F("Started Tasks."));
  Serial.flush();

  //Start SPI here, in case we want to use Ethernet in the future. 
  //SCK, MISO, MOSI, SS
  pinMode(NFCCS, OUTPUT);
  SPI.begin(SCKPin, MISOPin, MOSIPin, -1);

  Serial.println(F("Started SPI."));
  Serial.flush();

  sendStartup("Starting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, Password);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  if(WiFi.status() != WL_CONNECTED){
    WiFi.reconnect(); //Force a manual connect attempt
    Serial.println(F("Waiting for first WiFi connect"));
    while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(500);
    }
  }
  Serial.println(F("WiFi connected."));
  
  sendStartup("WiFi Started.");
  //Also get rid of "No NET" on screen
  JsonDocument NoNetStart;
  NoNetStart["noNetwork"] = false;
  String NoNetToSend;
  serializeJson(NoNetStart, NoNetToSend);
  Serial0.println(NoNetToSend);

  delay(500);

  sendStartup("Checking for OTA...");
  Serial.println(F("Checking for OTA..."));


  //Check for an OTA update, install it if there is one present.
  ota.SetCallback(callback_percent);
  ota.SetConfig(Hardware);
  ota.OverrideDevice("ACS Core");
  ota.EnableSerialDebug();
  int otaresp = ota.CheckForOTAUpdate("https://raw.githubusercontent.com/rit-construct-makerspace/access-control-firmware/refs/heads/main/otadirectory.json", Version);
  Serial.print(F("OTA Response: "));
  Serial.println(errtext(otaresp));

  //If we made it past the OTA, then we are ready for normal operation.

  sendStartup("Connecting MQTT...");

  mqtt.begin(socket); //Enable MQTT on the websocket

  NetworkConnect();

  //If we cared about why we restarted, this'd be the place to handle it.

  //Start the NFC reader, make sure it is working as expected. 
  mfrc630_AN1102_recommended_registers(MFRC630_PROTO_ISO14443A_106_MILLER_MANCHESTER);
  mfrc630_write_reg(0x28, 0x8E);
  mfrc630_write_reg(0x29, 0x15);
  mfrc630_write_reg(0x2A, 0x11);
  mfrc630_write_reg(0x2B, 0x06);

  //We should initialize the OneWire bus here, check for the right devices, etc.
  Serial.println(F("Starting OneWire..."));
  sendStartup("Starting OneWire...");
  discoverDevices();
  loadInventoryFromFile();
  if(deviceCount == 0){
     Serial.println(F("Inventory empty! Scanning to initialize..."));
     discoverDevices(); // This sets the initial baseline
     saveInventoryToFile(); // Save it so it's not empty next time
  }
  //We should also immediately do a OneWire integrity check.
  Serial.println(F("Checking Bus Integrity..."));
  checkBusHealth();
  updateBusTemperatures();
  refreshLiveAddressBuffer();

  //Going forward, we will check the OneWire bus in a different task to make life easier.
  xTaskCreate(BusManager, "BusManager", 4096, NULL, 5, NULL);

  //Time to loop!
  xTaskCreate(MachineState, "MachineState", 4096, NULL, 5, NULL);
  GamerMode = 0; //Disable the startup lighting

  xTaskCreate(SceenController, "ScreenController", 4096, NULL, 5, NULL);

}

void loop() {
  // put your main code here, to run repeatedly:

  //Step 0: Call the MQTT updater;
  mqtt.update();

  //Check for a config JSON:
  CheckforConfig();

  //Step 4: Communicate with the server

  //Only do all this if we have a connection
  if(mqtt.isConnected() && !NoNetwork){

    if(NoNetwork){
      NoNetwork = false;
      UpdateScreen = true;
    }

    JsonDocument outgoing; //Json to construct the outgoing message in

     //Step 4.1: See if we have any outgoing messages, and send them.
    if(MessageToSend){
      //Send a message to the history
      MessageToSend = 0;
      outgoing["auditLog"] = true; //Print in the history
      outgoing["message"] = Message;
      outgoing["category"] = "message";
      String MessagePayload;
      serializeJson(outgoing, MessagePayload);
      outgoing.clear(); //Clear so other sends can use it
      String MessageTopic = BaseTopic + "/log";
      publish(MessageTopic, MessagePayload);
    }
    if(LogToSend){
      //Send a log to the audit logs (not the user-visible history)
      LogToSend = 0;
      outgoing["auditLog"] = false; //Don't print in the history
      outgoing["message"] = Log;
      outgoing["type"] = LogType;
      String LogPayload;
      serializeJson(outgoing, LogPayload);
      outgoing.clear();
      String LogTopic = BaseTopic + "/log";
      publish(LogTopic, LogPayload);
      LogType = "message"; //Default value unless we say otherwise.
    }
    if(SendAuth){
      //Send an auth request to the server
      SendAuth = 0;
      outgoing["state"] = "UNLOCKED";
      outgoing["cardTagID"] = UID;
      String AuthPayload;
      serializeJson(outgoing, AuthPayload);
      outgoing.clear();
      String AuthTopic = BaseTopic + "/authTo/request";
      publish(AuthTopic, AuthPayload);
    }
    if(StateChange){
      //Send report of a changed state
      StateChange = 0;
      if(State != "WELCOMING"){
        //We don't report state change when we are in welcoming.
        JsonArray stateChannels = outgoing["channels"].to<JsonArray>();
        JsonObject stateObject = stateChannels.createNestedObject();
        stateObject["channelID"] = 0;
        stateObject["fromState"] = PreservedLastState;
        stateObject["toState"] = State;
        stateObject["reason"] = StateChangeReason;
        outgoing["currentCardTag"] = UID;
        String StateChangePayload;
        serializeJson(outgoing, StateChangePayload);
        outgoing.clear();
        String StateChangeTopic = BaseTopic + "/stateChange";
        publish(StateChangeTopic, StateChangePayload);
        //At the end, set change reason to nothing:
        StateChangeReason = "";
      }
    }
    if(ReportConfig){
      //Report the current configuration
      ReportConfig = 0;
      JsonArray configChannels = outgoing["channels"].to<JsonArray>();
      JsonObject configObject = configChannels.createNestedObject();
      configObject["channelID"] = 0;
      configObject["tempDuration"] = 0;
      outgoing["inputMode"] = InputMode;
      JsonObject configDeployment = outgoing["deployment"].to<JsonObject>();
      configDeployment["SN"] = SerialNumber;
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
            // Here we grab the deviceType and other data from the struct
            deviceObj["type"] = sensorList[j].deviceType; 
            deviceObj["identifier"] = sensorList[j].classification; //Server doesn't expect this yet, but we should send it
            break;
          }
        }
      }
      JsonObject flags = outgoing["flags"].to<JsonObject>();
      flags["lockWhenIdle"] = LockWhenIdle;
      flags["restartWhenUnused"] = RestartWhenUnused;
      flags["welcoming"] = WelcomeFlag;
      String FWVer = "CoreDuino " + String(Version);
      outgoing["firmware"] = FWVer;
      String ConfigPayload;
      serializeJson(outgoing, ConfigPayload);
      outgoing.clear();
      String ConfigTopic = BaseTopic + "/config/report";
      publish(ConfigTopic, ConfigPayload);
    }
    if(RequestInfo){
      //Request information from the server
      RequestInfo = 0;
      JsonArray infoFields = outgoing["fields"].to<JsonArray>();
      infoFields.add("TIME");
      if(State == "UNKNOWN"){
        //We don't know what state we should be in, so request it. 
        infoFields.add("STATE");
      }
      infoFields.add("FLAGS"); //Check our flags, mostly for welcoming
      String InfoPayload;
      serializeJson(outgoing, InfoPayload);
      outgoing.clear();
      String InfoTopic = BaseTopic + "/info/request";
      publish(InfoTopic, InfoPayload);
    }
    if(SendStatus){
      //Send our current status to the server
      SendStatus = 0;
      JsonArray statusChannels = outgoing["channels"].to<JsonArray>();
      if(State != "WELCOMING"){
        //We don't send this in welcoming mode
        JsonObject statusObject = statusChannels.createNestedObject();
        statusObject["channelID"] = 0;
        statusObject["state"] = State;
      }
      outgoing["currentCardTag"] = UID;
      String StatusPayload;
      serializeJson(outgoing, StatusPayload);
      outgoing.clear();
      String StatusTopic = BaseTopic + "/status";
      publish(StatusTopic, StatusPayload);
    }
    if(SendWelcome){
      //Send a welcome message to the server
      SendWelcome = 0;
      outgoing["cardTagID"] = UID;
      String WelcomePayload;
      serializeJson(outgoing, WelcomePayload);
      outgoing.clear();
      String WelcomeTopic = BaseTopic + "/welcome/request";
      publish(WelcomeTopic, WelcomePayload);
    }

    //Step 4.2: If we got a message, mark the OTA as secure.
    if(OTAValid && !OTALocked){
      //We got a message from the server, so we know the OTA is safe to keep.
      OTALocked = 1; //No need to do this again.
      const esp_partition_t *running_partition = esp_ota_get_running_partition();
      esp_ota_img_states_t ota_state;
      esp_ota_get_state_partition(running_partition, &ota_state);
      if(ota_state == ESP_OTA_IMG_PENDING_VERIFY){
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println(F("OTA update marked valid."));
        Message = "OTA update marked valid.";
        MessageToSend = 1;
      }
    }

    JsonDocument incoming; //Json doucment to parse the incoming
    
    //Step 4.3: Process any incoming messages
    if(NewAuth){
      //Process a response to an auth request.
      NewAuth = 0;
      deserializeJson(incoming, AuthResponse);
      bool IsAuthed = incoming["channels"][0]["approved"].as<bool>();
      String AuthID = incoming["cardTagID"].as<String>();
      AuthReason = incoming["channels"][0]["reason"].as<String>();
      PendingApproval = false;
      if(State == "IDLE"){
        if(IsAuthed){
          Serial.println(F("Access Granted!"));
          if(AuthID == UID){
            Serial.println(F("UIDs match. Unlocking."));
            State = "UNLOCKED";
            StateChangeReason = "AUTHED"; 
            UnlockedBeep = true;
          }
        } else{
          Serial.println(F("Access Denied!"));
          if(CardPresent){
            AccessDenied = 1;
          }
        }
      } else{
        Serial.println(F("Ignoring auth due to invalid state."));
      }
      UpdateScreen = true;
    }
    if(NewInfo){
      //Process a response to an info request.
      NewInfo = 0;
      deserializeJson(incoming, InfoResponse);
      //Set the time;
      if(incoming.containsKey("time")){
        unsigned long long millisecondTime = incoming["time"];
        rtc.setTime(millisecondTime/1000);
        Serial.print(F("Time set to: "));
        Serial.println(rtc.getDateTime(true));
      }
      if(incoming.containsKey("flags")){
        JsonObject flagObj = incoming["flags"].as<JsonObject>();
        if(flagObj.containsKey("lockWhenIdle")){
          LockWhenIdle = flagObj["lockWhenIdle"].as<bool>();
          Serial.print(F("Server set LockWhenIdle to: "));
          Serial.println(LockWhenIdle);
        }
        if(flagObj.containsKey("restartWhenUnused")){
          RestartWhenUnused = flagObj["restartWhenUnused"].as<bool>();
          Serial.print(F("Server set RestartWhenUnused to: "));
          Serial.println(RestartWhenUnused);
        }
        if(flagObj.containsKey("welcoming")){
          if(WelcomeFlag != flagObj["welcoming"].as<bool>()){
            WelcomeFlag = flagObj["welcoming"].as<bool>();
            if(WelcomeFlag){
            Serial.println(F("Server flag set to enter welcoming mode."));
            State = "WELCOMING";
            } else{
              Serial.println(F("Server flag unset for welcoming mode. Entering state 'UNKNOWN'"));
              State = "UNKNOWN";
              StateChangeReason = "SERVER_COMMANDED";
              //We should ask what state we should be in
              RequestInfo = 1;
            }
          }
        }
      }
      if((incoming.containsKey("state")) && (State == "UNKNOWN")){
        State = incoming["state"][0]["state"] | "UNKNOWN";
        if(State == "UNLOCKED" || State == "ALWAYS_ON"){
          //We shouldn't go into these states
          State = "IDLE";
        }
        StateChangeReason = "COMMANDED";
        SingleBeep = 1;
        if((State == "UNLOCKED") || (State == "ALWAYS_ON")){
          //We do not restart into these states;
          State = "IDLE";
        }
        if(State == "FAULT"){
          //If we used to be faulted, restart in lockout instead.
          State = "LOCKED_OUT";
        }
        Serial.print(F("State set to > "));
        Serial.print(State);
        Serial.println(F(" < on startup."));
      }
      ReportConfig = 1; //Once we get some info, we should send our configuration.
      SendStatus = 1; //Once we get some info, we should send our status.
      UpdateScreen = true;
    }
    if(NewCommand){
      //Process an incoming command.
      NewCommand = 0;
      deserializeJson(incoming, CommandResponse);
      //State change command
      if(incoming.containsKey("toState")){
        if(State != "WELCOMING"){
          //We never command state out of welcoming since it is set by flags
          State = incoming["toState"][0]["state"] | "UNKNOWN";
        }
        if(State == "UNLOCKED" && !CardPresent){
          //Don't unlock if no card present
          State = "IDLE";
        }
        StateChangeReason = "COMMANDED";
        SingleBeep = 1;
        Serial.print(F("Commanded state to: "));
        Serial.println(State);
      }
      //Set flags
      if(incoming.containsKey("flags")){
        JsonObject flagObj = incoming["flags"].as<JsonObject>();
        if(flagObj.containsKey("lockWhenIdle")){
          LockWhenIdle = flagObj["lockWhenIdle"].as<bool>();
          Serial.print(F("Server set LockWhenIdle to: "));
          Serial.println(LockWhenIdle);
        }
        if(flagObj.containsKey("restartWhenUnused")){
          RestartWhenUnused = flagObj["restartWhenUnused"].as<bool>();
          Serial.print(F("Server set RestartWhenUnused to: "));
          Serial.println(RestartWhenUnused);
        }
        if(flagObj.containsKey("welcoming")){
          if(WelcomeFlag != flagObj["welcoming"].as<bool>()){
            WelcomeFlag = flagObj["welcoming"].as<bool>();
            if(WelcomeFlag){
            Serial.println(F("Server flag set to enter welcoming mode."));
            State = "WELCOMING";
            } else{
              Serial.println(F("Server flag unset for welcoming mode. Entering state 'UNKNOWN'"));
              State = "UNKNOWN";
              StateChangeReason = "SERVER_COMMANDED";
              //We should ask what state we should be in
              RequestInfo = 1;
            }
          }
        }
      }
      //Action to do something
      if(incoming.containsKey("action")){
        if(incoming["action"] == "RESTART"){
          Serial.println(F("Server commanded restart!"));
          Serial.flush();
          ResetReason = "Server Ordered";
          RequestReset = true;
        }
        if((incoming["action"] == "SEAL") && SealBroken){
          Serial.println(F("Server commanded bus integrity re-seal."));
          ReSealBus = true;
        }
        if(incoming["action"] == "IDENTIFY"){
          Serial.println(F("Server commanded identify."));
          Identify = !Identify;
          if(!Identify){
            //Play a single beep to end the identify command.
            SingleBeep = true;
          }
        }
        if(incoming["action"] == "SCHEDULED_RESTART"){
          Serial.println(F("Server indicated it is time for a scheduled restart."));
          ScheduledRestart = true;
        }

      }
      UpdateScreen = true;
    }
    if(NewWelcome){
      //Response to welcoming a user
      NewWelcome = 0;
      deserializeJson(incoming, WelcomeResponse);
      bool IsWelcomed = incoming["welcomed"];
      String WelcomeID = incoming["cardTagID"];
      String WelcomeReason = incoming["reason"];
      if(IsWelcomed){
        //User was welcomed into the space properly.
        Serial.println(F("User welcomed!"));
        if(UID == WelcomeID){
          //The user's card is still here, so beep and light up.
          UserWelcomed = 1;
        } else{
          Serial.println(F("But their card isn't here anymore, so we will skip the lights/sounds."));
        }
      } else{
        //User was denied entry into the space.
        Serial.print(F("User denied! Reason: "));
        Serial.println(WelcomeReason);
        AccessDenied = 1; //Act like we denied the user access
      }
      UpdateScreen = true;
    }

    //Step 4.4: Send a ping if requested
    if(SendPing){
      String PingTopic = BaseTopic + "/ping";
      publish(PingTopic, "Ping!");
      //Serial.println(F("Ping sent."));
      SendPing = 0;
      NextPingTime = millis64() + 1000;
    }
    
  } else{
    Serial.println(F("No network?"));
    NoNetwork = true;
    UpdateScreen = true;
    NetworkConnect();
  }

}

void callback_percent(int offset, int totallength) {
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
    Serial0.println(CoreOTAString);
  }
}

const char *errtext(int code) {
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

void NetworkConnect(){
  //Check the WiFi first
  retryNetwork:
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
  
  //Start our websocket connection
  socket.disconnect();
  //socket.begin("129.21.61.154", 3000, "/mqtt", "mqtt"); //Test broker running on Stephen computer. 
  socket.beginSslWithCA(Server.c_str(), 443, "/mqtt", root_ca, "mqtt");
  socket.setReconnectInterval(2000); //Attempt to reconnect every 2 seconds if we lose connection
  Serial.println(F("Connecting to MQTT Broker"));
  unsigned long long SocketTime = millis64() + 15000;
  while(!mqtt.connect(SerialNumber, SerialNumber, Key)){ //Use serial number as unique ID, username, and key as password.
    Serial.print(".");
    delay(500);
    if(SocketTime <= millis64()){
      Serial.println(F("Failed to connect to websocket! Retrying network altogether..."));
      goto retryNetwork;
    }
  } 
  Serial.println(F(" MQTT Connected!"));

  //Subscribe to all MQTT topics relevant to us;
  BaseTopic = "makerspace/" + String(MakerspaceID) + "/device/" + SerialNumber;
  String SubAuth = BaseTopic + "/authTo/response";
  mqtt.subscribe(SubAuth, 2, [](const String& payload, const size_t size) {
    Serial.print(F("AuthTo Response: "));
    Serial.println(payload);
    AuthResponse = payload;
    NewAuth = 1;
    OTAValid = 1;
  });
  String SubInfo = BaseTopic + "/info/response";
  mqtt.subscribe(SubInfo, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Info Response: "));
    Serial.println(payload);
    InfoResponse = payload;
    NewInfo = 1;
    OTAValid = 1;
  });
  String SubCommand = BaseTopic + "/command";
  mqtt.subscribe(SubCommand, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Command Input: "));
    Serial.println(payload);
    CommandResponse = payload;
    NewCommand = 1;
    OTAValid = 1;
  });
  String SubPing = BaseTopic + "/ping";
  mqtt.subscribe(SubPing, 2, [](const String& payload, const size_t size) {
    //Serial.println(F("Ping Loopback."));
    NewPing = 1;
    OTAValid = 1;
  });
  String SubWelcome = BaseTopic + "/welcome/response";
  mqtt.subscribe(SubWelcome, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Welcome Response: "));
    Serial.println(payload);
    WelcomeResponse = payload;
    NewWelcome = 1;
    OTAValid = 1;
  });

  NoNetwork = false;
  UpdateScreen = true;

  //We should request and report things when we (re)connect
  ReportConfig = 1;
  RequestInfo = 1;
  SendPing = 1;
  NextPingTime = millis64() + 1000;
  LogType = "Network Connected";
  Log = "Network Connected";
  LogToSend = true;
}

void publish(String Topic, String Payload){
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
  digitalWrite(NFCCS, LOW);
}

// Unselect the chip and end the transaction.
void mfrc630_SPI_unselect() {
  digitalWrite(NFCCS, HIGH);
  SPI.endTransaction();    // release the SPI bus
}

String readCDCString( uint32_t timeout = 1000){
  String result = "";
  unsigned long long startMillis = millis64();
  while(millis64() - startMillis < timeout){
    size_t availableBytes = Serial.available();
    if(availableBytes > 0){
      uint8_t buf[availableBytes];
      size_t readCount = Serial.read(buf, availableBytes);
      for(size_t i = 0; i < readCount; i++){
        result += (char)buf[i];
      }
      startMillis = millis64();
    }
    delay(1);
  }
  return result;
}

void CheckforConfig(){
  //Called to see if a config JSON has been sent via USB.
  if(Serial.available()){
    String USBConfig = readCDCString(20);
    JsonDocument USBJson;
    deserializeJson(USBJson, USBConfig);
    String NewSSID = USBJson["SSID"];
    if(USBJson["SSID"].is<String>()){
      Serial.print(F("Set WiFi SSID to: "));
      Serial.println(NewSSID);
      settings.putString("SSID", NewSSID);
    } else{
      Serial.println(F("Kept old WiFi SSID."));
    }
    String NewPassword = USBJson["Password"];
    if(USBJson["Password"].is<String>()){
      Serial.print(F("Set WiFi password to: "));
      Serial.println(NewPassword);
      settings.putString("Password", NewPassword);
    } else{
      Serial.println(F("Kept old WiFi password."));
    }
    String NewServer = USBJson["Server"];
    if(USBJson["Server"].is<String>()){
      Serial.print(F("Set server to: "));
      Serial.println(NewServer);
      settings.putString("Server", NewServer);
    } else{
      Serial.println(F("Kept old server."));
    }
    String NewKey = USBJson["Key"];
    if(USBJson["Key"].is<String>()){
      Serial.println(F("Set a new key (not printed for security)"));
      settings.putString("Key", NewKey);
    } else{
      Serial.println(F("Kept old key."));
    }
    String NewTimezone = USBJson["Timezone"];
    if(USBJson["Timezone"].is<String>()){
      Serial.print(F("Set timezone to: "));
      Serial.println(NewTimezone);
      settings.putString("Timezone", NewTimezone);
    } else{
      Serial.println(F("Kept old timezone."));
    }
    String NewMakerspaceID = USBJson["MakerspaceID"];
    if(USBJson["MakerspaceID"].is<String>()){
      Serial.print(F("Set makerspace ID to: "));
      Serial.println(NewMakerspaceID);
      settings.putString("MakerspaceID", NewMakerspaceID);
    } else{
      Serial.println(F("Kept old makerspace ID."));
    }
    Serial.println(F("Above settings have been saved to memory. Restart device to apply settings."));
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

void sendStartup(String Message){
  JsonDocument Startup;
  Startup["startupMessage"] = Message;
  String StartMessageString;
  serializeJson(Startup, StartMessageString);
  Serial0.println(StartMessageString);
  Serial0.flush();
}