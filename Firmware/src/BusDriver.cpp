#include "BusDriver.h"
#include "Globals.h"
#include "AccessManager.h"

struct BusInfo bus;
EventGroupHandle_t busEvents = NULL;

static QueueHandle_t busCommands = NULL;
static TaskHandle_t busTaskHandle = NULL;

#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
static const uint8_t BUS_GPIO_PINS[] = { PIN_GPIO_1, PIN_GPIO_2, PIN_GPIO_3, PIN_GPIO_4 };
static const uint8_t BUS_IODIR_PINS[] = { PIN_IODIR_1, PIN_IODIR_2, PIN_IODIR_3, PIN_IODIR_4 };
#define BUS_GPIO_COUNT 4
#endif

//Preserve the old asymmetric debounce: assert after 5 consecutive low samples; while high, decrement
//the counter and clear once it reaches 2. Mid-debounce samples are spaced by INT_POLL_MS.
#define INT_DEBOUNCE_ASSERT 5
#define INT_DEBOUNCE_CLEAR  2
#define INT_POLL_MS 50 //Re-sample period while the line is mid-debounce (matches the old 50ms state loop)
#define FRONTEND_REFRESH_MS 50 //2.3.2: the access state is re-sent to the frontend this often, as the old state loop did

//The ISR only wakes the task; all logic runs in task context.
static void IRAM_ATTR interruptPinISR() {
    BaseType_t woken = pdFALSE;
    if (busTaskHandle) vTaskNotifyGiveFromISR(busTaskHandle, &woken);
    portYIELD_FROM_ISR(woken);
}

void busDriverInit() {
    busEvents = xEventGroupCreate();
    busCommands = xQueueCreate(8, sizeof(BusCommand));

#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
    pinMode(PIN_ACCESS, OUTPUT);
    digitalWrite(PIN_ACCESS, LOW);
    for (uint8_t i = 0; i < BUS_GPIO_COUNT; i++) {
        pinMode(BUS_IODIR_PINS[i], OUTPUT);
        digitalWrite(BUS_IODIR_PINS[i], HIGH);
        pinMode(BUS_GPIO_PINS[i], OUTPUT); //Level is only written when the pin is an access channel
    }
#endif
    pinMode(PIN_INTERRUPT, INPUT_PULLUP);
}

bool busSendCommand(const BusCommand &cmd) {
    if (!busCommands) return false;
    bool ok = xQueueSend(busCommands, &cmd, 0) == pdTRUE;
    if (ok && busTaskHandle) xTaskNotifyGive(busTaskHandle); //Wake the loop immediately
    return ok;
}

bool busSetAccess(uint8_t mask, uint8_t channelCount) {
    BusCommand cmd = { BUS_CMD_SET_ACCESS, mask, channelCount };
    return busSendCommand(cmd);
}

//Number of channels that currently use access control. GPIO n is an access output only when
//n < accessChannels and there is more than one channel (a single channel uses PIN_ACCESS alone).
//Any other GPIO is left alone, so it can be used for something else.
static uint8_t accessChannels = 0;

static void writeAccess(uint8_t mask) {
#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
    digitalWrite(PIN_ACCESS, mask != 0 ? HIGH : LOW);
    if (accessChannels > 1) {
        for (uint8_t i = 0; i < BUS_GPIO_COUNT && i < accessChannels; i++) {
            digitalWrite(BUS_GPIO_PINS[i], (mask >> i) & 1 ? HIGH : LOW);
        }
    }
#else
    frontendSend(mask != 0 ? "S 1" : "S 0");
#endif
}

static void applyAccess(uint8_t mask, uint8_t channelCount) {
#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
    //GPIOs that stop being access channels are driven to the safe (off) level once, then released.
    if (accessChannels > 1) {
        uint8_t firstReleased = channelCount > 1 ? channelCount : 0;
        for (uint8_t i = firstReleased; i < BUS_GPIO_COUNT && i < accessChannels; i++) {
            digitalWrite(BUS_GPIO_PINS[i], LOW);
        }
    }
#endif
    accessChannels = channelCount;
    writeAccess(mask);
    bus.accessMask = mask;
    xEventGroupSetBits(busEvents, BUS_EVENT_ACCESS_APPLIED);
}
void runBusDriverLoop(void *pvParameters) {
    busTaskHandle = xTaskGetCurrentTaskHandle();
    attachInterrupt(digitalPinToInterrupt(PIN_INTERRUPT), interruptPinISR, CHANGE);

    uint8_t interruptCount = 0;

    while (1) {
        //Sleep until the ISR or a command wakes us; while debouncing, also wake on a short timer.
        bool debouncing = bus.interrupted ? digitalRead(PIN_INTERRUPT) : interruptCount != 0;
        TickType_t wait = debouncing ? pdMS_TO_TICKS(INT_POLL_MS) : portMAX_DELAY;
#if !CORE_HAS_LOCAL_CHANNEL_OUTPUTS
        wait = min(wait, pdMS_TO_TICKS(FRONTEND_REFRESH_MS));
#endif
        ulTaskNotifyTake(pdTRUE, wait);

        BusCommand cmd;
        while (xQueueReceive(busCommands, &cmd, 0) == pdTRUE) {
            if (cmd.type == BUS_CMD_SET_ACCESS) applyAccess(cmd.mask, cmd.channelCount);
        }

#if !CORE_HAS_LOCAL_CHANNEL_OUTPUTS
        //Access is a UART message on 2.3.2, so keep it refreshed in case the frontend missed it.
        writeAccess(bus.accessMask);
#endif

        bool lineLow = !digitalRead(PIN_INTERRUPT);
        if (!bus.interrupted) {
            interruptCount = lineLow ? interruptCount + 1 : 0;
            if (interruptCount >= INT_DEBOUNCE_ASSERT) {
                bus.interrupted = true;
                xEventGroupSetBits(busEvents, BUS_EVENT_INTERRUPT_ASSERTED);
                AccessEvent e; e.type = AccessEventType::InterruptAsserted; accessPost(e);
            }
        } else if (!lineLow) {
            if (interruptCount > 0) interruptCount--;
            if (interruptCount <= INT_DEBOUNCE_CLEAR) {
                interruptCount = 0;
                bus.interrupted = false;
                xEventGroupSetBits(busEvents, BUS_EVENT_INTERRUPT_CLEARED);
                AccessEvent e; e.type = AccessEventType::InterruptCleared; accessPost(e);
            }
        }
    }
}
