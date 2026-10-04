#include "AccessManager.h"
#include "BusDriver.h"
#include "OfflineList.h"
#include <ArduinoJson.h>
#include <freertos/semphr.h>

uint64_t millis64(); //helperFunctions.cpp

EventGroupHandle_t accessEvents = nullptr;
QueueHandle_t accessOutbound = nullptr;
QueueHandle_t accessFeedback = nullptr;

static QueueHandle_t inbox = nullptr;
static SemaphoreHandle_t snapshotMutex = nullptr;
static AccessSnapshot published;

//------------------------------------------------------------------------------------------------
// Pure logic
//------------------------------------------------------------------------------------------------

static void setChannel(Channel &c, ChannelState state, ChangeReason reason) {
    c.state = state;
    c.changeReason = reason;
}

static bool anyIn(const ChannelSet &set, ChannelState state) { return set.any(state); }

static void deny(AccessState &s, DenyReason reason, AccessEffects &fx) {
    for (uint8_t i = 0; i < s.snap.channels.count; i++) {
        s.snap.channels.ch[i].denyReason = reason;
        s.snap.channels.ch[i].denyText[0] = '\0';
    }
    s.snap.accessDenied = true;
    if (reason == DenyReason::NoNetwork) fx.addFeedback(AccessFeedback::FaultBeep);
}

static void raiseFeedback(AccessEffects &fx, AccessFeedback f) {
    for (uint8_t i = 0; i < fx.feedbackCount; i++) if (fx.feedback[i] == f) return; //One beep, not one per channel
    fx.addFeedback(f);
}

static void startTapWindow(AccessState &s, Channel &c, uint64_t nowMs) {
    if (s.snap.inputMode == InputMode::TempPresent) c.tapExpiresAtMs = nowMs + c.tapDurationMs;
}

static void parseFlags(AccessState &s, JsonObject flags, AccessEffects &fx);

static void enterWelcomeMode(AccessState &s) {
    s.snap.mode = AccessMode::Welcome;
    s.snap.inputMode = InputMode::TempPresent;
    s.snap.channels.count = 0;
}

static void leaveWelcomeMode(AccessState &s, AccessEffects &fx) {
    s.snap.mode = AccessMode::Normal;
    s.snap.inputMode = s.config.inputMode;
    s.snap.channels.count = s.config.channelCount;
    for (uint8_t i = 0; i < s.snap.channels.count; i++) {
        Channel &c = s.snap.channels.ch[i];
        setChannel(c, ChannelState::Unknown, ChangeReason::ServerCommanded);
        c.lastState = ChannelState::Unknown;
        c.reportedState = ChannelState::Unknown;
    }
    AccessOutbound out;
    out.type = AccessOutType::InfoRequest;
    fx.addOutbound(out); //We no longer know what state we should be in
}

static void parseFlags(AccessState &s, JsonObject flags, AccessEffects &fx) {
    if (flags["lockWhenIdle"].is<bool>()) s.snap.lockWhenIdle = flags["lockWhenIdle"].as<bool>();
    if (flags["restartWhenUnused"].is<bool>()) s.snap.restartWhenUnused = flags["restartWhenUnused"].as<bool>();
    if (flags["welcoming"].is<bool>()) {
        bool welcoming = flags["welcoming"].as<bool>();
        if (welcoming && s.snap.mode != AccessMode::Welcome) enterWelcomeMode(s);
        else if (!welcoming && s.snap.mode == AccessMode::Welcome) leaveWelcomeMode(s, fx);
    }
}

static void setHobbs(Channel &c, uint32_t seconds, uint64_t nowMs) {
    c.hobbsSeconds = seconds;
    if (c.hasAccess()) c.accessSinceMs = nowMs ? nowMs : 1; //Restart the running period from the server's value
}

static void parseHobbs(AccessState &s, JsonVariant array, uint64_t nowMs) {
    for (JsonObject item : array.as<JsonArray>()) {
        int ch = item["channelID"] | -1;
        if (ch >= 0 && ch < s.snap.channels.count) setHobbs(s.snap.channels.ch[ch], item["hobbsTime"] | 0UL, nowMs);
    }
}

static void unlockIdle(AccessState &s, Channel &c, uint64_t nowMs) {
    setChannel(c, ChannelState::Unlocked, ChangeReason::Authed);
    startTapWindow(s, c, nowMs);
}

static void handleCardInserted(AccessState &s, const AccessEvent &e, uint64_t nowMs, uint32_t epoch, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    s.snap.cardPresent = true;
    strlcpy(s.snap.cardUid, e.uid, sizeof(s.snap.cardUid));
    const bool readable = e.uid[0] != '\0';
    s.snap.userOnOfflineList = readable && checkOfflineList(String(e.uid));
    const bool noNetwork = !s.networkAvailable;

    if (!readable) { deny(s, DenyReason::UnreadableCard, fx); return; }

    //Without a network only users on the offline list can be handled
    if (noNetwork && !s.snap.userOnOfflineList && (s.snap.inputMode == InputMode::TempPresent || s.snap.mode == AccessMode::Welcome)) {
        deny(s, DenyReason::NoNetwork, fx);
        return;
    }

    if (s.snap.mode == AccessMode::Welcome) {
        if (noNetwork) { s.snap.accessDenied = true; return; }
        s.snap.welcomingPending = true;
        AccessOutbound out; out.type = AccessOutType::WelcomeRequest;
        strlcpy(out.uid, e.uid, sizeof(out.uid));
        fx.addOutbound(out);
        return;
    }

    const bool tap = s.snap.inputMode == InputMode::TempPresent;
    const bool canUnlock = anyIn(set, ChannelState::Idle) || (tap && anyIn(set, ChannelState::Unlocked));
    if (!canUnlock) {
        if (set.anyAccess()) raiseFeedback(fx, AccessFeedback::SingleBeep);   //Already on: just confirm
        else deny(s, DenyReason::BadState, fx);
        return;
    }
    if (noNetwork) {
        if (s.snap.userOnOfflineList && set.count == 1) {
            //Offline auth: the list is the authority
            Channel &c = set.ch[0];
            if (c.state == ChannelState::Idle) { unlockIdle(s, c, nowMs); c.changeReason = ChangeReason::Local; }
            else if (tap && c.state == ChannelState::Unlocked) startTapWindow(s, c, nowMs);
            raiseFeedback(fx, AccessFeedback::UnlockedBeep);
            s.snap.pendingApproval = false;
        } else {
            deny(s, DenyReason::NoNetwork, fx);
        }
        return;
    }
    s.snap.pendingApproval = true;
    AccessOutbound out; out.type = AccessOutType::AuthRequest;
    strlcpy(out.uid, e.uid, sizeof(out.uid));
    fx.addOutbound(out);
}

static void handleCardRemoved(AccessState &s, AccessEffects &fx) {
    s.snap.cardPresent = false;
    s.snap.cardUid[0] = '\0';
    s.snap.userOnOfflineList = false;
    s.snap.pendingApproval = false;
    s.snap.welcomingPending = false;
    s.snap.accessDenied = false;
    s.snap.userWelcomed = false;
    if (s.snap.inputMode == InputMode::Insert) {
        for (uint8_t i = 0; i < s.snap.channels.count; i++) {
            Channel &c = s.snap.channels.ch[i];
            if (c.state == ChannelState::Unlocked) setChannel(c, ChannelState::Idle, ChangeReason::CardRemoved);
        }
    }
}

static void handleInterrupt(AccessState &s, bool asserted, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    s.snap.interrupted = asserted;
    const InterruptResponse r = s.config.interruptResponse;
    for (uint8_t i = 0; i < set.count; i++) {
        Channel &c = set.ch[i];
        if (asserted) {
            switch (r) {
                case InterruptResponse::LockTemp:
                    if (c.state == ChannelState::Unlocked || c.state == ChannelState::AlwaysOn || c.state == ChannelState::Idle) {
                        setChannel(c, ChannelState::LockedOut, ChangeReason::LockTemp);
                        raiseFeedback(fx, AccessFeedback::SingleBeep);
                    }
                    break;
                case InterruptResponse::Idle:
                    if (c.hasAccess()) {
                        setChannel(c, ChannelState::Idle, ChangeReason::Local);
                        raiseFeedback(fx, AccessFeedback::SingleBeep);
                    }
                    break;
                case InterruptResponse::Fault:
                    setChannel(c, ChannelState::Fault, ChangeReason::Fault);
                    strlcpy(s.snap.faultReason, "Interrupt Asserted!", sizeof(s.snap.faultReason));
                    break;
                case InterruptResponse::Message:
                    break;
            }
        } else if (r == InterruptResponse::LockTemp && c.state == ChannelState::LockedOut) {
            setChannel(c, ChannelState::Idle, ChangeReason::Local);
            raiseFeedback(fx, AccessFeedback::SingleBeep);
        }
    }
    //InterruptResponse::Message is reported by MainController, which watches snapshot.interrupted changing.
}

static void handleAuthResult(AccessState &s, JsonDocument &doc, uint64_t nowMs, uint32_t epoch, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    const bool tap = s.snap.inputMode == InputMode::TempPresent;
    const char *authId = doc["cardTagID"] | "";
    s.snap.pendingApproval = false;
    for (JsonVariant v : doc["channels"].as<JsonArray>()) {
        int ch = v["channelID"] | 0;
        if (ch < 0 || ch >= set.count) continue;
        Channel &c = set.ch[ch];
        const bool approved = v["approved"].as<bool>();
        const char *reason = v["reason"] | "";
        c.denyReason = approved ? DenyReason::None : DenyReason::Server;
        strlcpy(c.denyText, reason, sizeof(c.denyText));
        if (!(c.state == ChannelState::Idle || (c.state == ChannelState::Unlocked && tap))) continue; //Improper state
        if (approved) {
            if (strcmp(authId, s.snap.cardUid) != 0) continue; //Card was swapped while waiting
            if (!s.snap.userOnOfflineList) {
                updateOfflineList(String(s.snap.cardUid), epoch);
                s.snap.userOnOfflineList = true;
            }
            unlockIdle(s, c, nowMs);
            raiseFeedback(fx, AccessFeedback::UnlockedBeep);
        } else if (s.snap.cardPresent) {
            s.snap.accessDenied = true;
        }
    }
}

static void handleInfo(AccessState &s, JsonDocument &doc, uint64_t nowMs, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    Serial.printf("[access] Info received (state array: %s, %u channel(s))\n", doc["state"].is<JsonArray>() ? "yes" : "no", (unsigned)set.count);
    if (doc["state"].is<JsonArray>()) {
        for (JsonObject item : doc["state"].as<JsonArray>()) {
            int id = item["id"] | -1;
            if (id < 0 || id >= set.count) continue;
            ChannelState st = fromApiString(item["state"] | "UNKNOWN");
            //Info sync never restores active or faulted states: normalize them to safe, non-active states.
            if (st == ChannelState::Fault) st = ChannelState::LockedOut;
            if (st == ChannelState::Unlocked || st == ChannelState::AlwaysOn) st = ChannelState::Idle;
            setChannel(set.ch[id], st, ChangeReason::Commanded);
        }
        raiseFeedback(fx, AccessFeedback::SingleBeep);
    }
    if (doc["hobbsTime"].is<JsonArray>()) parseHobbs(s, doc["hobbsTime"], nowMs);
    if (doc["flags"].is<JsonObject>()) parseFlags(s, doc["flags"].as<JsonObject>(), fx);
}

static void handleCommand(AccessState &s, JsonDocument &doc, uint64_t nowMs, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    if (doc["toState"].is<JsonArray>()) {
        for (JsonVariant v : doc["toState"].as<JsonArray>()) {
            int id = v["id"] | -1;
            if (id < 0 || id >= set.count) continue;
            ChannelState st = fromApiString(v["state"] | "UNKNOWN");
            if (st == ChannelState::Unlocked && !s.snap.cardPresent) st = ChannelState::Idle;
            setChannel(set.ch[id], st, ChangeReason::Commanded);
        }
        raiseFeedback(fx, AccessFeedback::SingleBeep);
    }
    if (doc["flags"].is<JsonObject>()) parseFlags(s, doc["flags"].as<JsonObject>(), fx);
    if (doc["hobbsTime"].is<JsonArray>()) parseHobbs(s, doc["hobbsTime"], nowMs);
    //"action" (RESTART, SEAL, IDENTIFY, SCHEDULED_RESTART) is not access related; MainController keeps it.
}

static void handleWelcomeResult(AccessState &s, JsonDocument &doc) {
    if (doc["welcomed"].as<bool>()) {
        if (strcmp(doc["cardTagID"] | "", s.snap.cardUid) == 0) s.snap.userWelcomed = true;
    } else {
        s.snap.accessDenied = true;
    }
}

//Everything that must be true after any event: expiry, lockWhenIdle, hobbs, change reports, outputs.
static void finalize(AccessState &s, uint64_t nowMs, AccessEffects &fx) {
    ChannelSet &set = s.snap.channels;
    const bool tap = s.snap.inputMode == InputMode::TempPresent;

    for (uint8_t i = 0; i < set.count; i++) {
        Channel &c = set.ch[i];
        if (tap) {
            if (c.tapExpiresAtMs && c.tapExpiresAtMs <= nowMs) {
                if (c.state == ChannelState::Unlocked) {
                    setChannel(c, ChannelState::Idle, ChangeReason::CardRemoved);
                    raiseFeedback(fx, AccessFeedback::SingleBeep);
                }
                c.tapExpiresAtMs = 0;
            }
        } else {
            c.tapExpiresAtMs = 0;
        }
    }

    if (s.snap.lockWhenIdle && !set.any(ChannelState::Unlocked) && !set.any(ChannelState::AlwaysOn)) {
        for (uint8_t i = 0; i < set.count; i++) {
            if (set.ch[i].state == ChannelState::Idle) { setChannel(set.ch[i], ChannelState::LockedOut, ChangeReason::Commanded); raiseFeedback(fx, AccessFeedback::SingleBeep); }
        }
        s.snap.lockWhenIdle = false;
    }

    //Hobbs: fold the finished period in when a channel stops being accessible
    for (uint8_t i = 0; i < set.count; i++) {
        Channel &c = set.ch[i];
        if (c.hasAccess() && !c.accessSinceMs) c.accessSinceMs = nowMs ? nowMs : 1;
        else if (!c.hasAccess() && c.accessSinceMs) {
            c.hobbsSeconds += (uint32_t)((nowMs - c.accessSinceMs) / 1000);
            c.accessSinceMs = 0;
        }
    }

    //State-change report (not in welcome mode, and never for the first state after UNKNOWN)
    AccessOutbound change; change.type = AccessOutType::StateChange;
    for (uint8_t i = 0; i < set.count; i++) {
        Channel &c = set.ch[i];
        if (c.state != c.lastState) {
            if (c.lastState != ChannelState::Unknown && s.snap.mode != AccessMode::Welcome) {
                change.change[i] = { c.state, c.changeReason };
                change.changedMask |= (1 << i);
                c.reportedState = c.state;
            }
            c.lastState = c.state;
            fx.publish = true;
        }
    }
    if (change.changedMask) fx.addOutbound(change);

    const uint8_t mask = set.accessMask();
    s.snap.deviceInUse = set.anyAccess();
    if (mask != s.lastRequestedMask) {
        s.lastRequestedMask = mask;
        fx.bumpAccess = true;
    }
    fx.accessMask = mask;
    if (!s.snap.cardPresent) {
        s.snap.pendingApproval = false;
        s.snap.welcomingPending = false;
        s.snap.accessDenied = false;
    }
    s.snap.takenAtMs = nowMs;
}

void accessHandleEvent(AccessState &s, const AccessEvent &e, uint64_t nowMs, uint32_t epoch, AccessEffects &fx) {
    fx.publish = true;
    JsonDocument doc;
    if (e.payload) deserializeJson(doc, e.payload);
    switch (e.type) {
        case AccessEventType::CardInserted:       handleCardInserted(s, e, nowMs, epoch, fx); break;
        case AccessEventType::CardRemoved:        handleCardRemoved(s, fx); break;
        case AccessEventType::InterruptAsserted:  handleInterrupt(s, true, fx); break;
        case AccessEventType::InterruptCleared:   handleInterrupt(s, false, fx); break;
        case AccessEventType::NetworkChanged:     s.networkAvailable = e.flag; break;
        case AccessEventType::ApiAuthResult:      handleAuthResult(s, doc, nowMs, epoch, fx); break;
        case AccessEventType::ApiInfo:            handleInfo(s, doc, nowMs, fx); break;
        case AccessEventType::ApiCommand:         handleCommand(s, doc, nowMs, fx); break;
        case AccessEventType::ApiWelcomeResult:   handleWelcomeResult(s, doc); break;
        case AccessEventType::OfflineAuthTimeout: s.snap.pendingApproval = false; break;
        case AccessEventType::FaultRaised:
            for (uint8_t i = 0; i < s.snap.channels.count; i++) setChannel(s.snap.channels.ch[i], ChannelState::Fault, e.reason);
            strlcpy(s.snap.faultReason, toApiString(e.reason), sizeof(s.snap.faultReason));
            break;
        case AccessEventType::SnapshotRequest:    break;
    }
    finalize(s, nowMs, fx);
}

uint32_t accessHandleTime(AccessState &s, uint64_t nowMs, AccessEffects &fx) {
    finalize(s, nowMs, fx);
    uint64_t next = 0;
    for (uint8_t i = 0; i < s.snap.channels.count; i++) {
        uint64_t t = s.snap.channels.ch[i].tapExpiresAtMs;
        if (t && (!next || t < next)) next = t;
    }
    return next ? (uint32_t)(next > nowMs ? next - nowMs : 1) : 0;
}

//------------------------------------------------------------------------------------------------
// Task and plumbing
//------------------------------------------------------------------------------------------------

void accessManagerInit(const AccessConfig &config) {
    accessEvents = xEventGroupCreate();
    inbox = xQueueCreate(16, sizeof(AccessEvent));
    accessOutbound = xQueueCreate(8, sizeof(AccessOutbound));
    accessFeedback = xQueueCreate(8, sizeof(AccessFeedback));
    snapshotMutex = xSemaphoreCreateMutex();
    published.inputMode = config.inputMode;
    published.channels.count = config.channelCount;
}

bool accessPost(const AccessEvent &event) {
    return inbox && xQueueSend(inbox, &event, 0) == pdTRUE;
}

bool accessPostPayload(AccessEventType type, const char *json) {
    AccessEvent e; e.type = type;
    e.payload = strdup(json ? json : "");
    if (!e.payload) return false;
    if (accessPost(e)) return true;
    free(e.payload);
    return false;
}

bool accessPostCard(bool inserted, const char *uid) {
    AccessEvent e; e.type = inserted ? AccessEventType::CardInserted : AccessEventType::CardRemoved;
    if (uid) strlcpy(e.uid, uid, sizeof(e.uid));
    return accessPost(e);
}

void accessGetSnapshot(AccessSnapshot &out) {
    if (!snapshotMutex) { out = AccessSnapshot(); return; } //Called before accessManagerInit()
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    out = published;
    xSemaphoreGive(snapshotMutex);
}

uint32_t accessHobbsNow(uint8_t channel) {
    AccessSnapshot snap;
    accessGetSnapshot(snap);
    return snap.hobbsNow(channel, millis64());
}

extern ESP32Time rtc;

void runAccessManagerLoop(void *pvParameters) {
    static AccessState state; //Large; keep off the task stack
    state.config = *static_cast<AccessConfig *>(pvParameters);
    state.snap.inputMode = state.config.inputMode;
    state.snap.channels.count = state.config.channelCount;
    for (uint8_t i = 0; i < state.snap.channels.count; i++) state.snap.channels.ch[i].tapDurationMs = state.config.tapDurationMs[i];

    //Publish the starting snapshot so consumers see the configured channels (all UNKNOWN) before the first event
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    published = state.snap;
    xSemaphoreGive(snapshotMutex);
    xEventGroupSetBits(accessEvents, ACCESS_EVENT_CHANGED);
    Serial.printf("[access] Started with %u channel(s)\n", (unsigned)state.snap.channels.count);

    uint32_t waitMs = 0;
    for (;;) {
        AccessEvent e;
        AccessEffects fx;
        const TickType_t wait = waitMs ? pdMS_TO_TICKS(waitMs) : portMAX_DELAY;
        if (xQueueReceive(inbox, &e, wait) == pdTRUE) {
            accessHandleEvent(state, e, millis64(), rtc.getEpoch(), fx);
            if (e.payload) free(e.payload);
        } else {
            accessHandleTime(state, millis64(), fx);
        }
        waitMs = accessHandleTime(state, millis64(), fx);

        if (fx.bumpAccess) {
            //Retry on the next pass if the bus queue was full
            if (!busSetAccess(fx.accessMask, state.snap.channels.count)) state.lastRequestedMask = 0xFF;
        }
        if (fx.publish) {
            xSemaphoreTake(snapshotMutex, portMAX_DELAY);
            published = state.snap;
            xSemaphoreGive(snapshotMutex);
            xEventGroupSetBits(accessEvents, ACCESS_EVENT_CHANGED);
        }
        for (uint8_t i = 0; i < fx.outboundCount; i++) xQueueSend(accessOutbound, &fx.outbound[i], 0);
        for (uint8_t i = 0; i < fx.feedbackCount; i++) xQueueSend(accessFeedback, &fx.feedback[i], 0);
        if (fx.outboundCount || fx.feedbackCount) xEventGroupSetBits(accessEvents, ACCESS_EVENT_OUTBOUND);
    }
}