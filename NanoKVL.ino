#include <Arduino.h>
#include <EEPROM.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
#define PIN_BUTTON      2   // Active-low pushbutton
#define PIN_LED_RED     4   // Red LED (Fail)
#define PIN_LED_GREEN   6   // Green LED (Success)
#define PIN_3WI_DATA    3   // P25 3-Wire Interface DATA
#define PIN_3WI_SENSE   5   // P25 3-Wire Interface SENSE

// ============================================================================
// P25 3WI & TIA-102 CONSTANTS
// ============================================================================
#define P25_OPCODE_MR_DETECT     0x00
#define P25_OPCODE_KEYLOAD       0x01
#define P25_RSP_ACK              0x00
#define P25_RSP_NACK             0x01

#define BIT_HALF_PERIOD_US       100  // ~5 kbps 3WI bit clocking
#define MAX_SLOTS                16
#define EEPROM_MAGIC             0x5032354BUL // "P25K"

// Supported Algorithm IDs
#define ALGO_DES_OFB             0x81  // 8 bytes (64-bit)
#define ALGO_2_KEY_3DES          0x82  // 16 bytes
#define ALGO_3_KEY_3DES          0x83  // 24 bytes
#define ALGO_AES_256             0x84  // 32 bytes (256-bit)
#define ALGO_ADP                 0xAA  // 8 bytes (40-bit RC4 zero-padded)

// ============================================================================
// EEPROM STORAGE STRUCTURE
// ============================================================================
struct KeySlot {
  uint8_t  valid;        // 0x01 if slot is active
  uint8_t  algoId;       // Algorithm ID (0x81, 0x84, 0xAA, etc.)
  uint16_t keyId;        // KID (0x0001 - 0xFFFF)
  uint16_t ckr;          // Common Key Reference (0x0001 - 0xFFFF)
  uint8_t  keyLength;    // Number of active bytes (8, 16, 24, 32)
  uint8_t  keyData[32];  // Raw key bytes
};

// ============================================================================
// GLOBAL VARIABLES & CLI STATE
// ============================================================================
char cliBuffer[128];
uint8_t cliIndex = 0;

// Button Debounce & Timing
unsigned long btnPressTime = 0;
bool btnActive = false;

// ============================================================================
// 3-WIRE INTERFACE PHYSICAL LAYER ROUTINES
// ============================================================================
inline void wireReleaseData() {
  pinMode(PIN_3WI_DATA, INPUT_PULLUP);
}

inline void wireDriveDataLow() {
  pinMode(PIN_3WI_DATA, OUTPUT);
  digitalWrite(PIN_3WI_DATA, LOW);
}

inline void wireReleaseSense() {
  pinMode(PIN_3WI_SENSE, INPUT_PULLUP);
}

inline void wireDriveSenseLow() {
  pinMode(PIN_3WI_SENSE, OUTPUT);
  digitalWrite(PIN_3WI_SENSE, LOW);
}

inline uint8_t wireReadData() {
  return digitalRead(PIN_3WI_DATA);
}

inline uint8_t wireReadSense() {
  return digitalRead(PIN_3WI_SENSE);
}

void wireInit() {
  wireReleaseData();
  wireReleaseSense();
}

// Bit-bang 1 byte out over 3WI (MSB first)
void wireTxByte(uint8_t b) {
  for (int8_t i = 7; i >= 0; i--) {
    if ((b >> i) & 0x01) {
      wireReleaseData();
    } else {
      wireDriveDataLow();
    }
    delayMicroseconds(BIT_HALF_PERIOD_US);
    wireDriveSenseLow();
    delayMicroseconds(BIT_HALF_PERIOD_US);
    wireReleaseSense();
  }
}

// Bit-bang 1 byte in from 3WI (MSB first)
uint8_t wireRxByte() {
  uint8_t b = 0;
  for (int8_t i = 7; i >= 0; i--) {
    wireReleaseData();
    delayMicroseconds(BIT_HALF_PERIOD_US);
    wireDriveSenseLow();
    delayMicroseconds(BIT_HALF_PERIOD_US / 2);
    if (wireReadData()) {
      b |= (1 << i);
    }
    delayMicroseconds(BIT_HALF_PERIOD_US / 2);
    wireReleaseSense();
  }
  return b;
}

// ============================================================================
// TIA-102 CRC-16 GENERATOR
// ============================================================================
uint16_t calculateCrc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0x0000;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// ============================================================================
// P25 TRANSACTION ROUTINES
// ============================================================================
bool sendP25Packet(uint8_t opcode, const uint8_t *payload, uint8_t payloadLen, uint8_t &outResponseCode) {
  uint8_t txBuf[64];
  txBuf[0] = opcode;
  txBuf[1] = payloadLen;
  
  if (payloadLen > 0 && payload != nullptr) {
    memcpy(&txBuf[2], payload, payloadLen);
  }
  
  uint16_t crc = calculateCrc16(txBuf, payloadLen + 2);
  txBuf[payloadLen + 2] = (crc >> 8) & 0xFF;
  txBuf[payloadLen + 3] = crc & 0xFF;

  // Wake radio / assert sense line
  wireDriveSenseLow();
  delay(15);
  wireReleaseSense();
  delayMicroseconds(250);

  // Transmit frame
  uint8_t totalTx = payloadLen + 4;
  for (uint8_t i = 0; i < totalTx; i++) {
    wireTxByte(txBuf[i]);
  }

  // Await response packet (Opcode, Len, Data..., CRC16)
  delay(25); 
  
  uint8_t rxOpcode = wireRxByte();
  uint8_t rxLen    = wireRxByte();
  
  if (rxLen > 32) {
    outResponseCode = 0xFF; // Frame sync/length anomaly
    return false;
  }

  uint8_t rxPayload[34];
  for (uint8_t i = 0; i < rxLen; i++) {
    rxPayload[i] = wireRxByte();
  }
  
  uint8_t crcH = wireRxByte();
  uint8_t crcL = wireRxByte();
  (void)crcH;
  (void)crcL;

  outResponseCode = (rxLen > 0) ? rxPayload[0] : rxOpcode;
  return (outResponseCode == P25_RSP_ACK);
}

// Ping radio using P25 MR_DETECT
bool p25PingRadio() {
  uint8_t resp = 0xFF;
  return sendP25Packet(P25_OPCODE_MR_DETECT, nullptr, 0, resp);
}

// Assemble and push Keyload Frame
bool p25Keyload(const KeySlot &slot) {
  uint8_t frame[40];
  uint8_t idx = 0;

  frame[idx++] = slot.algoId;
  frame[idx++] = (slot.keyId >> 8) & 0xFF;
  frame[idx++] = slot.keyId & 0xFF;
  frame[idx++] = (slot.ckr >> 8) & 0xFF;
  frame[idx++] = slot.ckr & 0xFF;
  frame[idx++] = slot.keyLength;

  memcpy(&frame[idx], slot.keyData, slot.keyLength);
  idx += slot.keyLength;

  uint8_t resp = 0xFF;
  return sendP25Packet(P25_OPCODE_KEYLOAD, frame, idx, resp);
}

// ============================================================================
// EEPROM STORAGE MANAGEMENT
// ============================================================================
uint16_t getSlotAddress(uint8_t slotNum) {
  return sizeof(uint32_t) + ((slotNum - 1) * sizeof(KeySlot));
}

void loadSlot(uint8_t slotNum, KeySlot &slot) {
  EEPROM.get(getSlotAddress(slotNum), slot);
}

void writeSlot(uint8_t slotNum, const KeySlot &slot) {
  EEPROM.put(getSlotAddress(slotNum), slot);
}

void initStorage() {
  uint32_t magic = 0;
  EEPROM.get(0, magic);
  if (magic != EEPROM_MAGIC) {
    for (int i = 0; i < 1024; i++) EEPROM.write(i, 0x00);
    magic = EEPROM_MAGIC;
    EEPROM.put(0, magic);
  }
}

void clearAllSlots() {
  for (uint8_t i = 1; i <= MAX_SLOTS; i++) {
    KeySlot s;
    memset(&s, 0, sizeof(KeySlot));
    writeSlot(i, s);
  }
}

// ============================================================================
// UI & INDICATION UTILITIES
// ============================================================================
void signalStatus(bool success) {
  digitalWrite(PIN_LED_GREEN, success ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   success ? LOW : HIGH);
  delay(1200);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED,   LOW);
}

void triggerPushSlot(uint8_t slotNum) {
  if (slotNum < 1 || slotNum > MAX_SLOTS) {
    Serial.println(F("ERR: Invalid slot"));
    signalStatus(false);
    return;
  }

  KeySlot slot;
  loadSlot(slotNum, slot);

  if (!slot.valid) {
    Serial.print(F("ERR: Slot "));
    Serial.print(slotNum);
    Serial.println(F(" is empty"));
    signalStatus(false);
    return;
  }

  Serial.print(F("Pushing slot "));
  Serial.print(slotNum);
  Serial.println(F(" to radio..."));

  bool ok = p25Keyload(slot);
  signalStatus(ok);
  Serial.println(ok ? F("SUCCESS: Key accepted by radio") : F("FAIL: Radio rejected or no response"));
}

void triggerPing() {
  Serial.println(F("Pinging radio..."));
  bool ok = p25PingRadio();
  signalStatus(ok);
  Serial.println(ok ? F("SUCCESS: Radio detected") : F("FAIL: Radio not found / 3WI timeout"));
}

// ============================================================================
// SERIAL CLI COMMAND HANDLERS
// ============================================================================
uint8_t parseHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0xFF;
}

bool parseHexBytes(const char *src, uint8_t *dst, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    uint8_t hi = parseHexNibble(src[i * 2]);
    uint8_t lo = parseHexNibble(src[i * 2 + 1]);
    if (hi == 0xFF || lo == 0xFF) return false;
    dst[i] = (hi << 4) | lo;
  }
  return true;
}

void printKeySlot(uint8_t num) {
  KeySlot s;
  loadSlot(num, s);
  Serial.print(F("[Slot "));
  if (num < 10) Serial.print('0');
  Serial.print(num);
  Serial.print(F("] "));

  if (!s.valid) {
    Serial.println(F("-- EMPTY --"));
    return;
  }

  Serial.print(F("Algo: 0x"));
  if (s.algoId < 0x10) Serial.print('0');
  Serial.print(s.algoId, HEX);

  Serial.print(F(" | KID: 0x"));
  Serial.print(s.keyId, HEX);

  Serial.print(F(" | CKR: "));
  Serial.print(s.ckr);

  Serial.print(F(" | Key: "));
  for (uint8_t i = 0; i < s.keyLength; i++) {
    if (s.keyData[i] < 0x10) Serial.print('0');
    Serial.print(s.keyData[i], HEX);
  }
  Serial.println();
}

void handleCliCommand(char *cmd) {
  while (*cmd == ' ') cmd++; // Skip leading spaces

  if (strncmp(cmd, "list", 4) == 0) {
    Serial.println(F("--- Configured EEPROM Slots ---"));
    for (uint8_t i = 1; i <= MAX_SLOTS; i++) {
      printKeySlot(i);
    }
  } 
  else if (strncmp(cmd, "ping", 4) == 0) {
    triggerPing();
  }
  else if (strncmp(cmd, "push", 4) == 0) {
    int slot = atoi(cmd + 5);
    triggerPushSlot(slot);
  }
  else if (strncmp(cmd, "eraseall", 8) == 0) {
    clearAllSlots();
    Serial.println(F("All slots cleared from EEPROM."));
  }
  else if (strncmp(cmd, "erase", 5) == 0) {
    int slot = atoi(cmd + 6);
    if (slot >= 1 && slot <= MAX_SLOTS) {
      KeySlot s;
      memset(&s, 0, sizeof(KeySlot));
      writeSlot(slot, s);
      Serial.print(F("Slot "));
      Serial.print(slot);
      Serial.println(F(" erased."));
    } else {
      Serial.println(F("ERR: Invalid slot"));
    }
  }
  else if (strncmp(cmd, "set", 3) == 0) {
    // Syntax: set <slot> <algoHex> <kidHex> <ckrDec> <keyHex>
    char *p = cmd + 3;
    char *token = strtok(p, " ");
    if (!token) goto syntax_err;
    uint8_t slotNum = atoi(token);

    token = strtok(nullptr, " ");
    if (!token) goto syntax_err;
    uint8_t algo = strtol(token, nullptr, 16);

    token = strtok(nullptr, " ");
    if (!token) goto syntax_err;
    uint16_t kid = strtol(token, nullptr, 16);

    token = strtok(nullptr, " ");
    if (!token) goto syntax_err;
    uint16_t ckr = atoi(token);

    token = strtok(nullptr, " ");
    if (!token) goto syntax_err;
    uint8_t hexStrLen = strlen(token);
    uint8_t keyBytes = hexStrLen / 2;

    if (slotNum < 1 || slotNum > MAX_SLOTS || keyBytes > 32 || (hexStrLen % 2) != 0) {
      Serial.println(F("ERR: Range or length mismatch"));
      return;
    }

    KeySlot s;
    memset(&s, 0, sizeof(KeySlot));
    s.valid = 0x01;
    s.algoId = algo;
    s.keyId = kid;
    s.ckr = ckr;
    s.keyLength = keyBytes;

    if (!parseHexBytes(token, s.keyData, keyBytes)) {
      Serial.println(F("ERR: Malformed key hex characters"));
      return;
    }

    writeSlot(slotNum, s);
    Serial.print(F("Configured and saved to slot "));
    Serial.println(slotNum);
    return;

syntax_err:
    Serial.println(F("ERR Syntax: set <slot 1-16> <algoHex> <kidHex> <ckrDec> <keyHex>"));
  }
  else {
    Serial.println(F("Available Commands:"));
    Serial.println(F("  list                                   - Show all stored slots"));
    Serial.println(F("  set <slot> <algo> <kid> <ckr> <hexkey> - Store key"));
    Serial.println(F("  push <slot>                            - Push keyload packet to radio"));
    Serial.println(F("  ping                                   - Check 3WI radio connectivity"));
    Serial.println(F("  erase <slot>                           - Clear specific slot"));
    Serial.println(F("  eraseall                               - Wipe all EEPROM key slots"));
  }
}

// ============================================================================
// ARDUINO SETUP & MAIN LOOP
// ============================================================================
void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);

  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);

  wireInit();
  initStorage();

  Serial.begin(115200);
  while (!Serial && millis() < 1500); // Allow USB initialization

  Serial.println(F("\n======================================"));
  Serial.println(F("  P25 3-Wire Standalone Keyloader CLI "));
  Serial.println(F("  Ready. Type 'help' for commands.    "));
  Serial.println(F("======================================"));
}

void loop() {
  // Non-blocking serial CLI reader
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (cliIndex > 0) {
        cliBuffer[cliIndex] = '\0';
        handleCliCommand(cliBuffer);
        cliIndex = 0;
      }
    } else if (cliIndex < (sizeof(cliBuffer) - 1)) {
      cliBuffer[cliIndex++] = c;
    }
  }

  // Non-blocking Pushbutton Handling
  bool btnState = (digitalRead(PIN_BUTTON) == LOW);

  if (btnState && !btnActive) {
    // Button just pressed down
    btnActive = true;
    btnPressTime = millis();
  } else if (!btnState && btnActive) {
    // Button just released
    unsigned long duration = millis() - btnPressTime;
    btnActive = false;

    if (duration >= 1000) {
      // Long press: push slot 1
      triggerPushSlot(1);
    } else if (duration >= 50) {
      // Short press: ping radio
      triggerPing();
    }
  }
}