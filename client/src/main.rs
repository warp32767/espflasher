mod cli;
mod pfc;

use std::fs::File;
use std::io::{Read, Write};
use std::time::Duration;

use anyhow::{bail, Context, Result};
use clap::Parser;

use crate::cli::{Cli, Command};
use crate::pfc::{
	cmd_payload, Client, CMD_EMMC_DETECT, CMD_EMMC_GET_EXT_CSD, CMD_EMMC_INIT, CMD_EMMC_READ,
	CMD_EMMC_READ_STREAM, CMD_EMMC_WRITE_MULTI, CMD_GET_FLASH_CONFIG,
	CMD_GET_VERSION, CMD_READ_FLASH, CMD_READ_FLASH_STREAM, CMD_SET_SMC_WORKAROUND, CMD_STOP_SMC,
	CMD_WRITE_FLASH_MULTI,
};

fn main() -> Result<()> {
	let cli = Cli::parse();
	let timeout = Duration::from_millis(cli.timeout_ms);
	let (mut client, resolved) = Client::connect(&cli.addr, timeout)
		.with_context(|| format!("failed to connect to {}", cli.addr))?;

	eprintln!("connected to {resolved}");

	match cli.command {
		Command::ReadNand { out, start, count } => {
			let (flash_config, blocks_total) = prepare_nand(&mut client)?;
			let blocks = count.unwrap_or(blocks_total.saturating_sub(start));
			eprintln!("flash_config=0x{flash_config:08x} blocks={blocks} start={start}");
			read_nand(&mut client, out, start, blocks)?;
			println!("ok");
		}
		Command::WriteNand { input, start } => {
			let (flash_config, blocks_total) = prepare_nand(&mut client)?;
			eprintln!("flash_config=0x{flash_config:08x} start={start} max_blocks={blocks_total}");
			write_nand(&mut client, input, start)?;
			println!("ok");
		}
		Command::ReadEmmc { out, start, count } => {
			let blocks_total = prepare_emmc(&mut client)?;
			let blocks = count.unwrap_or(blocks_total.saturating_sub(start));
			eprintln!("emmc_blocks={blocks} start={start}");
			read_emmc(&mut client, out, start, blocks)?;
			println!("ok");
		}
		Command::WriteEmmc { input, start } => {
			let blocks_total = prepare_emmc(&mut client)?;
			eprintln!("start={start} max_blocks={blocks_total}");
			write_emmc(&mut client, input, start)?;
			println!("ok");
		}
	}

	Ok(())
}

fn prepare_nand(client: &mut Client) -> Result<(u32, u32)> {
	let _ver = client.cmd_u32(CMD_GET_VERSION, 0)?;
	let _ = client.cmd_u32(CMD_SET_SMC_WORKAROUND, 0)?;
	client.request_response(&cmd_payload(CMD_STOP_SMC, 0))?;
	std::thread::sleep(Duration::from_millis(500));

	let flash_config = client.cmd_u32(CMD_GET_FLASH_CONFIG, 0)?;
	if flash_config == 0 || flash_config == 0xFFFF_FFFF {
		bail!("console not found (flash_config=0x{flash_config:08x})");
	}

	let flash_size_bytes = flash_size_from_config(flash_config)
		.ok_or_else(|| anyhow::anyhow!("unknown flash size for flash_config=0x{flash_config:08x}"))?;
	let blocks = (flash_size_bytes / 512) as u32;
	Ok((flash_config, blocks))
}

fn flash_size_from_config(flash_config: u32) -> Option<usize> {
	let major = (flash_config >> 17) & 3;
	let minor = (flash_config >> 4) & 3;

	let size_mb = if major >= 1 {
		match minor {
			0 => {
				if major != 1 {
					16
				} else {
					return None;
				}
			}
			1 => {
				if major != 1 {
					64
				} else {
					16
				}
			}
			2 | 3 => {
				let a = (flash_config >> 19) & 0x3;
				let b = (flash_config >> 21) & 0xF;
				8usize.checked_shl((a + b) as u32)?
			}
			_ => return None,
		}
	} else {
		8usize.checked_shl(minor as u32)?
	};

	Some(size_mb * 1024 * 1024)
}

fn read_nand(client: &mut Client, out: std::path::PathBuf, start: u32, count: u32) -> Result<()> {
	let mut f = File::create(out).context("open output")?;

	if start == 0 {
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

			if (i & 0xFF) == 0 {
				eprintln!("read {}/{} blocks", i + 1, count);
			}
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

			if (i & 0xFF) == 0 {
				eprintln!("read {}/{} blocks", i + 1, count);
			}
		}
	}

	Ok(())
}

fn write_nand(client: &mut Client, input: std::path::PathBuf, start: u32) -> Result<()> {
	let mut buf = vec![];
	File::open(input)
		.context("open input")?
		.read_to_end(&mut buf)
		.context("read input")?;

	if buf.len() % 0x210 != 0 {
		bail!("input size must be a multiple of 0x210 (got 0x{:x})", buf.len());
	}

	let blocks = (buf.len() / 0x210) as u32;
	let mut i = 0u32;
	while i < blocks {
		let remaining = blocks - i;
		let chunk_blocks = remaining.min(8);
		let lba = start + i;

		let mut payload = Vec::with_capacity(5 + 2 + (chunk_blocks as usize) * 0x210);
		payload.push(CMD_WRITE_FLASH_MULTI);
		payload.extend_from_slice(&lba.to_le_bytes());
		payload.extend_from_slice(&(chunk_blocks as u16).to_le_bytes());
		let off = (i as usize) * 0x210;
		let end = off + (chunk_blocks as usize) * 0x210;
		payload.extend_from_slice(&buf[off..end]);

		client.send_request(&payload)?;
		let frame = client.recv_response()?;
		if frame.payload.len() != 8 {
			bail!("expected 8-byte response at lba {lba}, got {}", frame.payload.len());
		}
		let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
		let idx = u32::from_le_bytes(frame.payload[4..8].try_into().unwrap());
		if ret != 0 {
			bail!("write failed at lba {}: 0x{ret:08x}", lba + idx);
		}

		i += chunk_blocks;
		eprintln!("written {}/{} blocks", i, blocks);
	}

	Ok(())
}

fn prepare_emmc(client: &mut Client) -> Result<u32> {
	let _ver = client.cmd_u32(CMD_GET_VERSION, 0)?;
	let _ = client.cmd_u32(CMD_SET_SMC_WORKAROUND, 0)?;
	client.request_response(&cmd_payload(CMD_STOP_SMC, 0))?;
	std::thread::sleep(Duration::from_millis(500));

	let detect = client.request_response(&cmd_payload(CMD_EMMC_DETECT, 0))?;
	if detect.payload.len() != 1 || detect.payload[0] == 0 {
		bail!("eMMC not detected");
	}

	let ret = client.cmd_u32(CMD_EMMC_INIT, 0)?;
	if ret != 0 {
		bail!("EMMC_INIT failed: {ret}");
	}

	let ext = client.request_response(&cmd_payload(CMD_EMMC_GET_EXT_CSD, 0))?;
	if ext.payload.len() != 512 {
		bail!("unexpected EXT_CSD length {}", ext.payload.len());
	}

	let sec_count = u32::from_le_bytes(ext.payload[212..216].try_into().unwrap());
	if sec_count == 0 {
		bail!("invalid EXT_CSD SEC_COUNT=0");
	}
	Ok(sec_count)
}

fn read_emmc(client: &mut Client, out: std::path::PathBuf, start: u32, count: u32) -> Result<()> {
	let mut f = File::create(out).context("open output")?;

	if start == 0 {
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

			if (i & 0xFF) == 0 {
				eprintln!("read {}/{} blocks", i + 1, count);
			}
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

			if (i & 0xFF) == 0 {
				eprintln!("read {}/{} blocks", i + 1, count);
			}
		}
	}

	Ok(())
}

fn write_emmc(client: &mut Client, input: std::path::PathBuf, start: u32) -> Result<()> {
	let mut buf = vec![];
	File::open(input)
		.context("open input")?
		.read_to_end(&mut buf)
		.context("read input")?;

	if buf.len() % 0x200 != 0 {
		bail!("input size must be a multiple of 0x200 (got 0x{:x})", buf.len());
	}

	let blocks = (buf.len() / 0x200) as u32;
	let mut i = 0u32;
	while i < blocks {
		let remaining = blocks - i;
		let chunk_blocks = remaining.min(16);
		let lba = start + i;

		let mut payload = Vec::with_capacity(5 + 2 + (chunk_blocks as usize) * 0x200);
		payload.push(CMD_EMMC_WRITE_MULTI);
		payload.extend_from_slice(&lba.to_le_bytes());
		payload.extend_from_slice(&(chunk_blocks as u16).to_le_bytes());
		let off = (i as usize) * 0x200;
		let end = off + (chunk_blocks as usize) * 0x200;
		payload.extend_from_slice(&buf[off..end]);

		client.send_request(&payload)?;
		let frame = client.recv_response()?;
		if frame.payload.len() != 8 {
			bail!("expected 8-byte response at lba {lba}, got {}", frame.payload.len());
		}
		let ret = u32::from_le_bytes(frame.payload[0..4].try_into().unwrap());
		let idx = u32::from_le_bytes(frame.payload[4..8].try_into().unwrap());
		if ret != 0 {
			bail!("write failed at lba {}: {ret}", lba + idx);
		}

		i += chunk_blocks;
		eprintln!("written {}/{} blocks", i, blocks);
	}

	Ok(())
}
