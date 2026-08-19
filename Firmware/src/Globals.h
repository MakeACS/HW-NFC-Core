#pragma once

#include <Arduino.h>

#ifndef CORE_VARIANT_HEADER
#define CORE_VARIANT_HEADER "variants/CoreV3_0_0.h"
#endif

#include CORE_VARIANT_HEADER

#include <OneWire.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#if CORE_HAS_ETHERNET
#include <ETH.h>
#endif
#include "esp_timer.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#endif
#include <nvs_flash.h>
#include <ESP32Time.h>
#include <WebSocketsClient.h>
#include <ESP32Ping.h>
#include <ping.h>
#include "esp_ota_ops.h"
#include <MQTTPubSubClient.h>
#include <SPI.h>
#if CORE_NFC_READER_MFRC630
#include <mfrc630.h>
#endif
#if CORE_HAS_LOCAL_AUDIO_VISUAL
#include <Adafruit_NeoPixel.h>
#endif
#if CORE_NFC_READER_PN532
#include <Adafruit_PN532.h>
#endif
#include "USB.h"
#include <esp_system.h>
#include <esp_mac.h>
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#if CORE_HAS_ACCELEROMETER
#include "SparkFun_LIS2DH12.h"
#endif
#include <Wire.h>
#include <mbedtls/md.h>
#include <stdio.h>
#include <esp_crc.h>
#include "ESP32OTAPullSecure.h"

#include "Device.h"

#define STATUS_INTERVAL 15000

struct NetworkState {
	bool unavailable = true;
	enum class Transport { None, WiFi, Ethernet } transport = Transport::None;
};

struct NetworkConfiguration {
	String wifiPassword;
	String wifiSsid;
	String serverAddress;
	String mqttKey;
};

struct MqttState {
	String authResponse;
	bool newAuth = false;
	String infoResponse;
	bool newInfo = false;
	String commandResponse;
	bool newCommand = false;
	bool newPing = true;
	bool sendPing = false;
	unsigned long long nextPingTime = 0;
	String welcomeResponse;
	bool newWelcome = false;
	bool welcomingPending = false;
	String baseTopic;
	String statusMessage;
	bool messageToSend = false;
	bool logToSend = false;
	String logMessage;
	String logType;
	bool sendAuth = false;
	bool stateChange = false;
	bool reportConfig = false;
	bool requestInfo = false;
	bool sendStatus = false;
	bool sendWelcome = false;
};

struct SystemState {
	bool requestReset = false;
	String resetReason;
	bool scheduledRestart = false;
	unsigned long long scheduledRestartTime = 0;
	bool imminentShutdown = false;
	unsigned long long nextStatusTime = 0;
};

struct ChannelState {
	static constexpr byte kMaximumChannels = 4;

	byte count = 0;
	bool access[kMaximumChannels] = {false, false, false, false};
	String states[kMaximumChannels] = {"UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN"};
	String lastStates[kMaximumChannels] = {"UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN"};
	String changeReasons[kMaximumChannels];
	String authorizationReasons[kMaximumChannels];
	String reportedStates[kMaximumChannels] = {"UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN"};
	unsigned long tapDurations[kMaximumChannels] = {0, 0, 0, 0};
	unsigned long long tapExpirationTimes[kMaximumChannels] = {0, 0, 0, 0};
	volatile unsigned long hobbsSeconds[kMaximumChannels] = {0, 0, 0, 0};
};

// Forward declarations shared across translation units.
String readNfcCardId();
bool anyChannelMatcheschannelState(String targetState);

void sendDisplaychannelState(bool sendRarely = false, bool sendFrequently = true);
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
void publishMqttstatusMessage(String topic, String payload);
void mfrc630_SPI_transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
void mfrc630_SPI_select();
void mfrc630_SPI_unselect();
String getBaseMacAddress();
void sendStartupstatusMessage(String message);
String calculateSha256(String input);
void IRAM_ATTR updateHobbsCounter(void* arg);

// Shared objects.
extern Preferences settings;
extern JsonDocument ConfigJson;
extern ESP32OTAPull ota;
extern ESP32Time rtc;
extern WebSocketsClient socket;
extern MQTTPubSub::PubSubClient<1536> mqtt;
extern OneWire ds;
#if CORE_NFC_READER_PN532
extern Adafruit_PN532 nfc;
#endif
#if CORE_HAS_LOCAL_AUDIO_VISUAL
extern Adafruit_NeoPixel CBI;
#endif
#if CORE_HAS_ACCELEROMETER
extern SPARKFUN_LIS2DH12 accel;
#endif
extern NetworkClientSecure networkclient;
#if !CORE_HAS_LOCAL_AUDIO_VISUAL
extern HardwareSerial frontend;
extern bool frontendButtonPressed;
extern bool frontendCardDetect1;
extern bool frontendCardDetect2;
#endif

extern String rootCertificate;
extern bool gamerMode;
extern SystemState systemState;
extern bool accessEnabled;
extern String currentUserUid;
extern String detectedUid;
extern bool faultBeepRequested;
extern int MakerspaceNumber;
extern String hardwareVersion;
extern bool identifyRequested;
extern String inputMode;
extern String defaultInputMode;
extern bool pendingApproval;
extern bool accessDenied;
extern bool cardPresent;
extern bool lockWhenIdle;
extern bool restartWhenUnused;
extern bool welcomeMode;
extern NetworkState networkState;
extern String tapUid;
extern bool userWelcomed;
extern String serialNumber;
extern NetworkConfiguration networkConfiguration;
extern int makerspaceId;
extern MqttState mqttState;
extern Device sensorList[10];
extern bool configOneWire;
extern bool sealBroken;
extern bool reSealBus;
extern bool overTemp;
extern byte liveAddresses[5][8];
extern int liveAddressCount;
extern byte deviceCount;
extern byte redLed;
extern byte greenLed;
extern byte blueLed;
extern unsigned int buzzerTone;
extern bool resetLed;
extern bool unlockedBeep;
extern bool singleBeep;
extern ChannelState channels;
extern String interruptResponse;
extern bool isInterrupted;
extern byte interruptCount;
extern bool updateScreen;
extern String faultReason;
extern String hmiMachineNames[4];
extern String hmiMakerspace;
extern String hmiDeviceName;
extern String hmiRole;
extern String stationName;

