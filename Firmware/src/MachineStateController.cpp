#include "Globals.h"
#include "MachineStateController.h"
#include "TaggedSerial.h"

namespace {
TaggedSerial<decltype(::Serial)> machineStateSerial(::Serial, "[state] ");
}

#define Serial machineStateSerial

void runMachineStateLoop(void *pvParameters){
  Serial.println(F("runMachineStateLoop Started."));
  bool retryPing = false; //Stores if we have failed 2 pings in a row.
  while(1){
    delay(50);


    //Temp disable, false positives
    /*
    //Step 1.1: Check for any reason we should be in a fault state
    if(overTemp || sealBroken){
      //Check if every channel is in "FAULT";
      byte faultCount = 0;
      for( int i = 0; i < channels.count; i++){
        if(channels.states[i] == "FAULT"){
          faultCount++;
        }
      }
      if(channels.count != faultCount){
        //This is our first time going to the fault state
        String SetFaultReason;
        SetFaultReason = "FAULT";
        mqttState.statusMessage = "ACS Fault!";
        faultReason = "ACS Fault!";
        if(overTemp){
          SetFaultReason = "OVER_TEMP";
          mqttState.statusMessage = "Overtemperature!";
          faultReason = "Overtemperature!";
        }
        if(sealBroken){
          SetFaultReason = "INTEGRITY_FAIL";
          mqttState.statusMessage = "Bus Integrity Broken!";
          faultReason = "Bus Integrity!";
        }
        for (int i = 0; i < channels.count; i++) {
          channels.states[i] = "FAULT";
          channels.changeReasons[i] = SetFaultReason;
        }
        mqttState.messageToSend = true;
        updateScreen = true;
      }
    }

    */

    //Interrupt Manager:
    if(!isInterrupted){
      //Check if the interrupt is low;
      if(!digitalRead(PIN_INTERRUPT)){
        interruptCount++;
      } else{
        //Not interrupted and not reading interrupt, set the counter back to 0.
        interruptCount = 0;
      }
      if(interruptCount >= 5){
        //We had multiple interrupts in a row, so we must be in an interrupt state!
        Serial.println(F("Interrupt Triggered!"));
        isInterrupted = true;
        //Actually execute on the interrupt state;
        if(interruptResponse == "MESSAGE"){
          //Send a message
          mqttState.statusMessage = "Interrupt Triggered!";
          mqttState.messageToSend = true;
        }
        if(interruptResponse == "LOCK_TEMP"){
          //Temporarily lock the channels;
          for(int i = 0; i < channels.count; i++){
            if(channels.states[i] == "UNLOCKED" || channels.states[i] == "ALWAYS_ON" || channels.states[i] == "IDLE"){
              channels.states[i] = "LOCKED_OUT";
              channels.changeReasons[i] = "LOCK_TEMP";
              singleBeep = true;
            }
          }
        }
        if(interruptResponse == "IDLE"){
          //Idle any unlocked or Always-On channel:
          for(int i = 0; i < channels.count; i++){
            if(channels.states[i] == "UNLOCKED" || channels.states[i] == "ALWAYS_ON"){
              channels.states[i] = "IDLE";
              channels.changeReasons[i] = "LOCAL";
              singleBeep = true;
            }
          }
        }
        if(interruptResponse == "FAULT"){
          //Fault all channels.
          for(int i = 0; i < channels.count; i++){
            channels.states[i] = "FAULT";
            channels.changeReasons[i] = "FAULT";
          }
          faultReason = "Interrupt Asserted!";
        }
        updateScreen = true; //tell the frontend ASAP
      }
    } else{
      //Check if we are out of the interrupt state;
      if(digitalRead(PIN_INTERRUPT)){
        interruptCount--;
        if(interruptCount <= 2){
          //We had multiple interrupts missed in a row, so we must no longer be in an interrupt state.
          isInterrupted = false;
          Serial.println(F("De-asserted Interrupt."));
          interruptCount = 0;
          if(interruptResponse == "LOCK_TEMP"){
            //Now that the interrupt is clear, release the locks on channels
            for(int i = 0; i < channels.count; i++){
              if(channels.states[i] == "LOCKED_OUT"){
                channels.states[i] = "IDLE";
                channels.changeReasons[i] = "LOCAL";
                singleBeep = true;
              }
            }
          }
        }
      }
    }

    //IF we are in welcoming mode, the input mode is always TEMP_PRESENT and there are no access channels.
    if(welcomeMode){
      inputMode = "TEMP_PRESENT";
      channels.count = 0;
    } else{
      //Run one-time cleanup to get out of welcomeMode:
      if(inputMode != defaultInputMode){
        inputMode = defaultInputMode;
        //Re-load the channel count we have stored in memory;
        channels.count = settings.getString("channels.count").toInt();
        //Set all channels to UNKNONWN
        for(int i = 0; i < channels.count; i++){
          channels.states[i] = "UNKNOWN";
          channels.lastStates[i] = "UNKNOWN";
          channels.changeReasons[i] = "LOCAL";
          channels.authorizationReasons[i] = "LOCAL";
          channels.reportedStates[i] = "UNKNOWN";
        }
        //We should immediately request new information from the server, since we are no longer in welcome mode.
        mqttState.requestInfo = true;
      }

    }
    //Some random cleanup, none of these should be set if there isn't a card present
    if(!cardPresent){
      mqttState.welcomingPending = false;
      accessDenied = false;
      pendingApproval = false;
      pendingApproval = false;
    }

    //See if we have a regular status update to send
    if(systemState.nextStatusTime <= millis64()){
      //Time to send a status message.
      mqttState.sendStatus = true;
      systemState.nextStatusTime = millis64() + STATUS_INTERVAL;
    }

    //Then, what state are we in? Tap? Insert? 
      //If we are in INSERT mode, we look at the switches before determining if we are looking for a card.
      //If we are in TEMP_PRESENT mode, we look for a card no matter what.

    if(inputMode == "TEMP_PRESENT"){
      detectedUid = readNfcCardId();
      if(detectedUid == ""){
        //Double check there really isn't a card present
        detectedUid = readNfcCardId();
      }
      if(!cardPresent && detectedUid.length() > 2){
        //Accept the current card as the actual card.
        cardPresent = true;
        currentUserUid = detectedUid;

        //What do we do with the card? 

        //If there is no network, we cannot do anything so we should just deny the user and beep.
        if(networkState.unavailable){
          faultBeepRequested = true;
          accessDenied = true;
          for(int i = 0; i < channels.count; i++){
            channels.authorizationReasons[i] = "No network, try again soon or talk to staff.";
          }
          Serial.println(F("accessEnabled denied due to no network!"));
        } else{
          //We have a network connection. Let's handle the user's card.
          if(welcomeMode){
            //Let's welcome the user to the makerspace
            mqttState.sendWelcome = true;
            mqttState.welcomingPending = true;
          } else{
            //We are not in welcome mode, so we should check if the user is authorized to use the machine.
            if((anyChannelMatcheschannelState("IDLE") || anyChannelMatcheschannelState("UNLOCKED"))){
              //There is a channel that is idle or unlocked, so we should ask the server if this user can auth them.
              pendingApproval = true;
              mqttState.sendAuth = true;
            } else if(!anyChannelMatcheschannelState("IDLE") && (!anyChannelMatcheschannelState("UNLOCKED") || anyChannelMatcheschannelState("ALWAYS_ON"))){
              //Logic: If there are no channels in a state that the user could unlock, but something is already unlocked or always on, beep to confirm.
              singleBeep = true;
            } else{
              //Auto-deny the user, likely all locked or in a fault state?
              accessDenied = true;
              for(int i = 0; i < channels.count; i++){
                channels.authorizationReasons[i] = "Incorrect state, machine must be in \"IDLE\" mode to activate.";
              }
              Serial.println(F("Auto-denied due to bad state."));
            }
          }
        }
      }
    } else{ //INSERT
    #if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
      if(!cardPresent && !digitalRead(PIN_DET_1) && !digitalRead(PIN_DET_2)){
    #else
      if(!cardPresent && frontendCardDetect1 && frontendCardDetect2){
    #endif
        //New card inserted!
        cardPresent = true;
        bool cardRead = false;
        //Read the card:
        detectedUid = readNfcCardId();
        if(detectedUid == ""){
          //Double check there really isn't a card present
          detectedUid = readNfcCardId();
        }
        if(detectedUid.length() > 2){
          //Accept the current card as the actual card.
          currentUserUid = detectedUid;
          cardRead = true;
        } else{
          //We have a card present, but we cannot read it. This is likely a bad card or a bad read. 
          //We should deny the user and beep.
          accessDenied = true;
          for(int i = 0; i < channels.count; i++){
            channels.authorizationReasons[i] = "Card could not be read, try again or talk to staff.";
          }
          Serial.println(F("Access denied due to unreadable card!"));
        }
        //Only do the rest if a card was actually read
        if(cardRead && anyChannelMatcheschannelState("IDLE") && !networkState.unavailable){
          //Let's check for auth with the server
          pendingApproval = true;
          mqttState.sendAuth = true;
        } else if(cardRead && !anyChannelMatcheschannelState("IDLE") && (anyChannelMatcheschannelState("UNLOCKED") || anyChannelMatcheschannelState("ALWAYS_ON"))){
          //Logic: If there are no channels in IDLE that the user could unlock, but something is already unlocked or always on, beep to confirm.
          singleBeep = true;
        } else if(cardRead && anyChannelMatcheschannelState("IDLE") && networkState.unavailable){
          //Fault beep and deny the user due to no network
          faultBeepRequested = true;
          accessDenied = true;
          for(int i = 0; i < channels.count; i++){
            channels.authorizationReasons[i] = "No network, try again soon or talk to staff.";
          }
          Serial.println(F("accessEnabled denied due to no network!"));
        } else if(cardRead){
          //Auto-deny the user, likely all locked or in a fault state?
          accessDenied = true;
          for(int i = 0; i < channels.count; i++){
            channels.authorizationReasons[i] = "Incorrect state, machine must be in \"IDLE\" mode to activate.";
          }
          Serial.println(F("Auto-denied due to bad state."));
        }
      }
    }
  
    //Was a card that was present removed?

    //In TEMP_PRESENT mode, we do this based on the card no longer being detected by currentUserUid.
    if(inputMode == "TEMP_PRESENT"){
      if(cardPresent && !detectedUid.equalsIgnoreCase(currentUserUid)){
        //Either found no currentUserUid or currentUserUid we found is different
        Serial.print(F("Card "));
        Serial.print(currentUserUid);
        Serial.print(F(" replaced with "));
        Serial.println(detectedUid);
        cardPresent = false;
        currentUserUid = "";
        mqttState.sendWelcome = false;
        mqttState.welcomingPending = false;
        userWelcomed = 0;
        accessDenied = 0;
      }
    } else{ //INSERT
      //In INSERT mode, we detect this based on switches
    #if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
      if(cardPresent && (digitalRead(PIN_DET_1) || digitalRead(PIN_DET_2))){
    #else
      if(cardPresent && (!frontendCardDetect1 || !frontendCardDetect2)){
    #endif
        //Reset everything to normal.
        cardPresent = false;
        currentUserUid = "";
        pendingApproval = false;
        accessDenied = false;
        for(int i = 0; i < channels.count; i++){
          if(channels.states[i] == "UNLOCKED"){
            channels.states[i] = "IDLE";
            channels.changeReasons[i] = "CARD_REMOVED";
          }
        }
      }
    }

    //Handle access expiration for channels if in TEMP_PRESENT mode:
    if(inputMode == "TEMP_PRESENT"){
      for(int i = 0; i < channels.count; i++){
        if(channels.tapExpirationTimes[i] <= millis64()){
          if(channels.states[i] == "UNLOCKED"){
            channels.states[i] = "IDLE";
            channels.changeReasons[i] = "CARD_REMOVED";
            singleBeep = true;
          }
          channels.tapExpirationTimes[i] = 0; //Cleanup
        }
      }
    } else{
      //If we are not in TEMP_PRESENT, then all channels.tapExpirationTimes should be 0.
      for(int i = 0; i < channels.count; i++){
        channels.tapExpirationTimes[i] = 0;
      }
    }

    //Step 1.3: Check if the states have changed since last time we went through the loop.
    //Check that channels.access values are right, and set them properly.
    bool AccessOn = false;
    bool TellUpdateScreen = false;
    for(int i = 0; i < channels.count; i++){
      if(channels.states[i] == "UNLOCKED" || channels.states[i] == "ALWAYS_ON"){
        if(channels.access[i] != 1){
          TellUpdateScreen = true;
        }
        channels.access[i] = 1;
      #if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
        digitalWrite(PIN_ACCESS, HIGH);
      #else
        frontend.println("S 1");
      #endif
        AccessOn = true;
      } else{
        if(channels.access[i] != 0){
          TellUpdateScreen = true;
        }
        channels.access[i] = 0;
      }
      //While we are here, check that there is a valid state change reason for everyone. 
      if(channels.changeReasons[i] == ""){
        channels.changeReasons[i] = "UNKNOWN";
      }
    }
    if(AccessOn == false){
      //No channels are on, disable accessEnabled
    #if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
      digitalWrite(PIN_ACCESS, LOW);
    #else
      frontend.println("S 0");
    #endif
    }
    if(TellUpdateScreen){
      //A channels.access state changed, update the screen
      updateScreen = true;
    }
    //Set the GPIO of the bus based on channels.access
  #if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
  if(channels.count > 1){
    //We only need to set the GPIOs if we have more than 1 channel, otherwise we just use PIN_ACCESS for all channels.
    for(int i = 0; i < channels.count; i++){
      if(channels.access[i]){
        digitalWrite(PIN_GPIO_1 + i, HIGH);
      } else{
        digitalWrite(PIN_GPIO_1 + i, LOW);
      }
    }
  }
  #endif

    //See if any of the channels changed state to report to the server;
    bool SendStateChange = false;
    for(int i = 0; i < channels.count; i++){
      if(channels.states[i] != channels.lastStates[i]){
        if(channels.lastStates[i] != "UNKNOWN"){
          //The state changed for something other than setting back from unkown, we should send it.
          SendStateChange = true;
        }
        channels.lastStates[i] = channels.states[i]; //Override channels.lastStates with channels.states
      }
    }
    if(SendStateChange){
      mqttState.stateChange = true;
    }

    //Step 1.4: Check for and execute any flags;
    if(lockWhenIdle && !anyChannelMatcheschannelState("UNLOCKED") && !anyChannelMatcheschannelState("ALWAYS_ON")){
      //If no channel is unlocked or always on, lock any idle channels.
      for(int i = 0; i < channels.count; i++){
        if(channels.states[i] == "IDLE"){
          channels.states[i] = "LOCKED_OUT";
          channels.changeReasons[i] = "COMMANDED";
        }
        singleBeep = true; //Beep to let the user know we are locking the machine.
      }
      lockWhenIdle = 0;
    }
    if(restartWhenUnused && !anyChannelMatcheschannelState("UNLOCKED") && !anyChannelMatcheschannelState("ALWAYS_ON")){
      Serial.println(F("Executing restart-when-unused flag."));
      Serial.flush();
      delay(5);
      systemState.requestReset = true;
      while(1){
        delay(100);
      }
    }
    if(systemState.scheduledRestart){
      //It is time for a scheduled restart
      if(systemState.scheduledRestartTime <= millis64() && systemState.scheduledRestartTime != 0){
        //Time to force a restart, the user has had 60 seconds to stop.
        for(int i = 0; i < channels.count; i++){
          channels.states[i] = "UNKNOWN";
        }
        Serial.println(F("User has had 60 seconds to end session, forcing scheduled restart."));
      }
      if(anyChannelMatcheschannelState("ALWAYS_ON") || anyChannelMatcheschannelState("UNLOCKED")){
        //We should not restart now, someone is using the machine? 
        //Let the user know we are restarting soon.
        systemState.imminentShutdown = true;
        if(systemState.scheduledRestartTime == 0){
          Serial.println(F("serverAddress commanded scheduled shutdown, but user present? Giving them 60 seconds."));
          systemState.scheduledRestartTime = millis64() + 60000; //Give them 60 seconds
        }
      } else{
        //Time to execute a restart.
        Serial.println(F("Executing scheduled restart."));
        Serial.flush();
        systemState.requestReset = true;
        while(1){
          delay(100);
        }
      }
    }

    //Step 1.5: Send Regular ping
    if(mqttState.nextPingTime <= millis64() && !networkState.unavailable){
      //It is time to send a new ping
      mqttState.nextPingTime = millis64() + 1000;
      if(mqttState.newPing){
        //We got a ping response as expected.
        mqttState.newPing = false;
        retryPing = false;
        //Serial.println(F("Ping response OK."));
      } else{
        Serial.println(F("Didn't get a ping response?"));
        if(retryPing = false){
          //We didn't get a ping response, but we haven't retried yet. Let's retry.
          Serial.println(F("Retrying ping."));
          mqttState.sendPing = true;
          retryPing = true;
        } else{
          //We didn't get a ping response, and we already retried. Something is wrong with the network.
          Serial.println(F("No ping response after retry, network is unavailable."));
          retryPing = false;
          networkState.unavailable = true;
        }
      }
      mqttState.sendPing = true;
    }
  }
}

String readNfcCardId(){
  //Let's first ask the NFC reader for the card (if one is there)
  
  String ReturnedID = "";
  
#if CORE_NFC_READER_MFRC630
  uint16_t atqa = mfrc630_iso14443a_REQA();

  if (atqa != 0) {  // Are there any cards that answered?
    uint8_t sak;
    uint8_t uid[10] = {0};  // uids are maximum of 10 bytes long.

    // Select the card and discover its uid.
    uint8_t uid_len = mfrc630_iso14443a_select(uid, &sak);
    if (uid_len != 0) {  // did we get a currentUserUid?
      for (uint8_t i=0; i<uid_len; i++){
      if (uid[i] < 16){
          ReturnedID += "0"; 
          ReturnedID += String(uid[i], HEX);
        } else {
          ReturnedID += String(uid[i], HEX);;
        }
      }
      ReturnedID.toLowerCase();
      //Serial.print(F("Found currentUserUid :"));
      //Serial.println(ReturnedID);
    } else {
      Serial.print("Could not determine currentUserUid, perhaps some cards don't play");
      Serial.print(" well with the other cards? Or too many collisions?\n");
      ReturnedID = "";
    }
  } else{
    //Did not find a currentUserUid
    //Serial.println(F("Didn't find a card."));
    ReturnedID = "";
  }
#elif CORE_NFC_READER_PN532
  //1. Check the reader is working. If not, restart it.
  byte NFCTryCount = 0;
  uint32_t versionData = nfc.getFirmwareVersion();
  while(!versionData && NFCTryCount < 3) {
    //PN532 not responding, restart it.
    digitalWrite(PIN_NFC_RST, LOW);
    delay(10);
    digitalWrite(PIN_NFC_RST, HIGH);
    delay(10);
    nfc.wakeup();
    nfc.setPassiveActivationRetries(0xFF);
    delay(10);
    NFCTryCount++;
    versionData = nfc.getFirmwareVersion();
  }
  if(NFCTryCount >= 3){
    Serial.println(F("PN532 failed to respond after 3 restart attempts. Cannot read card."));
    ReturnedID = "";
    if(!mqttState.messageToSend){
      //Send a message
      mqttState.statusMessage = "Possible malfunction of NFC reader, please check the device.";
      mqttState.messageToSend = true;
    }
  } else{
    //Do a normal card read:
    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
      for (uint8_t index = 0; index < uidLength; index++) {
        if (uid[index] < 16) {
          ReturnedID += "0";
        }
        ReturnedID += String(uid[index], HEX);
      }
      ReturnedID.toLowerCase();
    }
  }
#endif //END PN532

  if(ReturnedID.length() > 0){
    return ReturnedID;
  } else{
    //We did not find a card due to errors or no card present.
    return "";
  }
}

bool anyChannelMatcheschannelState(String targetState) {
  //Checks if any channel is in this state
  for (int i = 0; i < channels.count; i++) {
    if (channels.states[i] == targetState) {
      return true; // Found a match, exit early
    }
  }
  return false; // No matches found
}