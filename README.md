![PicoFlasher logo](https://raw.githubusercontent.com/X360Tools/PicoFlasher/master/picoflasher.png)

# PicoFlasher

Open source XBOX 360 NAND flasher firmware for Raspberry Pi Pico

## Wiring:

### Nand Flash or eMMC

| Pico  | Xbox           |
| ----- | -------------- |
| GP16  | SPI_MISO       |
| GP17  | SPI_SS_N       |
| GP18  | SPI_CLK        |
| GP19  | SPI_MOSI       |
| GP20  | SMC_DBG_EN     |
| GP21  | SMC_RST_XDK_N  |
| GND   | GND            |

### Kernel Debug UART

| Pico           | Xbox          |
| ---------------| ------------- |
| GP0 (UART0_TX) | KER_DBG_RXD   |
| GP1 (UART0_RX) | KER_DBG_TXD   |

### SMC Debug UART

There is a second debug UART in the Southbridge that can be used by the SMC firmware to read/write bytes.
On most Retail PCBs only the TX pin is actually wired to to the debug headers, but RX can be accessed with
some modifications.
For simple debug output from SMC firmware, it is enough to only wire up SMC_DBG_TXD.

| Pico           | Xbox          |
| ---------------| ------------- |
| GP4 (UART1_TX) | SMC_DBG_RXD   |
| GP5 (UART1_RX) | SMC_DBG_TXD   |

### ISD12xx Audible Feedback IC

| Signal   | Pico | Trinity | Corona   |
| -------- | ---- | ------- | -------- |
| SPI_RDY  | GP11 | FT2V4   | J2C2-A10 |
| SPI_MISO | GP12 | FT2R7   | J2C2-B11 |
| SPI_SS_N | GP13 | FT2R6   | J2C2-A11 |
| SPI_CLK  | GP14 | FT2T4   | J2C2-A8  |
| SPI_MOSI | GP15 | FT2T5   | J2C2-B8  |

## Acknowledgements

- balika011 for the original PicoFlasher
- 15432 for eMMC SPI support
