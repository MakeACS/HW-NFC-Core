/* 


Screen Controller
This task is responsible for talking with any connected screen.


*/

void sendCurrent(bool sendTime = false){
  //Sends the common regular information the screen needs
  JsonDocument CurrentStates;
  if(sendTime){
    //Send the current time
    CurrentStates["time"] = rtc.getEpoch();
  }
  if(State == "WELCOMING"){
    CurrentStates["welcoming"] = true;
  } else{
    CurrentStates["welcoming"] = false;
  }

  CurrentStates["noNetwork"] = NoNetwork;
  CurrentStates["state"] = State;
  CurrentStates["channels"] = 1; //Always set to 1 for now. 
  CurrentStates["mode"] = InputMode;
  CurrentStates["denied"] = AccessDenied;
  CurrentStates["deniedReason"] = AuthReason;
  CurrentStates["faultMessage"] = FaultReason;
  CurrentStates["startupMessage"] = ""; //Should be no startup message by the time we make it here.
  CurrentStates["identify"] = Identify;
  CurrentStates["url"] = Server;
  CurrentStates["setRotation"] = getTargetRotation();
  String CurrentToSend;
  serializeJson(CurrentStates, CurrentToSend);
  Serial0.println(CurrentToSend);
  Serial0.flush();
}

void SceenController(void *pvParameters){
  
  unsigned long long NextScreenUpdate;


  //One time only; need to ask for all the info about the screen
  //TODO

  //Send our current information
  sendCurrent(true);
  delay(100);

  //Before we start regular operation, exit out of the startup screen;
  sendStartup("");

  while(1){

    delay(20);

    //Check if it is time for a regular update of the screen
    if(NextScreenUpdate <= millis64()){
      UpdateScreen = true;
    }

    if(UpdateScreen){
      NextScreenUpdate = millis64() + 1000;
      sendCurrent();
      UpdateScreen = false;
    }
  }
}

int getTargetRotation() {
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