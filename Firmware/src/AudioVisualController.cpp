/* 
These tasks are responsible for communicating with the frontend, handling things like switch states, LED and buzzer control, etc.
There are 2 tasks;
  runAudioVisualController - Converts flags from other tasks into a series of LED lights and buzzer tones.
  watchRestartButton - Listens for the front button to be held for 5 seconds, restarts the device. All other reset source throughout the code are handled by this task as well.
*/

#include "Globals.h"
#include "AudioVisualController.h"
#include "TaggedSerial.h"

namespace {
TaggedSerial<decltype(::Serial)> audioVisualSerial(::Serial, "[av] ");
}

#define Serial audioVisualSerial

uint64_t millis64();

void runAudioVisualController(void *pvParameters){
  unsigned long long OkToLED = 0;
  byte LEDAnimation = 0;
  byte OldLEDAnimation = 0;
  uint64_t AnimationTime = 0;
  byte AnimationBlock = 0;
  byte Melody = 0;
  byte OldMelody = 0;
  byte DonePlaying = 0;
  byte MelodyStep = 0;
  bool tonePlaying = false;
  uint64_t MelodyTime = 0;
  Serial.println(F("runAudioVisualController Started."));
  while(1){
    vTaskDelay(20 / portTICK_PERIOD_MS);
    //First, set the animation state:
    //Animation triggers are not exclusive, so if statements written in reverse-priority order.

    //NEW: Since there are 4 channels, we need to consider all 4 before we set a light.
    //Lighting should be based on the most permissive level currently available...
    //i.e. if Ch 1 is unlocked and Ch 2 is locked out, show a light as if we are unlocked
    //But, any channel being in fault or unknown takes priority.
    //So, we do a reverse-priority order collection of the states here to use for old lighting code.
    String LightState = "NOTHING";
    int highestPriority = 0;
    for (int i = 0; i < channels.count; i++) {
      int currentPriority = 0;

      // Tier 4 (Absolute Priority): Faults or Unknown states
      if (channels.states[i] == "FAULT" || channels.states[i] == "UNKNOWN") {
        currentPriority = 4;
      } 
      // Tier 3 (Most Permissive): Active access states
      else if (channels.states[i] == "UNLOCKED" || channels.states[i] == "ALWAYS_ON") {
        currentPriority = 3;
      } 
      // Tier 2 (Neutral): Waiting for interaction
      else if (channels.states[i] == "IDLE") {
        currentPriority = 2;
      } 
      // Tier 1 (Least Permissive): Completely locked out
      else if (channels.states[i] == "LOCKED_OUT") {
        currentPriority = 1;
      }

      // If this channel outranks the previous ones, update the LightState
      if (currentPriority > highestPriority) {
        highestPriority = currentPriority;
        LightState = channels.states[i]; // Inherit the actual state string (e.g., grabs "ALWAYS_ON" vs "UNLOCKED")
      }
    }
    if(LightState == "NOTHING"){
      LightState = "UNKNOWN";
    }

    if(LightState.equals("IDLE")){
      //Animation 3: Solid Yellow
      LEDAnimation = 3;
    }
    if(welcomeMode){
      //For now it is solid yellow, TODO switch to a slow blink yellow 10% duty cycle or so to catch user attention.
      LEDAnimation = 3;
    }
    if(pendingApproval || mqttState.welcomingPending){
      //Animation 4: Flashing Yellow
      LEDAnimation = 4;
    }
    if(LightState.equals("UNLOCKED") || LightState.equals("ALWAYS_ON") || userWelcomed){
      //Animation 2: Solid greenLed
      LEDAnimation = 2;
    }
    if((LightState.equals("UNKNOWN") && !welcomeMode) || networkState.unavailable){
      //If any channel is unknown (when not in welcome mode), or the network is unavailable.
      //Animation 7: Solid blueLed
      LEDAnimation = 7;
    }
    if((LightState.equals("UNLOCKED") || LightState.equals("ALWAYS_ON")) && networkState.unavailable){
      //Animation 5: Alternate blue/green
      LEDAnimation = 5;
    }
    if(systemState.imminentShutdown){
      //Play a warning alternating between red and green
      //Used when a machine is unlocked, but we should tell the user to stop.
      LEDAnimation = 11;
    }
    if(LightState.equals("LOCKED_OUT") || accessDenied){
      //Animation 1: Solid redLed
      LEDAnimation = 1;
    }
    if(identifyRequested){
      //Animation 9: Flashing blueLed
      LEDAnimation = 9;
    }
    if(resetLed){
      //Animation 8: Solid Purple
      LEDAnimation = 8;
    }
    if(LightState.equals("FAULT")){
      LEDAnimation = 0;
    }
    if(gamerMode){
      LEDAnimation = 10;
    }
    //Next, see if the animation changed;
    if(LEDAnimation != OldLEDAnimation){
      OldLEDAnimation = LEDAnimation;
      AnimationTime = 0; //Force an update of the animation block
    } 
    if(AnimationTime <= millis64()){
      //It is time to advance to the next animation block
      AnimationBlock++;
      if(LEDAnimation == 5){
        //Animation 5 runs at a slower speed
        AnimationTime = millis64() + 3000;
      } else{
        AnimationTime = millis64() + 400;
      }
      //Set the animation block here;
      switch(LEDAnimation){
      case 0:
        //Flashing redLed
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
        //Solid redLed
        redLed = 255;
        greenLed = 0;
        blueLed = 0;
      break;
      case 2:
        //Solid greenLed
        redLed = 0;
        greenLed = 255;
        blueLed = 0;
      break;
      case 3:
        //Solid Yellow
        redLed = 255;
        greenLed = 255;
        blueLed = 0;
      break;
      case 4:
        //Flashing Yellow
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
        //Cycle greenLed/blueLed
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
        //Solid White
        //redLed = 255;
        //greenLed = 255;
        //blueLed = 255;
        //This break the led. go for blue instead.
        redLed = 0;
        greenLed = 0;
        blueLed = 255;
      break;
      case 7:
        //Solid blueLed
        redLed = 0;
        greenLed = 0;
        blueLed = 255;
      break;
      case 8:
        //Solid Purple
        redLed = 255;
        greenLed = 0;
        blueLed = 255;
      break;
      case 9:
        //Flashing blue
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
        //redLed - greenLed - blueLed
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
        //Alternate red-green
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
       frontend.println("L " + String(redLed) + "," + String(greenLed) + "," + String(blueLed));
     #endif
      /*
      //If we are here, there is a new LED to send.
      if(OkToLED <= millis64()){
        //We enforce updates slower than 3Hz here to ensure no strobing or race conditions
        OkToLED = millis64() + 333;
        CBI.setPixelColor(0, redLed, greenLed, blueLed);
        CBI.show();
      }
      */
    }
    
    //After the LED, we need to set up the buzzer.

    if(unlockedBeep || userWelcomed){
      //The machine has been unlocked
      Melody = 1;
    } else if(accessDenied){
      Melody = 2;
    } else if(LightState == "FAULT" || faultBeepRequested){
      Melody = 3;
    } else if(singleBeep){
      Melody = 4;
    } else if(identifyRequested){
      //Play a constant tone to identify the device
      Melody = 5;
    } else if(DonePlaying){
      //If none of these apply, turn off the buzzer
      Melody = 0;
    }
    if(Melody != OldMelody){
      //Melody has changed!
      OldMelody = Melody;
      DonePlaying = 0;
      MelodyTime = 0;
      MelodyStep = 0;
    } else if((MelodyTime >= millis64()) || DonePlaying){
      //Melody didn't change and it's not time to advance to the next tone or the system is done playing a tone
      continue;
    }
    //If we make it here, there is a tone to be changed.
    MelodyTime = millis64() + 250; //Set the time to change the note again.
    switch (Melody){
      case 1:
        //Approved tone
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
        //Denied tone
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
        //Fault tone
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
        //Single beep
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
          //No case 0, this basically makes the sound delay before it starts playing.
          case 1:
            buzzerTone = 2000;
          break;
          case 2:
            buzzerTone = 1500;
            MelodyStep = 0;
            //This one ends when we command it to, so we don't put DonePlaying here.
            //Instead, the identifyRequested state ends with the change beep.
          break;
        }
      break;
    }
    //If we make it here, we should update the buzzer;
    if(buzzerTone == 0){
      if(tonePlaying){
       #if CORE_HAS_LOCAL_AUDIO_VISUAL
         noTone(PIN_BUZZER);
       #else
         frontend.println("B 0");
       #endif
        tonePlaying = false;
      }
    } else{
     #if CORE_HAS_LOCAL_AUDIO_VISUAL
       tone(PIN_BUZZER, buzzerTone);
     #else
       frontend.println("B " + String(buzzerTone));
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
    //First, check if the button is being held to trigger a restart;
    delay(100);
   #if CORE_HAS_LOCAL_AUDIO_VISUAL
     if(digitalRead(PIN_BUTTON)){
   #else
     if(!frontendButtonPressed){
   #endif
      //Button is not being pressed
      ButtonTime = millis64() + 3000;
      resetLed = 0;
    } else{
      resetLed = 1;
      //ALSO: Turn off identify tone if playing. 
      identifyRequested = 0;
      if(ButtonTime <= millis64()){
        //Button has been held long enough to trigger a restart.
        systemState.resetReason = "Restart Button";
        systemState.requestReset = true;
      }
    }
    //Next, check if anything has asked for the device to be restarted.
    if(systemState.requestReset){
      vTaskSuspendAll(); //Stop all other tasks
      Serial.print(F("Restarting. Source: "));
      Serial.println(systemState.resetReason);
      Serial.flush();
     #if CORE_HAS_LOCAL_AUDIO_VISUAL
      CBI.setPixelColor(0, 255, 0, 0);
      CBI.show();
    #else
      frontend.println("L 0,0,255");
    #endif
      settings.putString("system.reset", systemState.resetReason);
      //Tell the frontend, if connected;
    #if CORE_HAS_SCREEN
      Serial0.println("{\"command\":\"restart\"}");
      Serial0.flush();
    #endif
      delay(50);
      ESP.restart();
    }
  }
}