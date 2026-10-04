/*
* These tasks handle user-facing audio/visual feedback and restart requests.
* runAudioVisualController translates access and system state into LED and buzzer
* output. watchRestartButton detects a three-second button hold and handles reset
* requests raised elsewhere in the firmware.
*/

#include "Globals.h"
#include "AudioVisualController.h"
#include "TaggedSerial.h"
#include "OfflineList.h"
#include "AccessManager.h"

namespace {
TaggedSerial<decltype(::Serial)> audioVisualSerial(::Serial, "[av] ");
}

#define Serial audioVisualSerial

uint64_t millis64();

void runAudioVisualController(void *pvParameters){
  // Animation and melody state persist between iterations of this task.
  byte LEDAnimation = 0;
  byte OldLEDAnimation = 0;
  uint64_t AnimationTime = 0;
  byte AnimationBlock = 0;
  byte Melody = 0;
  byte OldMelody = 0;
  byte DonePlaying = 0;
  byte MelodyStep = 0;
  bool tonePlaying = false;
  bool firstSingleBeepSkipped = false;
  uint64_t MelodyTime = 0;
  static AccessSnapshot snap; // Keep the relatively large snapshot off the task stack.
  Serial.println(F("runAudioVisualController Started."));
  while(1){
    vTaskDelay(20 / portTICK_PERIOD_MS);

    // Build one aggregate access state for the shared indicator: an unlocked
    // channel wins over idle/locked-out channels, but fault/unknown wins over all.
    // Access flags come from the snapshot; sound requests are drained from the queue below.
    accessGetSnapshot(snap);
    const bool welcomeMode = snap.mode == AccessMode::Welcome;
    const bool pendingApproval = snap.pendingApproval;
    const bool accessDenied = snap.accessDenied;
    const bool userWelcomed = snap.userWelcomed;
    AccessFeedback feedback;
    // Transfer every queued feedback event without blocking this periodic task.
    while(accessFeedback && xQueueReceive(accessFeedback, &feedback, 0) == pdTRUE){
      switch(feedback){
        case AccessFeedback::SingleBeep:   singleBeep = true; break;
        case AccessFeedback::UnlockedBeep: unlockedBeep = true; break;
        case AccessFeedback::FaultBeep:    faultBeepRequested = true; break;
      }
    }
    String LightState = "NOTHING";
    int highestPriority = 0;
    for (int i = 0; i < snap.channels.count; i++) {
      int currentPriority = 0;
      const ChannelState state = snap.channels.ch[i].state;
      if (state == ChannelState::Fault || state == ChannelState::Unknown) {
        currentPriority = 4; // Absolute priority.
      } else if (state == ChannelState::Unlocked || state == ChannelState::AlwaysOn) {
        currentPriority = 3; // Most permissive.
      } else if (state == ChannelState::Idle) {
        currentPriority = 2;
      } else if (state == ChannelState::LockedOut) {
        currentPriority = 1;
      }
      if (currentPriority > highestPriority) {
        // Strict comparison preserves the first channel when priorities tie.
        highestPriority = currentPriority;
        LightState = toApiString(state);
      }
    }
    if(LightState == "NOTHING"){
      LightState = "UNKNOWN";
    }

    // These checks run in priority order: later matches override earlier ones.
    // If nothing matches, the previous animation remains selected.
    // Animation IDs: 0 flashing red; 1 solid red; 2 solid green; 3 solid yellow;
    // 4 flashing yellow; 5 alternate green/blue; 6 legacy white (blue fallback);
    // 7 solid blue; 8 solid purple; 9 flashing blue; 10 RGB cycle; 11 red/green.
    if(LightState.equals("IDLE")){
      // Idle: solid yellow.
      LEDAnimation = 3;
    }
    if(welcomeMode){
      // Welcome mode currently uses solid yellow; consider a slow blink to attract attention.
      LEDAnimation = 3;
    }
    if(pendingApproval || snap.welcomingPending){
      // Pending approval or welcome: flashing yellow.
      LEDAnimation = 4;
    }
    if(LightState.equals("UNLOCKED") || LightState.equals("ALWAYS_ON") || userWelcomed){
      // Access granted: solid green.
      LEDAnimation = 2;
    }
    if((LightState.equals("UNKNOWN") && !welcomeMode) || networkState.unavailable){
      // Unknown access outside welcome mode, or a network outage: solid blue.
      LEDAnimation = 7;
    }
    if((LightState.equals("UNLOCKED") || LightState.equals("ALWAYS_ON")) && networkState.unavailable){
      // Preserve the granted state during a network outage by alternating green and blue.
      LEDAnimation = 5;
    }
    if(systemState.imminentShutdown){
      // Warn users to stop before an imminent shutdown.
      LEDAnimation = 11;
    }
    if(LightState.equals("LOCKED_OUT") || accessDenied){
      // Access denied: solid red.
      LEDAnimation = 1;
    }
    if(identifyRequested){
      // Device identification: flashing blue.
      LEDAnimation = 9;
    }
    if(resetLed){
      // Reset button held: solid purple.
      LEDAnimation = 8;
    }
    if(LightState.equals("FAULT")){
      LEDAnimation = 0;
    }
    if(gamerMode){
      LEDAnimation = 10;
    }
    // Animation IDs map to the color/pattern cases in the switch below. Reset
    // the animation clock when the selection changes so the first block is immediate.
    if(LEDAnimation != OldLEDAnimation){
      OldLEDAnimation = LEDAnimation;
      AnimationTime = 0; // Force an immediate animation update.
    } 
    if(AnimationTime <= millis64()){
      // Advance the pattern phase; network-loss alternation is deliberately slower.
      AnimationBlock++;
      if(LEDAnimation == 5){
        // Animation 5 runs at a slower speed.
        AnimationTime = millis64() + 3000;
      } else{
        AnimationTime = millis64() + 400;
      }
      // Select the RGB output for the current animation phase.
      switch(LEDAnimation){
      case 0:
        // Flash red, then turn the LED off.
        if(AnimationBlock == 1){
          redLed = 255;
          greenLed = 0;
          blueLed = 0;
        } else{
          redLed = 0;
          greenLed = 0;
          blueLed = 0;
          AnimationBlock = 0;
        }
      break;
      case 1:
        // Solid red.
        redLed = 255;
        greenLed = 0;
        blueLed = 0;
      break;
      case 2:
        // Solid green.
        redLed = 0;
        greenLed = 255;
        blueLed = 0;
      break;
      case 3:
        // Solid yellow.
        redLed = 255;
        greenLed = 255;
        blueLed = 0;
      break;
      case 4:
        // Flash yellow, then turn the LED off.
        if(AnimationBlock == 1){
          redLed = 255;
          greenLed = 255;
          blueLed = 0;
        } else{
          redLed = 0;
          greenLed = 0;
          blueLed = 0;
          AnimationBlock = 0;
        }
      break;
      case 5:
        // Alternate between green and blue.
        if(AnimationBlock == 1){
          redLed = 0;
          greenLed = 255;
          blueLed = 0;
        } else{
          redLed = 0;
          greenLed = 0;
          blueLed = 255;
          AnimationBlock = 0;
        }
      break;
      case 6:
        // Legacy white mode uses blue because white damages this LED.
        redLed = 0;
        greenLed = 0;
        blueLed = 255;
      break;
      case 7:
        // Solid blue.
        redLed = 0;
        greenLed = 0;
        blueLed = 255;
      break;
      case 8:
        // Solid purple.
        redLed = 255;
        greenLed = 0;
        blueLed = 255;
      break;
      case 9:
        // Flash blue, then turn the LED off.
        if(AnimationBlock == 1){
          redLed = 0;
          greenLed = 0;
          blueLed = 255;
        } else{
          redLed = 0;
          greenLed = 0;
          blueLed = 0;
          AnimationBlock = 0;
        }
      break;
      case 10:
        // Cycle through red, green, and blue.
        if(AnimationBlock == 1){
          redLed = 255;
          greenLed = 0;
          blueLed = 0;
        }
        else if(AnimationBlock == 2){
          redLed = 0;
          greenLed = 255;
          blueLed = 0;
        }
        else{
          redLed = 0;
          greenLed = 0;
          blueLed = 255;
          AnimationBlock = 0;
        }
      break;
      case 11:
        // Alternate between red and green.
        if(AnimationBlock == 1){
          redLed = 255;
          greenLed = 0;
          blueLed = 0;
        } else{
          redLed = 0;
          greenLed = 255;
          blueLed = 0;
          AnimationBlock = 0;
        }
      break;
      }

     #if CORE_HAS_LOCAL_AUDIO_VISUAL
       CBI.setPixelColor(0, redLed, greenLed, blueLed);
      CBI.show();
     #else
       frontendSend("L " + String(redLed) + "," + String(greenLed) + "," + String(blueLed));
     #endif
    }
    
    // Choose the highest-priority active sound request. The ordering here is
    // intentional: unlock/welcome, denial, fault, single beep, then identify.
    if(unlockedBeep || userWelcomed){
      // Approved access or welcome feedback.
      Melody = 1;
    } else if(accessDenied){
      Melody = 2;
    } else if(LightState == "FAULT" || faultBeepRequested){
      Melody = 3;
    } else if(singleBeep){
      if(!firstSingleBeepSkipped){
        // Ignore the first single-beep request after boot.
        firstSingleBeepSkipped = true;
        singleBeep = 0;
      } else{
        Melody = 4;
      }
    } else if(identifyRequested){
      // Repeat the identification tone until the request is cleared.
      Melody = 5;
    } else if(DonePlaying){
      // No request remains; select silence.
      Melody = 0;
    }
    if(Melody != OldMelody){
      // Start a newly selected sound from its first step.
      OldMelody = Melody;
      DonePlaying = 0;
      MelodyTime = 0;
      MelodyStep = 0;
    } else if((MelodyTime >= millis64()) || DonePlaying){
      // No new sound and no tone deadline yet (or the current sound has finished).
      continue;
    }
    // Each melody step lasts 250 ms unless the melody explicitly loops.
    MelodyTime = millis64() + 250;
    switch (Melody){
      case 1:
        // Approved access: two rising notes.
        switch (MelodyStep){
          case 0:
            buzzerTone = 1500;
          break;
          case 1:
            buzzerTone = 2000;
          break;
          case 2:
            buzzerTone = 0;
            unlockedBeep = 0;
            DonePlaying = 1;
          break;
        }
      break;
      case 2:
        // Denied access: two descending notes.
        switch (MelodyStep){
          case 0: 
            buzzerTone = 880;
          break;
          case 1:
            buzzerTone = 440;
          break;
          case 2:
            buzzerTone = 0;
            DonePlaying = 1;
          break;
        }
      break;
      case 3:
        // Fault: three short pulses.
        switch (MelodyStep){
          case 0:
            buzzerTone = 1000;
          break;
          case 1:
            buzzerTone = 0;
          break;
          case 2:
            buzzerTone = 1000;
          break;
          case 3:
            buzzerTone = 0;
          break;
          case 4:
            buzzerTone = 1000;
          break;
          case 5:
            buzzerTone = 0;
            DonePlaying = 1;
            faultBeepRequested = 0;
          break;
        }
      break;
      case 4:
        // Single, short confirmation beep.
        switch(MelodyStep){
          case 0:
            buzzerTone = 1500;
          break;
          case 1:
            buzzerTone = 0;
            DonePlaying = 1;
            singleBeep = 0;
          break;
        }
      break;
      case 5:
        switch(MelodyStep){
          // Step 0 provides a short lead-in before the repeating identification tone.
          case 1:
            buzzerTone = 2000;
          break;
          case 2:
            buzzerTone = 1500;
            MelodyStep = 0;
            // Identification repeats until its request is cleared; it is not self-terminating.
          break;
        }
      break;
    }
    // Apply the selected tone using local hardware or the frontend protocol.
    if(buzzerTone == 0){
      if(tonePlaying){
       #if CORE_HAS_LOCAL_AUDIO_VISUAL
         noTone(PIN_BUZZER);
       #else
         frontendSend("B 0");
       #endif
        tonePlaying = false;
      }
    } else{
     #if CORE_HAS_LOCAL_AUDIO_VISUAL
       tone(PIN_BUZZER, buzzerTone);
     #else
       frontendSend("B " + String(buzzerTone));
     #endif
      tonePlaying = true;
    }
    MelodyStep++;
  }
}


void watchRestartButton(void *pvParameters){
  unsigned long long ButtonTime = 0;
  Serial.println(F("watchRestartButton Started"));
  while(1){
    // Poll at 100 ms intervals. Button polarity differs between local hardware
    // and the frontend-reported input.
    delay(100);
   #if CORE_HAS_LOCAL_AUDIO_VISUAL
     if(digitalRead(PIN_BUTTON)){
   #else
     if(!frontendButtonPressed){
   #endif
      // Start a fresh three-second hold window whenever the button is released.
      ButtonTime = millis64() + 3000;
      resetLed = 0;
    } else{
      resetLed = 1;
      // A held reset button also cancels device-identification mode.
      identifyRequested = 0;
      if(ButtonTime <= millis64()){
        // The hold window expired; request a restart.
        systemState.resetReason = "Restart Button";
        systemState.requestReset = true;
      }
    }
    // Complete any pending reset, regardless of whether the button requested it.
    if(systemState.requestReset){
      Serial.print(F("Restarting. Source: "));
      Serial.println(systemState.resetReason);
      Serial.flush();
     #if CORE_HAS_LOCAL_AUDIO_VISUAL
      CBI.setPixelColor(0, 255, 0, 0);
      CBI.show();
    #else
      frontendSend("L 0,0,255");
    #endif
      settings.putString("system.reset", systemState.resetReason);
      delay(10);
      // Notify a connected screen before shutting down.
    #if CORE_HAS_SCREEN
      Serial0.println("{\"command\":\"restart\"}");
      Serial0.flush();
    #endif

      settings.end();
      // Persist offline data before restarting so no recent changes are lost.
      saveListToSPIFFS();
      Serial.flush();
      delay(50);
      Serial.println(F("Restarting now..."));
      delay(100);
      Serial.flush();
      ESP.restart();
    }
  }
}