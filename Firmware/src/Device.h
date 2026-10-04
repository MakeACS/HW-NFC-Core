#pragma once
#include <Arduino.h>

struct Device {
  byte address[8];          //OneWire ROM address
  byte deviceMode;          //Provisioned mode from scratchpad bits 5-3 of byte 3
  uint32_t deviceID;        //19-bit ID reconstructed from scratchpad bytes 3, 6, and 7
  byte highTempLimit;       //Temperature threshold stored in scratchpad byte 2
  float currentTemp;
  bool isAlarming;
  bool isOnline;
};