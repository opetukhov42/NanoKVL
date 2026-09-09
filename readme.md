# Standalone Arduino Nano P25 Keyloader (NanoKVL)

An autonomous, pocket-sized P25 (Project 25) Manual Rekeying Key Fill Device (KFD / KVL) implemented directly on an **Arduino Nano** (ATmega328P).

This project eliminates the need for the original Windows desktop application (.NET 4.0 / C# GUI). All TIA-102.AACD-A 3-Wire Interface (3WI) framing, CRC-16 generation, key packaging, and Mobile Radio (MR) detection run natively on the microcontroller. Keys are stored safely in internal non-volatile EEPROM.

---

## Features

- **100% Standalone Operation:** Push cryptographic keys directly to a radio using an onboard tactile button—no computer or host required in the field.
- **Onboard Dual-Function Pushbutton:**
  - **Short Press (< 1 sec):** Pings the connected radio (`Detect MR`) and verifies 3WI connectivity.
  - **Long Press (> 1 sec):** Immediately pushes the encryption key from **Slot 1** to the radio.
- **Bi-Color Status Indication:** Dedicated Green (Success/ACK) and Red (Error/NACK/Timeout) LEDs.
- **Internal Non-Volatile EEPROM Key Storage:** 16 dedicated slots storing Key ID (KID), Common Key Reference (CKR), Algorithm ID, and raw key material (up to 256 bits).
- **Interactive Serial CLI:** Provision, inspect, and test keys using any standard terminal emulator (PuTTY, Tera Term, Minicom, or Arduino Serial Monitor) at 115200 baud over USB.
- **Supported Encryption Algorithms:**
  - **AES-256** (`0x84`, 32 bytes)
  - **DES-OFB** (`0x81`, 8 bytes)
  - **DES-XL** (`0x9F`, 8 bytes)
  - **Motorola ADP / RC4** (`0xAA`, 8 bytes / 40-bit padded)

---

## Hardware Interface & Schematic

### Hardware Safety & Level Shifting
The P25 3-Wire Interface (3WI) is an active-low, open-collector bus. Subscriber radios typically use **3.3V or lower logic levels** on their accessory side connectors. 

> ⚠️ **CRITICAL HARDWARE WARNING:** Never connect 5V Arduino GPIO pins directly to modern radio lines without an open-collector/open-drain switching circuit or level converter. Doing so risks permanent electrical damage to the radio's internal encryption module or baseband interface ASIC.

### Reference Hardware Schematic
Build the hardware adapter using the original verified `KFDtool` open-collector interface circuit:

![KFD Minimal Schematic](https://raw.githubusercontent.com/omahacommsys/KFDtool/master/doc/pic/basic_hw_schematic.png)

### Open-Collector Circuit Description (Hardwired Sense)

```text
                           +------------------------+
                           |      Arduino Nano      |
                           |       (ATmega328P)     |
                           +------------------------+
                               |     |    |    |
     [Pushbutton]              |     |    |    |
        +---\ --- GND <--------+ D2  |    |    |
                                     |    |    |
     [Red LED]                       |    |    |
        +---[ 330Ω ]--->|--- GND <---+ D4 |    |
                                          |    |
     [Green LED]                          |    |
        +---[ 330Ω ]--->|--- GND <--------+ D6 |
                                               |
     === Safe Open-Collector 3-Wire Interface ===

      Arduino D3 (DATA OUT) ----[ 2.2kΩ ]----+----> Base (Q1: 2N3904 / NPN)
                                             |
                                            [E] Emitter ---> GND
                                             |
                                            [C] Collector --+----> Radio 3WI DATA Line
                                                            |
                                      Radio Pullup / +3.3V -+

      Arduino D5 (DATA IN)  <------------------------------------+
                                                                 |
                                       (Connected to Collector Q1 / Radio DATA)

      GND                   -----------------+-------------------> Radio GND
                                             |
                                             +-------------------> Radio 3WI SENSE Line (Hardwired Low)
```

*(Note: Hardwiring the Radio SENSE line to GND puts the radio into Keyload mode as soon as the cable is physically connected, eliminating the need for a dedicated Arduino pin to drive it).*

---

## Pin Mapping Table

| Nano Pin | Function | Peripheral Connection |
| :--- | :--- | :--- |
| **D2** | Pushbutton Input | Momentary N.O. switch connected to GND (Internal `INPUT_PULLUP` enabled) |
| **D4** | Red LED Output | Anode via 220Ω–330Ω resistor; Cathode to GND |
| **D6** | Green LED Output | Anode via 220Ω–330Ω resistor; Cathode to GND |
| **D3** | 3WI DATA OUT | Transmit 3WI data to radio via open-collector transistor circuit |
| **D5** | 3WI DATA IN | Receive 3WI data from radio (connected to transistor collector) |
| **N/A** | 3WI SENSE Line | **Hardwired to GND** |
| **GND** | Ground Reference | Common ground to Radio Hirose / MX / TRS connector shield |

---

## Installation & Flashing

1. Connect your Arduino Nano via USB.
2. (Optional) If you modified the code to remove the `D7` sense logic, ensure `wireDriveSenseLow()` and `wireReleaseSense()` are empty or removed. The original code will also work fine as-is (toggling an unconnected pin).
3. Open `NanoKVL.ino` in the Arduino IDE.
4. Under **Tools**:
   - **Board:** "Arduino Nano"
   - **Processor:** "ATmega328P" (or "ATmega328P (Old Bootloader)" depending on your board)
   - **Port:** Select the appropriate USB-serial COM port.
5. Click **Upload**.

---

## Serial Terminal Configuration

Connect to the Nano using any serial terminal emulator:
- **Baud Rate:** `115200`
- **Data Bits:** `8`
- **Parity:** `None`
- **Stop Bits:** `1`
- **Line Ending:** Both `CR` and `LF` (`\r\n`)

---

## CLI Command Reference

### 1. `list`
Prints all 16 EEPROM storage slots, showing algorithm ID, KID, CKR, and hex key contents.

### 2. `set <slot> <algoHex> <kidHex> <ckrDec> <keyHex>`
Stores a key into the specified EEPROM slot (1 to 16).

### 3. `push <slot>`
Pushes the key in the chosen slot over the 3WI bus to the connected radio.

### 4. `ping`
Transmits an MR Detect frame to check if a radio is connected and responding.

### 5. `erase <slot>`
Zeroes out and invalidates a single slot in EEPROM.

### 6. `eraseall`
Wipes all 16 key slots in EEPROM.

---

## Hardware Operation (Field Mode)

Once configured, the device requires no computer connection. It can be powered with a 5V USB battery bank or a 9V battery into the `VIN` pin.

| User Action | Device Operation | Green LED | Red LED |
| :--- | :--- | :--- | :--- |
| **Short Press (< 1.0s)** | Pings radio via 3WI (`Detect MR`) | **Solid ON (1.2s)** on ACK | **Solid ON (1.2s)** on timeout/fail |
| **Long Press (> 1.0s)** | Loads key stored in **Slot 1** to radio | **Solid ON (1.2s)** on ACK | **Solid ON (1.2s)** on NACK/fail |

---

## Upstream References & Attribution

- Based on protocol reverse-engineering and hardware specifications from the [KFDtool](https://github.com/KFDtool/KFDtool) and [gatekeep/KFDToolSW_NET4.0](https://github.com/gatekeep/KFDToolSW_NET4.0) projects.
- Implements Manual Rekeying protocols compliant with **TIA-102.AACD-A**.

---

## License

This project is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version**.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
