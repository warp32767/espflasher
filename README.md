# EspFlasher

Open source XBOX 360 NAND flasher firmware for ESP32

## Wiring:

### SPI or eMMC-over-SPI

| Pico  | Xbox           |
| ----- | -------------- |
|   | SPI_MISO       |
|   | SPI_SS_N       |
|   | SPI_CLK        |
|   | SPI_MOSI       |
|   | SMC_DBG_EN     |
|   | SMC_RST_XDK_N  |
| GND   | GND            |

### POST reading

| Pico  | Xbox           |
| ----- | -------------- |
|   | POST bit 0     |
|   | POST bit 1     |
|   | POST bit 2     |
|   | POST bit 3     |
|   | POST bit 4     |
|   | POST bit 5     |
|   | POST bit 6     |
|   | POST bit 7     |
| GND   | GND            |

### Kernel Debug UART

| Pico           | Xbox          |
| ---------------| ------------- |
|  (UART0_TX) | KER_DBG_RXD   |
|  (UART0_RX) | KER_DBG_TXD   |

### SMC Debug UART

There is a second debug UART in the Southbridge that can be used by the SMC firmware to read/write bytes.
On most Retail PCBs only the TX pin is actually wired to to the debug headers, but RX can be accessed with
some modifications.
For simple debug output from SMC firmware, it is enough to only wire up SMC_DBG_TXD.

| Pico           | Xbox          |
| ---------------| ------------- |
|  (UART1_TX) | SMC_DBG_RXD   |
|  (UART1_RX) | SMC_DBG_TXD   |

## Acknowledgements

- balika011 for the original PicoFlasher
- 15432 for eMMC SPI support
- Hax360 for overhauling the project
- ExposureMG for ESP32 support
- warp32767 for testing and POST reading