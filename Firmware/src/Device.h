#pragma once
#include <Arduino.h>

struct Device {
  byte address[8];
  byte deviceMode;         
  uint32_t deviceID;       
  byte highTempLimit;      
  float currentTemp;       
  bool isAlarming;         
  bool isOnline;           
};