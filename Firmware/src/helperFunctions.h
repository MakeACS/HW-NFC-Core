#pragma once

#include <Arduino.h>

extern "C" bool verifyRollbackLater();

void handleOtaProgress(int offset, int totalLength);
const char *getOtaErrorText(int code);
uint64_t millis64();
void connectNetwork();
void resetKeepAliveTimer();
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
void migrateLegacySettings();
bool getTLSCert();
String getCertificateCommonName();
String getCertificateExpiration();
