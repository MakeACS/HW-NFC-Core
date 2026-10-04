// Bus Driver

// This task is responsible for handling everything related to the MakeACS Bus
// OneWire, Access Control, GPIO state, interrupts, communication, etc.

//Scope:
// * Own the bus pins: PIN_INTERRUPT, PIN_ACCESS, PIN_GPIO_1..4, PIN_IODIR_1..4 (and later OneWire).
// * Debounce the interrupt line and report *edges* (asserted/cleared) as events.
// * Apply requested access output state to the hardware (PIN_ACCESS / GPIOs, or the frontend on V2).
// * Keep a struct of bus info up to date for other tasks to read.
// * Does NOT decide what an interrupt means. The response policy (FAULT, LOCK_TEMP, IDLE, MESSAGE)
//   lives in the AccessManager, which reacts to the edges posted to its inbox.

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

//Struct for other tasks to read (written only by the BusDriver task):
struct BusInfo {
    volatile bool interrupted = false;     //Debounced interrupt state (was isInterrupted)
    volatile uint8_t accessMask = 0;       //Access outputs currently applied; bit n = channel n
    //Future will have more info on things like OneWire
};

extern struct BusInfo bus;

//Events produced by the BusDriver. Other tasks wait on these with xEventGroupWaitBits(busEvents, ...).
//Only the lower 24 bits are usable on ESP32 (FreeRTOS reserves the top 8).
#define BUS_EVENT_INTERRUPT_ASSERTED (1 << 0) //Debounced interrupt went active; bus.interrupted is true
#define BUS_EVENT_INTERRUPT_CLEARED  (1 << 1) //Debounced interrupt released; bus.interrupted is false
#define BUS_EVENT_ACCESS_APPLIED     (1 << 2) //A busSetAccess() request was applied; bus.accessMask is current
//Future: BUS_EVENT_ONEWIRE_CHANGED, BUS_EVENT_BUS_FAULT, ...

extern EventGroupHandle_t busEvents; //Created in busDriverInit() (BusDriver.cpp)

//Commands consumed by the BusDriver. Other tasks never touch bus pins directly; they send commands.
enum BusCommandType : uint8_t {
    BUS_CMD_SET_ACCESS,   //mask = full desired access state (bit n = channel n); channelCount = channels using access control
    //Future: BUS_CMD_SCAN_ONEWIRE, BUS_CMD_RESEAL, ...
};

struct BusCommand {
    BusCommandType type;
    uint8_t mask;
    uint8_t channelCount; //Bus GPIOs beyond this are not access outputs and are never written for access
};

//Public API (safe to call from any task; none block on hardware):
void busDriverInit();                       //Creates busEvents + command queue, configures pins; call before starting any task that uses them
void runBusDriverLoop(void *pvParameters);  //Task entry point, matches the other tasks
bool busSendCommand(const BusCommand &cmd); //Queue a command for the driver; false if the queue is full
bool busSetAccess(uint8_t mask, uint8_t channelCount); //Convenience wrapper for BUS_CMD_SET_ACCESS
