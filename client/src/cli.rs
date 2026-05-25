use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser, Debug)]
#[command(name = "picoclient")]
#[command(about = "TCP client for PicoFlasher ESP32 server", long_about = None)]
pub struct Cli {
	#[arg(long = "ip", alias = "addr", default_value = "192.168.4.255:3232")]
	pub addr: String,

	#[arg(long, default_value = "3000")]
	pub timeout_ms: u64,

	#[command(subcommand)]
	pub command: Command,
}

#[derive(Subcommand, Debug)]
pub enum Command {
	GetVersion,
	StopSmc,
	StartSmc,
	GetFlashConfig,

	NandRead {
		#[arg(long)]
		lba: u32,

		#[arg(long)]
		out: PathBuf,
	},

	NandWrite {
		#[arg(long)]
		lba: u32,

		#[arg(long)]
		input: PathBuf,
	},

	NandDump {
		#[arg(long)]
		start: u32,

		#[arg(long)]
		count: u32,

		#[arg(long)]
		out: PathBuf,

		#[arg(long, default_value_t = false)]
		use_stream: bool,
	},

	NandFlash {
		#[arg(long)]
		start: u32,

		#[arg(long)]
		input: PathBuf,
	},

	EmmcDetect,
	EmmcInit,
	EmmcRead {
		#[arg(long)]
		lba: u32,

		#[arg(long)]
		out: PathBuf,
	},

	EmmcWrite {
		#[arg(long)]
		lba: u32,

		#[arg(long)]
		input: PathBuf,
	},

	EmmcDump {
		#[arg(long)]
		start: u32,

		#[arg(long)]
		count: u32,

		#[arg(long)]
		out: PathBuf,

		#[arg(long, default_value_t = false)]
		use_stream: bool,
	},
}
