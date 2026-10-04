#include "Globals.h"
#include "SystemSupervisor.h"
#include "AccessManager.h"


void runSystemSupervisorLoop(void *pvParameters){
  uint64_t networkUnavailableSince = 0;
  bool reportedUnavailable = false;
  static AccessSnapshot snap;
  Serial.println(F("[supervisor] Started."));
  while(1){
    vTaskDelay(pdMS_TO_TICKS(50));
    accessGetSnapshot(snap);
    const bool inUse = snap.deviceInUse;

    //Let the AccessManager know about network changes (retry next pass if its inbox is full)
    if(networkState.unavailable != reportedUnavailable){
      AccessEvent e;
      e.type = AccessEventType::NetworkChanged;
      e.flag = !networkState.unavailable;
      if(accessPost(e)){
        reportedUnavailable = networkState.unavailable;
      }
    }

    if(networkState.unavailable){
      if(networkUnavailableSince == 0){
        networkUnavailableSince = millis64();
      } else if(millis64() - networkUnavailableSince >= 60000 && !inUse){
        systemState.resetReason = "Network unavailable for more than 60 seconds";
        systemState.requestReset = true;
      }
    } else{
      networkUnavailableSince = 0;
    }

    if(systemState.nextStatusTime <= millis64()){
      mqttState.sendStatus = true;
      systemState.nextStatusTime = millis64() + STATUS_INTERVAL;
    }

    if(snap.restartWhenUnused && !inUse){
      Serial.println(F("Executing restart-when-unused flag."));
      Serial.flush();
      delay(5);
      systemState.requestReset = true;
      while(1){
        delay(100);
      }
    }

    if(systemState.scheduledRestart){
      const bool expired = systemState.scheduledRestartTime != 0 && systemState.scheduledRestartTime <= millis64();
      if(inUse && !expired){
        //Someone is using the machine; let them know we are restarting soon.
        systemState.imminentShutdown = true;
        if(systemState.scheduledRestartTime == 0){
          Serial.println(F("serverAddress commanded scheduled shutdown, but user present? Giving them 60 seconds."));
          systemState.scheduledRestartTime = millis64() + 60000;
        }
      } else{
        Serial.println(expired ? F("User has had 60 seconds to end session, forcing scheduled restart.") : F("Executing scheduled restart."));
        Serial.flush();
        systemState.requestReset = true;
        while(1){
          delay(100);
        }
      }
    }
  }
}

