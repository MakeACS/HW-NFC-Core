#include "CardReader.h"
#include "Globals.h"
#include "TaggedSerial.h"
#include <SPI.h>
#if CORE_NFC_READER_MFRC630
#include <mfrc630.h>
#endif
#if CORE_NFC_READER_PN532
#include <Adafruit_PN532.h>
#endif

namespace {
TaggedSerial<decltype(::Serial)> cardReaderSerial(::Serial, "[card] ");
}

#define Serial cardReaderSerial

CardInfo card;
EventGroupHandle_t cardEvents = nullptr;

#if CORE_NFC_READER_PN532
//The PN532 is driven with bit-banged software SPI, so it never touches the hardware SPI bus.
static Adafruit_PN532 nfc(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_NFC_CS);
#endif

#if CORE_NFC_READER_MFRC630
//HAL callbacks for the mfrc630 library. The shared hardware SPI bus is only touched between
//select and unselect, which hold the SPIClass transaction lock, so other devices
//(i.e. the W5500 Ethernet) cannot interleave with a register access.
void mfrc630_SPI_transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) {
  for (uint16_t i = 0; i < len; i++){
    rx[i] = SPI.transfer(tx[i]);
  }
}

void mfrc630_SPI_select() {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0)); //Takes the bus lock
  digitalWrite(PIN_NFC_CS, LOW);
}

void mfrc630_SPI_unselect() {
  digitalWrite(PIN_NFC_CS, HIGH);
  SPI.endTransaction(); //Releases the bus lock
}
#endif

//Requires SPI.begin() to have been called already, since the bus is shared and owned by the main controller.
void cardReaderInit(){
  cardEvents = xEventGroupCreate();

  //Park the chip select high right away, so the reader ignores traffic meant for other devices on the bus.
  pinMode(PIN_NFC_CS, OUTPUT);
  digitalWrite(PIN_NFC_CS, HIGH);

#if CORE_NFC_READER_MFRC630
  mfrc630_AN1102_recommended_registers(MFRC630_PROTO_ISO14443A_106_MILLER_MANCHESTER);
  mfrc630_write_reg(0x28, 0x8E);
  mfrc630_write_reg(0x29, 0x15);
  mfrc630_write_reg(0x2A, 0x11);
  mfrc630_write_reg(0x2B, 0x06);
#elif CORE_NFC_READER_PN532
  pinMode(PIN_NFC_POWER, OUTPUT);
  pinMode(PIN_NFC_RST, OUTPUT);
  digitalWrite(PIN_NFC_POWER, HIGH);
  digitalWrite(PIN_NFC_RST, HIGH);
  nfc.begin();
  nfc.SAMConfig();
#endif
}

//Update card first, then set the event bit, so waiters always see current data.
static void cardInserted(const String &uid, bool readFailed){
  card.UID = uid;
  card.readFailed = readFailed;
  card.present = true;
  xEventGroupSetBits(cardEvents, CARD_EVENT_INSERTED);
}

static void cardRemoved(){
  card.present = false;
  card.UID = "";
  card.readFailed = false;
  xEventGroupSetBits(cardEvents, CARD_EVENT_REMOVED);
}

static String readNfcCardId(){
  //Let's first ask the NFC reader for the card (if one is there)
  
  String ReturnedID = "";
  
#if CORE_NFC_READER_MFRC630
  uint16_t atqa = mfrc630_iso14443a_REQA();

  if (atqa != 0) {  // Are there any cards that answered?
    uint8_t sak;
    uint8_t uid[10] = {0};  // uids are maximum of 10 bytes long.

    // Select the card and discover its uid.
    uint8_t uid_len = mfrc630_iso14443a_select(uid, &sak);
    if (uid_len != 0) {  // did we get a uid?
      for (uint8_t i=0; i<uid_len; i++){
      if (uid[i] < 16){
          ReturnedID += "0"; 
          ReturnedID += String(uid[i], HEX);
        } else {
          ReturnedID += String(uid[i], HEX);;
        }
      }
      ReturnedID.toLowerCase();
      //Serial.print(F("Found uid :"));
      //Serial.println(ReturnedID);
    } else {
      Serial.print("Could not determine uid, perhaps some cards don't play");
      Serial.print(" well with the other cards? Or too many collisions?\n");
      ReturnedID = "";
    }
  } else{
    //Did not find a uid
    //Serial.println(F("Didn't find a card."));
    ReturnedID = "";
  }
#elif CORE_NFC_READER_PN532
  //1. Check the reader is working. If not, restart it.
  byte NFCTryCount = 0;
  uint32_t versionData = nfc.getFirmwareVersion();
  while(!versionData && NFCTryCount < 3) {
    //PN532 not responding, restart it.
    digitalWrite(PIN_NFC_RST, LOW);
    delay(10);
    digitalWrite(PIN_NFC_RST, HIGH);
    delay(10);
    nfc.wakeup();
    nfc.setPassiveActivationRetries(0xFF);
    delay(10);
    NFCTryCount++;
    versionData = nfc.getFirmwareVersion();
  }
  if(NFCTryCount >= 3){
    Serial.println(F("PN532 failed to respond after 3 restart attempts. Cannot read card."));
    ReturnedID = "";
    if(!mqttState.messageToSend){
      //Send a message
      mqttState.statusMessage = "Possible malfunction of NFC reader, please check the device.";
      mqttState.messageToSend = true;
    }
  } else{
    //Do a normal card read:
    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
      for (uint8_t index = 0; index < uidLength; index++) {
        if (uid[index] < 16) {
          ReturnedID += "0";
        }
        ReturnedID += String(uid[index], HEX);
      }
      ReturnedID.toLowerCase();
    }
  }
#endif //END PN532

  if(ReturnedID.length() > 0){
    return ReturnedID;
  } else{
    //We did not find a card due to errors or no card present.
    return "";
  }
}


//Reads the UID twice if needed, to make sure there really isn't a card.
static String readUidWithRetry(){
  String uid = readNfcCardId();
  if(uid == ""){
    uid = readNfcCardId();
  }
  return uid;
}

static bool detectSwitchesClosed(){
#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
  return !digitalRead(PIN_DET_1) && !digitalRead(PIN_DET_2);
#else
  return frontendCardDetect1 && frontendCardDetect2;
#endif
}

static bool detectSwitchesOpen(){
#if CORE_HAS_LOCAL_CHANNEL_OUTPUTS
  return digitalRead(PIN_DET_1) || digitalRead(PIN_DET_2);
#else
  return !frontendCardDetect1 || !frontendCardDetect2;
#endif
}

void runCardReaderLoop(void *pvParameters){
  Serial.println(F("runCardReaderLoop Started."));
  while(1){
    vTaskDelay(pdMS_TO_TICKS(50));

    if(inputMode == "TEMP_PRESENT"){
      //We always scan for a card in TEMP_PRESENT mode, and the card is "removed" when its UID changes or disappears.
      const String detectedUid = readUidWithRetry();
      if(card.present && !detectedUid.equalsIgnoreCase(card.UID)){
        Serial.print(F("Card "));
        Serial.print(card.UID);
        Serial.print(F(" replaced with "));
        Serial.println(detectedUid);
        cardRemoved();
      } else if(!card.present && detectedUid.length() > 2){
        cardInserted(detectedUid, false);
      }
    } else{ //INSERT
      //In INSERT mode, presence is based on the detect switches.
      if(!card.present && detectSwitchesClosed()){
        const String uid = readUidWithRetry();
        if(uid.length() > 2){
          cardInserted(uid, false);
        } else{
          //A card is present but we cannot read it, likely a bad card or a bad read.
          cardInserted("", true);
        }
      } else if(card.present && detectSwitchesOpen()){
        cardRemoved();
      }
    }
  }
}