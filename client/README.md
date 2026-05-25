# PicoFlasher TCP Client

This is a cross-platform Rust CLI for the ESP32 TCP server (`PFC1` protocol).

## Build

```bash
cd /home/e3xp0/Projects/PicoFlasher/client
cargo build --release
```

## Usage

Default target is the ESP32 SoftAP IP/port (`192.168.4.255:3232`).

```bash
./target/release/picoclient get-version
./target/release/picoclient stop-smc
./target/release/picoclient get-flash-config
```

Dump NAND (stream mode; requires `start=0`):

```bash
./target/release/picoclient nand-dump --start 0 --count 0x10000 --out nand.bin --use-stream
```

Flash NAND from a file (file must be 0x210-per-block layout):

```bash
./target/release/picoclient stop-smc
./target/release/picoclient nand-flash --start 0 --input nand.bin
```

Dump eMMC (stream mode; requires `start=0`):

```bash
./target/release/picoclient emmc-init
./target/release/picoclient emmc-dump --start 0 --count 0x10000 --out emmc.bin --use-stream
```

Override address/timeout:

```bash
./target/release/picoclient --ip 192.168.4.1:3232 --timeout-ms 5000 get-version
```
