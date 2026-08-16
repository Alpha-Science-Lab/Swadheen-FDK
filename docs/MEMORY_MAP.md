# Swadheen MCU Memory Map

## Overview

The **Swadheen MCU** uses a fully memory-mapped peripheral architecture. All processor-visible peripherals and memories are accessed through the **Wishbone interface** using normal load and store instructions.

The software-visible addresses below are shown as **byte addresses**.

> **Note:** The hardware address constants are internally represented as word addresses and are shifted left by 2 (`<< 2`) to obtain the processor byte address.

The following peripherals are currently implemented:

* LEDs
* Buttons
* UART
* Timer / CLINT-style timer registers
* PWM
* I2C
* I2S
* SPI
* GPIO Ports A, B, and C
* Test register

The following previously mapped components have been **dropped from the current Swadheen MCU design**:

* VGA
* Switches
* Seven-segment display

---

# Memory Map Summary

| Peripheral | Word Base Address | Byte Base Address | Description                         |
| ---------- | ----------------: | ----------------: | ----------------------------------- |
| LEDs       |      `0x00080000` |      `0x00200000` | LED output register                 |
| Buttons    |      `0x00081000` |      `0x00204000` | Push-button input                   |
| Timer      |      `0x00085000` |      `0x00214000` | Machine timer and compare registers |
| PWM        |      `0x00085100` |      `0x00214400` | PWM controller                      |
| I2C        |      `0x00085200` |      `0x00214800` | I2C controller                      |
| I2S        |      `0x00085300` |      `0x00214C00` | I2S controller                      |
| SPI        |      `0x00085400` |      `0x00215000` | SPI controller                      |
| GPIO       |      `0x00085600` |      `0x00215800` | GPIO Ports A, B, and C              |
| UART       |      `0x00084000` |      `0x00210000` | UART controller                     |
| Test       |      `0x00120000` |      `0x00480000` | Simulation/test interface           |

---

# 1. LEDs

**Base Address:** `0x00200000`

| Register |      Address | Access Width | Description              |
| -------- | -----------: | -----------: | ------------------------ |
| LED Data | `0x00200000` |       16-bit | Controls the LED outputs |

Software access:

```c
#define LEDS_ADDRESS ((volatile uint16_t *)0x00200000)
```

Example:

```c
*LEDS_ADDRESS = 0x00FF;
```

---

# 2. Buttons

**Base Address:** `0x00204000`

| Register    |      Address | Access Width | Description              |
| ----------- | -----------: | -----------: | ------------------------ |
| Button Data | `0x00204000` |        8-bit | Reads push-button states |

Defined button bit positions:

| Button | Bit |
| ------ | --: |
| Center |   0 |
| North  |   1 |
| West   |   2 |
| East   |   3 |
| South  |   4 |

Example:

```c
if ((*BUTTONS_ADDRESS >> BUTTON_CENTER_IDX) & 1) {
    // Center button pressed
}
```

---

# 3. UART

**Base Address:** `0x00210000`

The UART provides memory-mapped access to its data buffer and receive/transmit status registers.

| Register    |      Address | Access Width | Description           |
| ----------- | -----------: | -----------: | --------------------- |
| UART Buffer | `0x00210000` |        8-bit | Transmit/receive data |
| RX Status   | `0x00210002` |        8-bit | UART receive status   |
| TX Status   | `0x00210003` |        8-bit | UART transmit status  |

## RX Status Bits

| Bit | Name   | Description                       |
| --: | ------ | --------------------------------- |
|   0 | `ER`   | Receive error                     |
|   1 | `IE`   | Receive interrupt/event indicator |
|   2 | `FULL` | Receive buffer contains data      |

## TX Status Bits

| Bit | Name    | Description                        |
| --: | ------- | ---------------------------------- |
|   0 | `ER`    | Transmit error                     |
|   1 | `IE`    | Transmit interrupt/event indicator |
|   2 | `EMPTY` | Transmit buffer is empty           |

Example:

```c
while (((*UART_TX_STATUS_ADDRESS >> UART_TX_STATUS_IDX_EMPTY) & 1) == 0);

*UART_BUFFER_ADDRESS = 'A';
```

---

# 4. Timer

**Base Address:** `0x00214000`

The timer provides a 64-bit machine timer counter and a 64-bit timer compare register.

| Register        |      Address |  Width | Description                    |
| --------------- | -----------: | -----: | ------------------------------ |
| Status          | `0x00214000` | 32-bit | Timer status/control           |
| `mtime` Low     | `0x00214004` | 32-bit | Lower 32 bits of machine time  |
| `mtime` High    | `0x00214008` | 32-bit | Upper 32 bits of machine time  |
| `mtimecmp` Low  | `0x0021400C` | 32-bit | Lower 32 bits of timer compare |
| `mtimecmp` High | `0x00214010` | 32-bit | Upper 32 bits of timer compare |

---

# 5. PWM

**Base Address:** `0x00214400`

The PWM peripheral provides configurable PWM generation through control, prescaler, period, enable, polarity, and duty-cycle registers.

| Register  |      Address | Description                          |
| --------- | -----------: | ------------------------------------ |
| Control   | `0x00214400` | PWM control                          |
| Prescaler | `0x00214404` | PWM clock divider                    |
| Period    | `0x00214408` | PWM period                           |
| Enable    | `0x0021440C` | Channel enable configuration         |
| Invert    | `0x00214410` | Output polarity inversion            |
| Duty Base | `0x00214414` | Base address of duty-cycle registers |

---

# 6. I2C

**Base Address:** `0x00214800`

| Register    |      Address | Description               |
| ----------- | -----------: | ------------------------- |
| `PRER_LO`   | `0x00214800` | Prescaler low byte        |
| `PRER_HI`   | `0x00214804` | Prescaler high byte       |
| `CTR`       | `0x00214808` | Control register          |
| `RXR/TXR`   | `0x0021480C` | Receive/transmit register |
| `CR/SR`     | `0x00214810` | Command/status register   |
| FIFO Status | `0x00214814` | FIFO status               |

The I2C peripheral follows a register-oriented controller interface. Some registers have different read and write meanings, such as `RXR/TXR` and `CR/SR`.

---

# 7. I2S

**Base Address:** `0x00214C00`

| Register      |      Address | Description                   |
| ------------- | -----------: | ----------------------------- |
| Control       | `0x00214C00` | I2S configuration and control |
| Left Channel  | `0x00214C04` | Left-channel audio sample     |
| Right Channel | `0x00214C08` | Right-channel audio sample    |
| Status        | `0x00214C0C` | I2S status                    |

---

# 8. SPI

**Base Address:** `0x00215000`

| Register    |      Address | Description                  |
| ----------- | -----------: | ---------------------------- |
| Control     | `0x00215000` | SPI configuration and enable |
| Prescaler   | `0x00215004` | SPI clock prescaler          |
| Status      | `0x00215008` | SPI transfer status          |
| Data        | `0x0021500C` | SPI transmit/receive data    |
| Chip Select | `0x00215010` | Slave chip-select control    |

The SPI peripheral is configured through the control and prescaler registers. Data transfers are performed through the data register, while the status register indicates transfer state.

---

# 9. GPIO

**Base Address:** `0x00215800`

The Swadheen MCU currently provides three memory-mapped GPIO ports:

* GPIO Port A
* GPIO Port B
* GPIO Port C

Each port has an identical register structure.

## GPIO Port A

**Base Address:** `0x00215800`

| Offset |      Address | Register |
| -----: | -----------: | -------- |
| `0x00` | `0x00215800` | DATA     |
| `0x04` | `0x00215804` | OUTPUT   |
| `0x08` | `0x00215808` | MODE     |
| `0x0C` | `0x0021580C` | FUNC0    |
| `0x10` | `0x00215810` | FUNC1    |
| `0x14` | `0x00215814` | FUNC2    |
| `0x18` | `0x00215818` | FUNC3    |

## GPIO Port B

**Base Address:** `0x00215820`

| Offset |      Address | Register |
| -----: | -----------: | -------- |
| `0x00` | `0x00215820` | DATA     |
| `0x04` | `0x00215824` | OUTPUT   |
| `0x08` | `0x00215828` | MODE     |
| `0x0C` | `0x0021582C` | FUNC0    |
| `0x10` | `0x00215830` | FUNC1    |
| `0x14` | `0x00215834` | FUNC2    |
| `0x18` | `0x00215838` | FUNC3    |

## GPIO Port C

**Base Address:** `0x00215840`

| Offset |      Address | Register |
| -----: | -----------: | -------- |
| `0x00` | `0x00215840` | DATA     |
| `0x04` | `0x00215844` | OUTPUT   |
| `0x08` | `0x00215848` | MODE     |
| `0x0C` | `0x0021584C` | FUNC0    |
| `0x10` | `0x00215850` | FUNC1    |
| `0x14` | `0x00215854` | FUNC2    |
| `0x18` | `0x00215858` | FUNC3    |

### GPIO Register Description

| Register      | Description                                 |
| ------------- | ------------------------------------------- |
| `DATA`        | Reads GPIO input data                       |
| `OUTPUT`      | GPIO output data register                   |
| `MODE`        | Configures GPIO pin operating mode          |
| `FUNC0-FUNC3` | Alternate peripheral-function configuration |

The function registers allow GPIO pins to be multiplexed between general-purpose operation and supported peripheral functions.

---

# 10. Test Interface

**Base Address:** `0x00480000`

| Register |      Address |  Width | Description               |
| -------- | -----------: | -----: | ------------------------- |
| Test     | `0x00480000` | 32-bit | Test/simulation interface |

This register is primarily intended for software testing, simulation, and verification.

---

# Addressing Model

Swadheen MCU uses a **Wishbone-based memory-mapped I/O architecture**.

From the processor's perspective, peripherals are accessed exactly like memory:

```c
*SPI_CTRL_ADDRESS = control_value;
```

```c
uint32_t status = *SPI_STATUS_ADDRESS;
```

A Wishbone transaction is generated when the processor accesses an address decoded as belonging to a peripheral. The address decoder routes the transaction to the appropriate Wishbone slave, and the selected peripheral responds to the read or write request.

Conceptually:

```text
                    +------------------+
                    |    Hadi-V CPU    |
                    |                  |
                    |   Load / Store   |
                    +--------+---------+
                             |
                             v
                  +----------------------+
                  | Wishbone Interconnect|
                  | / Address Decoder    |
                  +--------+-------------+
                             |
          +------------------+------------------+
          |                  |                  |
          v                  v                  v
       UART Slave         SPI Slave         GPIO Slave
          |                  |                  |
          +------------------+------------------+
                             |
                    Other Memory-Mapped
                      Wishbone Slaves
```

This architecture provides a unified programming model: **software does not require special I/O instructions**. Standard RISC-V load and store instructions are sufficient to communicate with all memory-mapped peripherals.

---

# Dropped Peripherals

The following address regions existed in earlier versions of the platform but are **not part of the current Swadheen MCU peripheral set**:

| Peripheral            | Previous Byte Base Address | Current Status |
| --------------------- | -------------------------: | -------------- |
| Switches              |               `0x00208000` | Dropped        |
| Seven-Segment Display |               `0x0020C000` | Dropped        |
| VGA                   |               `0x00240000` | Dropped        |

These address ranges should currently be treated as **unimplemented/reserved** unless assigned to future peripherals.

---

# Notes for Firmware Developers

1. All peripheral accesses must use `volatile` pointers.
2. The addresses documented here are processor-visible **byte addresses**.
3. Hardware register spacing is generally **4 bytes per 32-bit Wishbone word**.
4. Peripheral access is performed using standard RISC-V load/store instructions.
5. Access widths must match the peripheral register definition where applicable.
6. Software should use the definitions provided in `peripherals.h` rather than hardcoding addresses.
7. Switches, seven-segment display, and VGA are no longer supported in the current Swadheen MCU implementation.

## Recommended Software Include

```c
#include "peripherals.h"
```

This header provides the official software-visible peripheral addresses and register definitions for the Swadheen MCU.
