#include "AccessTypes.h"

//Wire format. These strings are part of the server API, do not reword them.
struct StateName { ChannelState value; const char *text; };
static const StateName stateNames[] = {
    { ChannelState::Unknown,   "UNKNOWN" },
    { ChannelState::Idle,      "IDLE" },
    { ChannelState::Unlocked,  "UNLOCKED" },
    { ChannelState::AlwaysOn,  "ALWAYS_ON" },
    { ChannelState::LockedOut, "LOCKED_OUT" },
    { ChannelState::Fault,     "FAULT" },
};

struct ReasonName { ChangeReason value; const char *text; };
static const ReasonName reasonNames[] = {
    { ChangeReason::Unknown,         "UNKNOWN" },
    { ChangeReason::Local,           "LOCAL" },
    { ChangeReason::Commanded,       "COMMANDED" },
    { ChangeReason::ServerCommanded, "SERVER_COMMANDED" },
    { ChangeReason::Authed,          "AUTHED" },
    { ChangeReason::CardRemoved,     "CARD_REMOVED" },
    { ChangeReason::LockTemp,        "LOCK_TEMP" },
    { ChangeReason::Fault,           "FAULT" },
    { ChangeReason::OverTemp,        "OVER_TEMP" },
    { ChangeReason::IntegrityFail,   "INTEGRITY_FAIL" },
};

const char *toApiString(ChannelState state) {
    for (const StateName &n : stateNames) if (n.value == state) return n.text;
    return "UNKNOWN";
}

const char *toApiString(ChangeReason reason) {
    for (const ReasonName &n : reasonNames) if (n.value == reason) return n.text;
    return "UNKNOWN";
}

const char *toApiString(InputMode mode) {
    return mode == InputMode::Insert ? "INSERT" : "TEMP_PRESENT";
}

const char *toReportedApiString(ChangeReason reason) {
    return reason == ChangeReason::LockTemp ? "LOCAL" : toApiString(reason);
}

ChannelState fromApiString(const char *text, ChannelState fallback) {
    if (!text) return fallback;
    for (const StateName &n : stateNames) if (strcmp(n.text, text) == 0) return n.value;
    return fallback;
}

ChangeReason changeReasonFromApiString(const char *text) {
    if (!text) return ChangeReason::Unknown;
    for (const ReasonName &n : reasonNames) if (strcmp(n.text, text) == 0) return n.value;
    return ChangeReason::Unknown;
}

InputMode inputModeFromApiString(const char *text, InputMode fallback) {
    if (!text) return fallback;
    if (strcmp(text, "INSERT") == 0) return InputMode::Insert;
    if (strcmp(text, "TEMP_PRESENT") == 0) return InputMode::TempPresent;
    return fallback;
}

InterruptResponse interruptResponseFromApiString(const char *text, InterruptResponse fallback) {
    if (!text) return fallback;
    if (strcmp(text, "FAULT") == 0) return InterruptResponse::Fault;
    if (strcmp(text, "LOCK_TEMP") == 0) return InterruptResponse::LockTemp;
    if (strcmp(text, "IDLE") == 0) return InterruptResponse::Idle;
    if (strcmp(text, "MESSAGE") == 0) return InterruptResponse::Message;
    return fallback;
}

const char *toDisplayText(DenyReason reason, const char *denyText) {
    switch (reason) {
        case DenyReason::Server:         return denyText ? denyText : "";
        case DenyReason::NoNetwork:      return "No network, try again soon or talk to staff.";
        case DenyReason::BadState:       return "Incorrect state, machine must be in \"IDLE\" mode to activate.";
        case DenyReason::UnreadableCard: return "Card could not be read, try again or talk to staff.";
        case DenyReason::None:           break;
    }
    return "";
}
