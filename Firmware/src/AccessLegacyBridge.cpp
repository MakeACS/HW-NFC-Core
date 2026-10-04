#include "Globals.h"
#include "AccessManager.h"
#include "AccessLegacyBridge.h"
#include "CardReader.h"
#include "OfflineList.h"

static void updateConfigFrontend(const AccessSnapshot &snap) {
#ifndef REDUCED_CONFIG
    static ChannelState lastState[CORE_MAX_CHANNELS];
    static AccessMode lastMode = AccessMode::Normal;
    static char lastUid[32] = "";
    static bool lastOnList = false;

    for (uint8_t i = 0; i < snap.channels.count; i++) {
        const Channel &c = snap.channels.ch[i];
        if (c.state != lastState[i]) {
            if (lastState[i] != ChannelState::Unknown) {
                String source = "Channel " + String(i);
                config.updateInformation(source, "channel-state", toApiString(c.state));
                config.updateInformation(source, "channel-reason", toApiString(c.changeReason));
            }
            lastState[i] = c.state;
        }
    }
    if (snap.mode != lastMode) {
        lastMode = snap.mode;
        config.updateInformation("General", "mode", snap.mode == AccessMode::Welcome ? String("Welcome Reader (Tap)") : defaultInputMode);
    }
    if (strcmp(snap.cardUid, lastUid) != 0 || snap.userOnOfflineList != lastOnList) {
        const bool cardChanged = strcmp(snap.cardUid, lastUid) != 0;
        const bool justAdded = !cardChanged && snap.userOnOfflineList && !lastOnList;
        strlcpy(lastUid, snap.cardUid, sizeof(lastUid));
        lastOnList = snap.userOnOfflineList;
        if (snap.cardUid[0]) {
            config.updateInformation("General", "current-card", String(snap.cardUid));
            config.updateInformation("Current ID", "current-id", String(snap.cardUid));
            config.updateInformation("Current ID", "on-list", justAdded ? String("Just added!") : String(snap.userOnOfflineList ? "True" : "False"));
        } else {
            config.updateInformation("General", "current-card", "Waiting...");
            config.updateInformation("Current ID", "current-id", "Waiting...");
            config.updateInformation("Current ID", "on-list", "Waiting...");
        }
        if (justAdded) config.updateInformation("Total", "offline-count", String(getOfflineListSize()));
    }
#endif
}

void runAccessBridgeLoop(void *pvParameters) {
    static AccessSnapshot snap;
    for (;;) {
        xEventGroupWaitBits(accessEvents, ACCESS_EVENT_CHANGED | ACCESS_EVENT_OUTBOUND, pdTRUE, pdFALSE, portMAX_DELAY);
        accessGetSnapshot(snap);
        updateScreen = true;
        updateConfigFrontend(snap);

        AccessOutbound out;
        while (xQueueReceive(accessOutbound, &out, 0) == pdTRUE) {
            switch (out.type) {
                case AccessOutType::AuthRequest:    mqttState.sendAuth = true; break;
                case AccessOutType::StateChange:    mqttState.stateChange = true; break;
                case AccessOutType::WelcomeRequest: mqttState.sendWelcome = true; break;
                case AccessOutType::InfoRequest:    mqttState.requestInfo = true; break;
            }
        }
        //Feedback (beeps) is consumed directly by the AudioVisualController.

    }
}