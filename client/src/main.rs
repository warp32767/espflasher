mod cli;
mod pfc;

use std::fs::File;
use std::io::{Read, Write};
use std::time::Duration;

use anyhow::{bail, Context, Result};
use clap::Parser;

use crate::cli::{Cli, Command};
use crate::pfc::{
	cmd_payload, Client, CMD_EMMC_DETECT, CMD_EMMC_INIT, CMD_EMMC_READ, CMD_EMMC_READ_STREAM,
	CMD_EMMC_WRITE, CMD_GET_FLASH_CONFIG, CMD_GET_VERSION, CMD_READ_FLASH, CMD_READ_FLASH_STREAM,
	CMD_START_SMC, CMD_STOP_SMC, CMD_WRITE_FLASH,
};

fn main() -> Result<()> {
	let cli = Cli::parse();
	let timeout = Duration::from_millis(cli.timeout_ms);
	let (mut client, resolved) = Client::connect(&cli.addr, timeout)
		.with_context(|| format!("failed to connect to {}", cli.addr))?;

	eprintln!("connected to {resolved}");

	match cli.command {
		Command::GetVersion => {
			let ver = client.cmd_u32(CMD_GET_VERSION, 0)?;
			println!("{ver}");
		}
		Command::StopSmc => {
			client.request_response(&cmd_payload(CMD_STOP_SMC, 0))?;
			println!("ok");
		}
		Command::StartSmc => {
			client.request_response(&cmd_payload(CMD_START_SMC, 0))?;
			println!("ok");
		}
		Command::GetFlashConfig => {
			let fc = client.cmd_u32(CMD_GET_FLASH_CONFIG, 0)?;
			println!("0x{fc:08x}");
		}
		Command::NandRead { lba, out } => {
			let frame = client.request_response(&cmd_payload(CMD_READ_FLASH, lba))?;
			if frame.payload.len() < 4 {
				bail!("short response");
			}
			let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
			if ret != 0 {
				bail!("read failed: 0x{ret:08x}");
			}
			if frame.payload.len() != 4 + 0x210 {
				bail!("expected {} bytes, got {}", 4 + 0x210, frame.payload.len());
			}
			let mut f = File::create(out).context("open output")?;
			f.write_all(&frame.payload[4..]).context("write output")?;
			println!("ok");
		}
		Command::NandWrite { lba, input } => {
			let mut buf = vec![];
			File::open(input)
				.context("open input")?
				.read_to_end(&mut buf)
				.context("read input")?;
			if buf.len() != 0x210 {
				bail!("expected 0x210 bytes input, got 0x{:x}", buf.len());
			}

			client.send_cmd(CMD_WRITE_FLASH, lba, &buf)?;
			let frame = client.recv_response()?;
			if frame.payload.len() != 4 {
				bail!("expected 4-byte response, got {}", frame.payload.len());
			}
			let ret = u32::from_le_bytes(frame.payload[..4].try_into().unwrap());
			if ret != 0 {
				bail!("write failed: 0x{ret:08x}");
			}
			println!("ok");
		}
		Command::NandDump {
			start,
			count,
			out,
			use_stream,
		} => {
			let mut f = File::create(out).context("open output")?;
			if use_stream {
				if start != 0 {
					bail!("stream mode only supports start=0 with current ESP server");
				}
				client.send_request(&cmd_payload(CMD_READ_FLASH_STREAM, count))?;
				for i in 0..count {
					let frame = client.recv_response()?;
					if frame.payload.len() < 4 {
						bail!("short response at block {i}");
					}
					let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
					if ret != 0 {
						bail!("read failed at block {i}: 0x{ret:08x}");
					}
					if frame.payload.len() != 4 + 0x210 {
						bail!("bad payload size at block {i}: {}", frame.payload.len());
					}
					f.write_all(&frame.payload[4..]).context("write output")?;
				}
			} else {
				for i in 0..count {
					let lba = start + i;
					let frame = client.request_response(&cmd_payload(CMD_READ_FLASH, lba))?;
					if frame.payload.len() < 4 {
						bail!("short response at lba {lba}");
					}
					let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
					if ret != 0 {
						bail!("read failed at lba {lba}: 0x{ret:08x}");
					}
					if frame.payload.len() != 4 + 0x210 {
						bail!("bad payload size at lba {lba}: {}", frame.payload.len());
					}
					f.write_all(&frame.payload[4..]).context("write output")?;
				}
			}
			println!("ok");
		}
		Command::NandFlash { start, input } => {
			let mut f = File::open(input).context("open input")?;
			let mut buf = vec![];
			f.read_to_end(&mut buf).context("read input")?;

			if buf.len() % 0x210 != 0 {
				bail!("input size must be a multiple of 0x210 (got 0x{:x})", buf.len());
			}

			let blocks = (buf.len() / 0x210) as u32;
			for i in 0..blocks {
				let lba = start + i;
				let off = (i as usize) * 0x210;
				let chunk = &buf[off..off + 0x210];

				client.send_cmd(CMD_WRITE_FLASH, lba, chunk)?;
				let frame = client.recv_response()?;
				if frame.payload.len() != 4 {
					bail!("expected 4-byte response at lba {lba}, got {}", frame.payload.len());
				}
				let ret = u32::from_le_bytes(frame.payload[..4].try_into().unwrap());
				if ret != 0 {
					bail!("write failed at lba {lba}: 0x{ret:08x}");
				}

				if (i & 0xFF) == 0 {
					eprintln!("written {}/{} blocks", i + 1, blocks);
				}
			}

			println!("ok");
		}
		Command::EmmcDetect => {
			let frame = client.request_response(&cmd_payload(CMD_EMMC_DETECT, 0))?;
			if frame.payload.len() != 1 {
				bail!("expected 1-byte response, got {}", frame.payload.len());
			}
			println!("{}", frame.payload[0]);
		}
		Command::EmmcInit => {
			let ret = client.cmd_u32(CMD_EMMC_INIT, 0)?;
			println!("{ret}");
		}
		Command::EmmcRead { lba, out } => {
			let frame = client.request_response(&cmd_payload(CMD_EMMC_READ, lba))?;
			if frame.payload.len() < 4 {
				bail!("short response");
			}
			let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
			if ret != 0 {
				bail!("read failed: {ret}");
			}
			if frame.payload.len() != 4 + 0x200 {
				bail!("expected {} bytes, got {}", 4 + 0x200, frame.payload.len());
			}
			let mut f = File::create(out).context("open output")?;
			f.write_all(&frame.payload[4..]).context("write output")?;
			println!("ok");
		}
		Command::EmmcWrite { lba, input } => {
			let mut buf = vec![];
			File::open(input)
				.context("open input")?
				.read_to_end(&mut buf)
				.context("read input")?;
			if buf.len() != 0x200 {
				bail!("expected 0x200 bytes input, got 0x{:x}", buf.len());
			}

			client.send_cmd(CMD_EMMC_WRITE, lba, &buf)?;
			let frame = client.recv_response()?;
			if frame.payload.len() != 4 {
				bail!("expected 4-byte response, got {}", frame.payload.len());
			}
			let ret = u32::from_le_bytes(frame.payload[..4].try_into().unwrap());
			if ret != 0 {
				bail!("write failed: {ret}");
			}
			println!("ok");
		}
		Command::EmmcDump {
			start,
			count,
			out,
			use_stream,
		} => {
			let mut f = File::create(out).context("open output")?;
			if use_stream {
				if start != 0 {
					bail!("stream mode only supports start=0 with current ESP server");
				}
				client.send_request(&cmd_payload(CMD_EMMC_READ_STREAM, count))?;
				for i in 0..count {
					let frame = client.recv_response()?;
					if frame.payload.len() < 4 {
						bail!("short response at block {i}");
					}
					let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
					if ret != 0 {
						bail!("read failed at block {i}: {ret}");
					}
					if frame.payload.len() != 4 + 0x200 {
						bail!("bad payload size at block {i}: {}", frame.payload.len());
					}
					f.write_all(&frame.payload[4..]).context("write output")?;
				}
			} else {
				for i in 0..count {
					let lba = start + i;
					let frame = client.request_response(&cmd_payload(CMD_EMMC_READ, lba))?;
					if frame.payload.len() < 4 {
						bail!("short response at lba {lba}");
					}
					let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
					if ret != 0 {
						bail!("read failed at lba {lba}: {ret}");
					}
					if frame.payload.len() != 4 + 0x200 {
						bail!("bad payload size at lba {lba}: {}", frame.payload.len());
					}
					f.write_all(&frame.payload[4..]).context("write output")?;
				}
			}
			println!("ok");
		}
	}

	Ok(())
}
