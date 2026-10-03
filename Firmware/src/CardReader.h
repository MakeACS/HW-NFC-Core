//Card Reader

//This code is responsible for reading and monitoring the presented card
//and informing other tasks about the card.

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

//Supports different readers based on what is defined in /variants
// CORE_NFC_READER_MFRC630 for MFRC630 found in V3.X Core. Requires pin definitions:
    // PIN_NFC_CS, PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_NFC_PDWN, PIN_NFC_IRQ
    // NFC_IRQ and NFC_PDWN are not currently used, but may be in the future
// CORE_NFC_READER_PN532 for PN532 found in V2.X Core. Requires pin definitions:
    // PIN_NFC_CS, PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_NFC_RST, PIN_NFC_IRQ, PIN_NFC_POWER
    // NFC_IRQ is not currently used, but may be in the future
    // NFC_POWER does not go to the NFC reader itself, but rather shuts down the power to the NFC module (for hard power restarts) 

//Scope:
// * Manage the attached card reader, for reading of cards.
// * Manage card presence detection via switches (when in INSERT mode)
// * Generate events for other tasks to use (i.e. CARD_INSERTED, CARD_REMOVED)
// * Keep a struct of card info up to date for other tasks to use.

//Struct for other tasks to use:
struct CardInfo {
	bool present = false;           //True while a card is present (in INSERT mode)
	String UID = "";                //UID of the current card, i.e. the current user (was currentUserUid)
    bool readFailed = false;        //Set to true to indicate we could not read the present card - maybe not a real NFC?
};

extern struct CardInfo card;

//Events for other tasks to use:
//Other tasks wait on these with xEventGroupWaitBits(cardEvents, ...).
//Only the lower 24 bits are usable on ESP32 (FreeRTOS reserves the top 8).
#define CARD_EVENT_INSERTED (1 << 0) //A card was found; card.UID is valid (or readFailed is true)
#define CARD_EVENT_REMOVED  (1 << 1) //The card was removed; card.present is false

extern EventGroupHandle_t cardEvents; //Created in cardReaderInit() (CardReader.cpp)

void cardReaderInit();                //Creates cardEvents; call before starting any task that waits on it
void runCardReaderLoop(void *pvParameters); //Task entry point, matches runMachineStateLoop()