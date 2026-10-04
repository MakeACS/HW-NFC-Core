//Access Manager

//The Access Manager is responsible for handling everything related to state changes
//Based on API response from MainController and sending the results to BusDriver. 
//It also handles returning to an IDLE state on the removal of a card from CardReader.

//Scope:
    // * Prompt the sending of auth API requests when a card is presented via CardReader
    // * Parse incoming API messages related to access state
        // * Auth, set state, and flags like lockwhenidle
    // * Handle the execution of flags that the API sets
        // * LockWhenIdle, etc.
    // * Manage the offline list
        // * Use the offline list for auth if there is no network
        // * Add users to offline list if they are not and have been authed
    // * Handle logic related to Interrupts from the BusDriver
    // * Set access control and output channels via BusDriver
    // * Handles the special case of being in Welcome Mode
        // * No auth in welcome mode, just take presented card and pass to the API, wait for response.

//Events created for other tasks to use:
    // on state change (for BusDriver and MainController)

//Variables for other tasks to use:
    // deviceInUse (bool, indicates device in use so should not i.e. restart or something)

//Design notes:
// * One inbox queue. Everything the AccessManager reacts to (card, bus edges, API payloads, flags) is an
//   AccessEvent posted to it, so no edge is ever lost the way a coalesced event-group bit could be.
//   The queue wait timeout is the next deadline (tap expiry), so there is no polling.
// * Channel data lives in one ChannelSet (see AccessTypes.h), owned by this task only. Other tasks read
//   a mutex-protected AccessSnapshot and are told about changes with ACCESS_EVENT_CHANGED.
// * One-shot things (auth request, state-change report, ...) go out on the outbound queue, which
//   MainController drains and turns into MQTT. Beeps/lights go out on the feedback queue.
// * Hobbs is not ticked. It is computed on demand from the moment a channel became accessible, so
//   a status message simply calls accessRequestStatus() / reads snapshot.hobbsNow(). 
// * The logic itself (accessHandleEvent) takes explicit state and time and has no FreeRTOS calls in it.
// * API enum strings are converted only at the edge (see AccessTypes.h).

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include "AccessTypes.h"

//Settings applied at accessManagerInit()
struct AccessConfig {
    InputMode inputMode = InputMode::TempPresent;
    InterruptResponse interruptResponse = InterruptResponse::Fault;
    uint8_t channelCount = 1;
    uint32_t tapDurationMs[CORE_MAX_CHANNELS] = {};
};

//---- Inbox ----
enum class AccessEventType : uint8_t {
    CardInserted,       //uid = card UID (empty if the card could not be read)
    CardRemoved,
    InterruptAsserted,
    InterruptCleared,
    NetworkChanged,     //flag = network available
    ApiAuthResult,      //payload = JSON from the server
    ApiInfo,            //payload = JSON from the server
    ApiCommand,         //payload = JSON from the server
    ApiWelcomeResult,   //payload = JSON from the server
    OfflineAuthTimeout, //The pending auth request is not going to be answered
    FaultRaised,        //reason = ChangeReason (e.g. OverTemp, IntegrityFail)
    SnapshotRequest,    //Re-publish the snapshot with current hobbs (for status messages)
};

struct AccessEvent {
    AccessEventType type;
    bool flag = false;
    ChangeReason reason = ChangeReason::Unknown;
    char uid[32] = "";
    char *payload = nullptr; //Heap copy of JSON; the AccessManager frees it after handling
};

//---- Outbound (consumed by MainController) ----
enum class AccessOutType : uint8_t { AuthRequest, StateChange, WelcomeRequest, InfoRequest };

struct AccessOutbound {
    AccessOutType type;
    char uid[32] = "";                       //AuthRequest, WelcomeRequest
    struct { ChannelState state; ChangeReason reason; } change[CORE_MAX_CHANNELS]; //StateChange
    uint8_t changedMask = 0;                 //StateChange: which channels are in change[]
};

//---- Feedback (consumed by AudioVisual) ----
enum class AccessFeedback : uint8_t { SingleBeep, UnlockedBeep, FaultBeep }; //Level conditions (denied, welcomed) are in the snapshot

//---- Snapshot (read by anyone) ----
#define ACCESS_EVENT_CHANGED  (1 << 0) //Snapshot was republished
#define ACCESS_EVENT_OUTBOUND (1 << 1) //Something was put on the outbound or feedback queue

enum class AccessMode : uint8_t { Normal, Welcome };

struct AccessSnapshot {
    AccessMode mode = AccessMode::Normal;
    InputMode inputMode = InputMode::TempPresent;
    ChannelSet channels;
    uint64_t takenAtMs = 0;           //Time the snapshot was published, for hobbsNow()
    bool deviceInUse = false;         //Something is unlocked; do not restart (was deviceInUse)
    bool cardPresent = false;
    bool pendingApproval = false;     //Waiting on a server answer for the present card
    bool welcomingPending = false;    //Welcome request sent for the present card
    bool accessDenied = false;        //Present card was denied
    bool userWelcomed = false;        //Present card was welcomed
    bool userOnOfflineList = false;
    char cardUid[32] = "";
    bool lockWhenIdle = false;
    bool restartWhenUnused = false;
    bool interrupted = false;
    char faultReason[32] = "";
    uint32_t hobbsNow(uint8_t channel, uint64_t nowMs) const {
        return channel < channels.count ? channels.ch[channel].hobbsNow(nowMs) : 0;
    }
};

extern EventGroupHandle_t accessEvents;   //Created in accessManagerInit()
extern QueueHandle_t accessOutbound;      //of AccessOutbound
extern QueueHandle_t accessFeedback;      //of AccessFeedback

//---- Public API (safe from any task) ----
void accessManagerInit(const AccessConfig &config);  //Creates queues/events; call before starting the task or posting
void runAccessManagerLoop(void *pvParameters);       //Task entry point
bool accessPost(const AccessEvent &event);           //Queue an event; false if the inbox is full (payload is NOT freed then)
bool accessPostPayload(AccessEventType type, const char *json); //Copies json to the heap and posts it
bool accessPostCard(bool inserted, const char *uid);
void accessGetSnapshot(AccessSnapshot &out);         //Thread-safe copy
uint32_t accessHobbsNow(uint8_t channel);           //Current hobbs seconds for a channel, computed on demand

//---- Pure logic (no FreeRTOS), used by the task and testable on a host ----
struct AccessEffects {
    bool bumpAccess = false;     //Access mask changed; send to BusDriver
    uint8_t accessMask = 0;
    bool publish = false;        //Snapshot changed
    AccessOutbound outbound[4];  //One-shot messages produced
    uint8_t outboundCount = 0;
    AccessFeedback feedback[4];
    uint8_t feedbackCount = 0;
    void addOutbound(const AccessOutbound &o) { if (outboundCount < 4) outbound[outboundCount++] = o; }
    void addFeedback(AccessFeedback f) { if (feedbackCount < 4) feedback[feedbackCount++] = f; }
};

struct AccessState {
    AccessConfig config;
    AccessSnapshot snap;            //Includes the channels
    bool networkAvailable = true;
    uint8_t lastRequestedMask = 0xFF;
};

//Applies one event. nowMs must be monotonic (millis64()); epoch is the RTC time for the offline list.
void accessHandleEvent(AccessState &state, const AccessEvent &event, uint64_t nowMs, uint32_t epoch, AccessEffects &fx);
//Time-based work (tap expiry); call after waking from a timeout too. Returns ms until the next deadline, or 0 if none.
uint32_t accessHandleTime(AccessState &state, uint64_t nowMs, AccessEffects &fx);