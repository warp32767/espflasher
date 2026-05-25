# PicoFlasher TCP Client

This is a cross-platform Rust CLI for the ESP32 TCP server (`PFC1` protocol).

## Build

```bash
cd /home/e3xp0/Projects/PicoFlasher/client
cargo build --release
```

## Usage

Default target is the ESP32 SoftAP IP/port (`192.168.4.1:3232`).

```bash
./target/release/picoclient read-nand --out nand.bin
./target/release/picoclient write-nand --input nand.bin
./target/release/picoclient read-emmc --out emmc.bin
./target/release/picoclient write-emmc --input emmc.bin
```

Read NAND (auto-detects flash size from flash config):

```bash
./target/release/picoclient read-nand --out nand.bin
```

Write NAND from a file (file must be 0x210-per-block layout):

```bash
./target/release/picoclient write-nand --start 0 --input nand.bin
```

Read eMMC (auto-detects size from EXT_CSD SEC_COUNT):

```bash
./target/release/picoclient read-emmc --out emmc.bin
```

Override address/timeout:

```bash
./target/release/picoclient --ip 192.168.4.1:3232 --timeout-ms 5000 get-version
```
