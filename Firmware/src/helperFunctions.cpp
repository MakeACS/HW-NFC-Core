#include "Globals.h"
#include "TaggedSerial.h"
#include "helperFunctions.h"
#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>

namespace {
TaggedSerial<decltype(::Serial)> helperSerial(::Serial, "[main] ");
}

#define Serial helperSerial

String getCertificateCommonName() {
  mbedtls_x509_crt certificate;
  mbedtls_x509_crt_init(&certificate);

  int result = mbedtls_x509_crt_parse(
    &certificate,
    reinterpret_cast<const unsigned char *>(rootCertificate.c_str()),
    rootCertificate.length() + 1
  );
  if (result < 0) {
    mbedtls_x509_crt_free(&certificate);
    return "";
  }

  for (mbedtls_x509_name *name = &certificate.subject; name != nullptr; name = name->next) {
    const char *attributeName = nullptr;
    if (mbedtls_oid_get_attr_short_name(&name->oid, &attributeName) == 0 &&
      attributeName != nullptr && strcmp(attributeName, "CN") == 0) {
      String commonName;
      for (size_t index = 0; index < name->val.len; index++) {
        commonName += static_cast<char>(name->val.p[index]);
      }
      mbedtls_x509_crt_free(&certificate);
      return commonName;
    }
  }

  mbedtls_x509_crt_free(&certificate);
  return "";
}

String getCertificateExpiration() {
  mbedtls_x509_crt certificate;
  mbedtls_x509_crt_init(&certificate);

  int result = mbedtls_x509_crt_parse(
    &certificate,
    reinterpret_cast<const unsigned char *>(rootCertificate.c_str()),
    rootCertificate.length() + 1
  );
  if (result < 0) {
    mbedtls_x509_crt_free(&certificate);
    return "";
  }

  const mbedtls_x509_time &expiration = certificate.valid_to;
  char expirationText[32];
  snprintf(
    expirationText,
    sizeof(expirationText),
    "%04d-%02d-%02d %02d:%02d:%02d UTC",
    expiration.year,
    expiration.mon,
    expiration.day,
    expiration.hour,
    expiration.min,
    expiration.sec
  );

  mbedtls_x509_crt_free(&certificate);
  return String(expirationText);
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

#ifndef REDUCED_CONFIG
//Tell the config frontend we are disconnected.
config.updateInformation("Network", "state", "[time]: Attempting to reconnect...");
#endif

  //First, figure out if we should be using ethernet or wifi

  bool useEthernet = false;

  #if CORE_HAS_ETHERNET
  if(ETH.hasIP()){
    networkState.transport = NetworkState::Transport::Ethernet;
    Serial.println(F("Using Ethernet connection."));
    useEthernet = true;
    //TODO the rest of the ethernet connect code, if any?
  }
  #endif
  
  bool wifiIssue = false;
  if(!useEthernet){
    //Using WIFI:
    networkState.transport = NetworkState::Transport::WiFi;
    //Should not need to restart the WiFi.
    //WiFi.mode(WIFI_STA);
    //WiFi.begin(networkConfiguration.wifiSsid, networkConfiguration.wifiPassword);
    if(WiFi.status() != WL_CONNECTED){
      wifiIssue = true;
      WiFi.reconnect(); //Force a manual connect attempt
      Serial.println(F("No WiFi? Waiting for reconnect"));
      unsigned long long WiFiTime = millis64() + 15000;
      while(WiFi.status() != WL_CONNECTED){
        Serial.println(".");
        delay(2000);
        if(WiFiTime <= millis64()){
          Serial.println(F("Failed to connect to WiFi! Retrying..."));
          goto retryNetwork;
        }
      }
      Serial.println(F(" WiFi Connected!"));
    } else{
      Serial.println(F("Already had WiFi connection, skipping to websocket connection."));
    }
  }

  //Next, check our websocket connection:
  bool socketIssue = false;
  bool certIssue = false;
  if(!socket.isConnected()){
    //No socket connection?
    socketIssue = true;
    socket.disconnect();
    const char* global_ca_pointer = rootCertificate.c_str();
    socket.beginSslWithCA(networkConfiguration.serverAddress.c_str(), 443, "/mqtt", global_ca_pointer, "mqtt");
    //Give the socket some time to stabilize:
    unsigned long long wsTimeout = millis64() + 5000;
    while(!socket.isConnected() && millis64() <= wsTimeout){
      socket.loop();
      delay(2);
    }
    //Did the socket work? If not, it may be a TLS cert issue.
    if(!socket.isConnected()){
      Serial.println(F("Websocket connection failed..."));
      socket.disconnect();
      //Is the server alive?
      if(!Ping.ping(networkConfiguration.serverAddress.c_str())){
        //Server is not responding?
        Serial.println(F("Cannot ping the server. No network or server unavailable?"));
        Serial.println(F("Going to retry network altogether..."));
        goto retryNetwork;
      } else{
        Serial.println(F("Server is online. Bad TLS cert?"));
        Serial.println(F("Trying TLS certs again to make sure..."));
        if(TLSRetryCount <=5){
          Serial.print(F("That was attempt: "));
          Serial.print(TLSRetryCount);
          Serial.println(F("/5 attempts before we get new certs."));
          delay(1000);
          TLSRetryCount++;
          goto retryNetwork;
        }
        //The issue is probably the certs?
        socketIssue = false;
        certIssue = true;
        Serial.println(F("Getting new TLS certs from server."));
        if(getTLSCert()){
          //Got new TLS Certs!
          Serial.println(F("Successfully loaded new TLS certs, continuing network connection..."));
          TLSRetryCount = 0;
          goto retryNetwork;
        } else{
          //Failed to get TLS cert?
          Serial.println(F("Failed to get TLS cert, retrying network altogether..."));
          goto retryNetwork;
        }
      }
    } //If not this, the connection worked and we can continue.
    socket.setReconnectInterval(2000); //Attempt to reconnect every 2 seconds if we lose connection
    Serial.println(F("Websocket Connected."));
  } else{
    Serial.println(F("Websocket OK."));
  }

  //Lastly, let's check out mqtt connection:
  Serial.println(F("Connecting MQTT..."));
  bool mqttIssue = false;
  if(!mqtt.isConnected()){
    //MQTT is not connected. Reconnect.
    mqttIssue = true;
    unsigned long long mqttTime = millis64() + 15000;
    while(!mqtt.connect(serialNumber, serialNumber, networkConfiguration.mqttKey)){ //Use serial number as unique ID, username, and key as password.
      Serial.println(".");
      socket.loop();
      delay(2000);
      if(mqttTime <= millis64()){
        Serial.println(F("Failed to connect to mqtt broker! Retrying network altogether..."));
        goto retryNetwork;
      }
    } 
    Serial.println(F(" MQTT Connected!"));
  } else{
    Serial.println(F("MQTT was already connected."));
  }

  //Let's figure out why we had to do a reconnect.
  //wifiIssue, socketIssue, certIssue, mqttIssue
  String connectBlame;
  if(wifiIssue){
    //Issue was caused by wifi
    connectBlame = "WiFi";
  } else if(certIssue){
    //Issue was caused by cert
    connectBlame = "TLS Cert";
  } else if(socketIssue){
    //Issue was caused by socket
    connectBlame = "Websocket";
  } else if(mqttIssue){
    //Issue was caused by mqtt
    connectBlame = "MQTT";
  } else{
    //Unknown issue?
    connectBlame = "Unknown";
  }

  //Subscribe to all MQTT topics relevant to us;
  mqttState.baseTopic = "makerspace/device/" + serialNumber;
  String SubAuth = mqttState.baseTopic + "/authTo/response";
  mqtt.subscribe(SubAuth, 2, [](const String& payload, const size_t size) {
    Serial.print(F("AuthTo Response: "));
    Serial.println(payload);
    mqttState.authResponse = payload;
    mqttState.newAuth = true;
    resetKeepAliveTimer();
      
  });
  String SubInfo = mqttState.baseTopic + "/info/response";
  mqtt.subscribe(SubInfo, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Info Response: "));
    Serial.println(payload);
    mqttState.infoResponse = payload;
    mqttState.newInfo = true;
    resetKeepAliveTimer();
  });
  String SubCommand = mqttState.baseTopic + "/command";
  mqtt.subscribe(SubCommand, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Command Input: "));
    Serial.println(payload);
    mqttState.commandResponse = payload;
    mqttState.newCommand = true;
    resetKeepAliveTimer();
  });
  String SubWelcome = mqttState.baseTopic + "/welcome/response";
  mqtt.subscribe(SubWelcome, 2, [](const String& payload, const size_t size) {
    Serial.print(F("Welcome Response: "));
    Serial.println(payload);
    mqttState.welcomeResponse = payload;
    mqttState.newWelcome = true;
    resetKeepAliveTimer();
  });
  String SubPing = mqttState.baseTopic + "/ping";
  mqtt.subscribe(SubPing, 2, [](const String& payload, const size_t size) {
    //Serial.println(F("Ping Loopback."));
    resetKeepAliveTimer();
  });

  //We should request and report things when we (re)connect
  mqttState.reportConfig = true;
  mqttState.requestInfo = true;
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
    NetConnect["ip"] = WiFi.localIP().toString();
    //NEW: Add the disconnect reasons
    if(lastDisconnectReason != 0 && connectBlame == "WiFi"){
      //If 0, we just reconnected no need to send this.
      //We also do not need to send unless the disconnect reason was the Wifi.
      NetConnect["disconnectReason"] = lastDisconnectReason;
      NetConnect["disconnectReasonString"] = disconnectReasonToString(lastDisconnectReason);
    }
  }
  lastDisconnectReason = 0; //Reset the reason
  NetConnect["sys_uptime"] = millis64() / 1000;
  NetConnect["net_uptime"] = (millis64() - lastReconnectTime) / 1000;
  lastReconnectTime = millis64();
  //What went wrong that resulted in using having to do a reconnect?
  NetConnect["connectBlame"] = connectBlame;
  String netPayload;
  serializeJson(NetConnect, netPayload);
  //If there is another log pending to send, do not overwrite it;
  if(!mqttState.logToSend){
    mqttState.logType = "network-info";
    mqttState.logMessage = netPayload;
    mqttState.logToSend = true;
  }
  //Send a ping on connect:
  String PingTopic = mqttState.baseTopic + "/ping";
  publishMqttstatusMessage(PingTopic, "Ping!");
  resetKeepAliveTimer();
  Serial.println(F("Network connected."));

  #ifndef REDUCED_CONFIG
  //Update the config frontend with our network information now that we are connected.
  config.updateInformation("Network", "state", "Connected at [time]");
  config.updateInformation("Network", "local-ip", WiFi.localIP().toString());
  String reasonConfig = "Unknown";
  if(connectBlame == "WiFi"){
    //Blame the wifi with additional info;
    reasonConfig = "WiFi: " + disconnectReasonToString(lastDisconnectReason);
  } else{
    reasonConfig = connectBlame;
  }
  config.updateInformation("Network", "disconnect-reason", reasonConfig);
  config.updateInformation("WiFi", "bssid", WiFi.BSSIDstr());
  config.updateInformation("WiFi", "wifi-channel", String(WiFi.channel()));
  #endif
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

String disconnectReasonToString(uint8_t reason) {
  switch (reason) {
    case 0: return "STARTUP";
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "DISASSOC_DUE_TO_INACTIVITY";
    case 5: return "ASSOC_TOOMANY";
    case 6: return "CLASS2_FRAME_FROM_NONAUTH_STA";
    case 7: return "CLASS3_FRAME_FROM_NONASSOC_STA";
    case 8: return "ASSOC_LEAVE";
    case 9: return "ASSOC_NOT_AUTHED";
    case 10: return "DISASSOC_PWRCAP_BAD";
    case 11: return "DISASSOC_SUPCHAN_BAD";
    case 12: return "BSS_TRANSITION_DISASSOC";
    case 13: return "IE_INVALID";
    case 14: return "MIC_FAILURE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 17: return "IE_IN_4WAY_DIFFERS";
    case 18: return "GROUP_CIPHER_INVALID";
    case 19: return "PAIRWISE_CIPHER_INVALID";
    case 20: return "AKMP_INVALID";
    case 21: return "UNSUPP_RSN_IE_VERSION";
    case 22: return "INVALID_RSN_IE_CAP";
    case 23: return "802_1X_AUTH_FAILED";
    case 24: return "CIPHER_SUITE_REJECTED";
    case 25: return "TDLS_PEER_UNREACHABLE";
    case 26: return "TDLS_UNSPECIFIED";
    case 27: return "SSP_REQUESTED_DISASSOC";
    case 28: return "NO_SSP_ROAMING_AGREEMENT";
    case 29: return "BAD_CIPHER_OR_AKM";
    case 30: return "NOT_AUTHORIZED_THIS_LOCATION";
    case 31: return "SERVICE_CHANGE_PRECLUDES_TS";
    case 32: return "UNSPECIFIED_QOS_REASON";
    case 33: return "NOT_ENOUGH_BANDWIDTH";
    case 34: return "DISASSOC_LOW_ACK";
    case 35: return "EXCEEDED_TXOP";
    case 36: return "STA_LEAVING";
    case 37: return "END_TS_BA_DLS";
    case 38: return "UNKNOWN_TS_BA";
    case 39: return "TIMEOUT";
    case 46: return "PEERKEY_MISMATCH";
    case 47: return "AUTHORIZED_ACCESS_LIMIT_REACHED";
    case 48: return "UNKNOWN_BSS_TRANSITION_MANAGEMENT_PARAM";
    case 49: return "INVALID_PMKID";
    case 50: return "INVALID_MDE";
    case 51: return "INVALID_FTE";
    case 67: return "TRANSMISSION_LINK_ESTABLISH_FAILED";
    case 68: return "ALTERATIVE_CHANNEL_OCCUPIED";

    //Special case; reports when a disconnect was not Wifi based.
    case 99: return "NOMINAL_WIFI";

    // ESP32 Specific Error Codes
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    case 206: return "AP_TSF_RESET";
    case 207: return "ROAMING";
    case 208: return "ASSOC_COMEBACK_TIME_TOO_LONG";
    case 209: return "SA_QUERY_TIMEOUT";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    
    default: return String("UNKNOWN_") + String(reason);
  }
}

void resetKeepAliveTimer(){
  //Simple function that defers the time to send a ping, called when we get an MQTT payload.
  keepAlivePing.nextTime = millis64() + keepAlivePing.gapTime;
  keepAlivePing.missedPing = false; //We got something, so clear the missed ping flag.
  keepAlivePing.pingPending = false; //We got something, so clear the missed ping flag.
  networkState.unavailable = false; //We got something, so we must have a network connection.
}

bool getTLSCert(){
  //Gets new TLS certs from the server.
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
      return false;
    }
    delay(10); 
    networkState.unavailable = true;
    return false;
  }

  if(networkState.unavailable == true){
    return false;
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
      #ifndef REDUCED_CONFIG
      //Also update the config values
      config.updateCommand("TLS", "cert-name", getCertificateCommonName());
      config.updateCommand("TLS", "cert-expiration", getCertificateExpiration());
      #endif
      return true;
    } else{
      networkState.unavailable = true;
      Serial.println(F("Unknown error, could not write new cert to file?"));
      return false;
    }
  } else{
    //The hashes did not match, potental attack in progress!
    networkState.unavailable = true;
    faultReason = "TLS hash does not match!";
    Serial.println(F("CRITICAL ERROR: ATTEMPT WAS MADE TO LOAD BAD TLS CERTS!"));
    //statusMessage = "Attmpted to load cert with bad hash?";
    //messageToSend = true;
    delay(1000);
    return false;
  }
  return false; //We shouldn't ever make it here?
}

extern "C" bool verifyRollbackLater() {
  return true;
}
