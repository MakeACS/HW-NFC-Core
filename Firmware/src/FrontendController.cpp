#include "Globals.h"
#include "FrontendController.h"

void runFrontendController(void *pvParameters) {
#if !CORE_HAS_LOCAL_AUDIO_VISUAL
  unsigned long long nextPollTime = 0;
  while (true) {
    if (nextPollTime <= millis64()) {
      nextPollTime = millis64() + 1000;
      frontend.println("P");
    }

    while (frontend.available()) {
      String message = frontend.readStringUntil('\n');
      message.trim();
      if (message.length() < 3) {
        continue;
      }

      if (message[0] == 'B') {
        frontendButtonPressed = message[2] == '1';
      } else if (message[0] == 'S') {
        const bool cardDetected = message[3] != '1';
        if (message[1] == '1') {
          frontendCardDetect1 = cardDetected;
        } else {
          frontendCardDetect2 = cardDetected;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
#else
  vTaskDelete(NULL);
#endif
}