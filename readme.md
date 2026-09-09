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

## Hardware Schematic

### Original Minimal Circuit Diagram
The minimal 3-wire interface circuit from the original `KFDtool` / `KFD-AVR` project:

![KFD Minimal Schematic](https://raw.githubusercontent.com/omahacommsys/KFDtool/master/doc/pic/basic_hw_schematic.png)

### Complete Standalone Wiring Diagram

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
     === P25 3-Wire Radio Interface ===        |
                                               |
      D3 (DATA)  --------+---------------------+
                         |
                       [10kΩ] (Pullup to +5V)
                         |
                         +------------------------------------> Radio DATA Line (TRS Tip)
                                                                 
      D5 (SENSE) --------+---------------------+
                         |                     |
                       [10kΩ] (Pullup to +5V)  |
                         |                     |
                         +---------------------+--------------> Radio SENSE Line (TRS Ring)

      GND        ---------------------------------------------> Radio GND (TRS Shield)
```

> **Note on Pullups:** While the microcontroller has internal pullups configured, external 4.7kΩ to 10kΩ pull-up resistors to 5V on the DATA and SENSE lines are strongly recommended to ensure clean rise times over longer keyload cables.

---

## Pin Mapping Table

| Nano Pin | Function | Peripheral Connection |
| :--- | :--- | :--- |
| **D2** | Pushbutton Input | Momentary N.O. switch connected to GND (Internal `INPUT_PULLUP` enabled) |
| **D4** | Red LED Output | Anode via 220Ω–330Ω resistor; Cathode to GND |
| **D6** | Green LED Output | Anode via 220Ω–330Ω resistor; Cathode to GND |
| **D3** | 3WI DATA | Bi-directional open-collector/drain data line (Radio Keyfill pin) |
| **D5** | 3WI SENSE | Bi-directional sense line (Radio Keyload/Sense pin) |
| **GND** | Ground Reference | Common ground to Radio Hirose / MX / TRS connector shield |

---

## Installation & Flashing

1. Connect your Arduino Nano via USB.
2. Open `NanoKVL.ino` in the Arduino IDE.
3. Under **Tools**:
   - **Board:** "Arduino Nano"
   - **Processor:** "ATmega328P" (or "ATmega328P (Old Bootloader)" depending on your board)
   - **Port:** Select the appropriate USB-serial COM port.
4. Click **Upload**.

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
```text
> list
--- Configured EEPROM Slots ---
[Slot 01] Algo: 0xAA | KID: 0x0001 | CKR: 1 | Key: 0123456789ABCDEF
[Slot 02] Algo: 0x84 | KID: 0x0002 | CKR: 2 | Key: 00010203...
[Slot 03] -- EMPTY --
...
```

### 2. `set <slot> <algoHex> <kidHex> <ckrDec> <keyHex>`
Stores a key into the specified EEPROM slot (1 to 16).
- `<slot>`: Slot number (`1` - `16`).
- `<algoHex>`: Algorithm identifier in hex (`84` for AES-256, `81` for DES-OFB, `AA` for ADP).
- `<kidHex>`: Key ID in hex (`0001` - `FFFF`).
- `<ckrDec>`: Common Key Reference in decimal (`1` - `4096`).
- `<keyHex>`: Raw cryptographic key string in hexadecimal.

**Examples:**
- **Store an ADP Key in Slot 1:**
  ```text
  set 1 aa 0001 1 0123456789ABCDEF
  ```
- **Store an AES-256 Key in Slot 2:**
  ```text
  set 2 84 0002 2 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
  ```

### 3. `push <slot>`
Pushes the key in the chosen slot over the 3WI bus to the connected radio.
```text
> push 1
Pushing slot 1 to radio...
SUCCESS: Key accepted by radio
```

### 4. `ping`
Transmits an MR Detect frame to check if a radio is connected and responding.
```text
> ping
Pinging radio...
SUCCESS: Radio detected
```

### 5. `erase <slot>`
Zeroes out and invalidates a single slot in EEPROM.
```text
> erase 1
Slot 1 erased.
```

### 6. `eraseall`
Wipes all 16 key slots in EEPROM.
```text
> eraseall
All slots cleared from EEPROM.
```

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
