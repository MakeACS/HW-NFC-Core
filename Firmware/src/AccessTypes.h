//Access Types

//Shared types for access control: channel states, reasons, and the per-channel struct.
//Everything here is plain data (no String), so it can be copied between tasks and through queues.

//The enums are internal. The API expects exact legacy strings (i.e. "ALWAYS_ON"), so convert
//with toApiString() only at the moment a message leaves the device, and with fromApiString()
//as soon as one arrives. Display text for the UI is separate (toDisplayText()) so that
//rewording the UI can never change the protocol.

#pragma once

#include <Arduino.h>
#include "Globals.h" //For CORE_MAX_CHANNELS

enum class ChannelState : uint8_t { Unknown, Idle, Unlocked, AlwaysOn, LockedOut, Fault };

enum class ChangeReason : uint8_t {
    Unknown, Local, Commanded, ServerCommanded, Authed, CardRemoved, LockTemp, Fault, OverTemp, IntegrityFail
};

//Why a card was not granted access. Server is free text from the API, held in Channel::denyText.
enum class DenyReason : uint8_t { None, Server, NoNetwork, BadState, UnreadableCard };

enum class InputMode : uint8_t { TempPresent, Insert };

enum class InterruptResponse : uint8_t { Fault, LockTemp, Idle, Message };

//API string conversions. fromApiString() returns Unknown/default for anything unrecognised.
const char *toApiString(ChannelState state);
const char *toApiString(ChangeReason reason);
const char *toApiString(InputMode mode);
ChannelState fromApiString(const char *text, ChannelState fallback = ChannelState::Unknown);
ChangeReason changeReasonFromApiString(const char *text);
InputMode inputModeFromApiString(const char *text, InputMode fallback = InputMode::TempPresent);
InterruptResponse interruptResponseFromApiString(const char *text, InterruptResponse fallback = InterruptResponse::Fault);

//The server does not recognise LOCK_TEMP as a reason, it is reported as LOCAL.
const char *toReportedApiString(ChangeReason reason);

//Human-readable text for the screen and config frontend. denyText is only used for DenyReason::Server.
const char *toDisplayText(DenyReason reason, const char *denyText);

struct Channel {
    ChannelState state = ChannelState::Unknown;
    ChannelState lastState = ChannelState::Unknown;     //For change detection
    ChannelState reportedState = ChannelState::Unknown; //What the server last heard
    ChangeReason changeReason = ChangeReason::Unknown;
    DenyReason denyReason = DenyReason::None;
    char denyText[64] = "";
    uint32_t tapDurationMs = 0;
    uint64_t tapExpiresAtMs = 0;

    //Hobbs is the time in an accessible state. Nothing ticks it: the total is folded in when the
    //channel leaves the accessible state, and hobbsNow() adds the period still in progress.
    uint32_t hobbsSeconds = 0;
    uint64_t accessSinceMs = 0; //0 when not currently accessible

    bool hasAccess() const { return state == ChannelState::Unlocked || state == ChannelState::AlwaysOn; }
    uint32_t hobbsNow(uint64_t nowMs) const {
        return hobbsSeconds + (accessSinceMs ? (uint32_t)((nowMs - accessSinceMs) / 1000) : 0);
    }
};

struct ChannelSet {
    static constexpr uint8_t kMax = CORE_MAX_CHANNELS;
    uint8_t count = 0;
    Channel ch[kMax];

    uint8_t accessMask() const {
        uint8_t mask = 0;
        for (uint8_t i = 0; i < count; i++) if (ch[i].hasAccess()) mask |= (1 << i);
        return mask;
    }
    bool any(ChannelState state) const {
        for (uint8_t i = 0; i < count; i++) if (ch[i].state == state) return true;
        return false;
    }
    bool anyAccess() const {
        for (uint8_t i = 0; i < count; i++) if (ch[i].hasAccess()) return true;
        return false;
    }
};
