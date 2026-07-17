use boot;
use clap::{Parser, Subcommand};
use kmod;
use modules;
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "ncore")]
#[command(about = "nekosu userspace tools.")]
#[command(disable_help_subcommand = true)]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Load kernel module
    Load {
        /// Path to the kernel module
        path: PathBuf,
    },

    /// Boot image tools
    Boot {
        #[command(subcommand)]
        command: BootCommands,
    },

    /// Load userspace module
    Mod {
        #[command(subcommand)]
        command: ModCommands,
    },
}

#[derive(Subcommand)]
enum BootCommands {
    /// Unpack boot image
    Unpack {
        /// Boot image path
        #[arg(value_name = "IMAGE")]
        image_path: PathBuf,

        /// Output directory
        #[arg(value_name = "OUTDIR")]
        out_dir: PathBuf,
    },

    /// Repack boot image from unpacked dir
    Repack {
        /// Original boot image
        original: PathBuf,

        /// Input directory with unpacked components
        in_dir: PathBuf,

        /// Output boot image path
        output: PathBuf,
    },

    /// Patch init to init.real and inject new init
    Patch {
        /// Original boot image
        image_path: PathBuf,

        /// New init file path
        new_init_path: PathBuf,

        /// Output patched boot image path
        output: PathBuf,
    },

    /// Print boot image header info
    Info {
        /// Boot image path
        image_path: PathBuf,
    },
}

#[derive(Subcommand)]
enum ModCommands {
    /// Show all installed modules
    Show,

    /// Run modules
    Run,
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Load { path } => {
            if let Err(e) = kmod::load(&path) {
                eprintln!("Error: {}", e);
                std::process::exit(1);
            }
            std::process::exit(0);
        }
        Commands::Boot { command } => {
            if let Err(e) = handle_boot_command(command) {
                eprintln!("Error: {}", e);
                std::process::exit(1);
            }
        }
        Commands::Mod { command } => {
            if let Err(e) = handle_mod_command(command) {
                eprintln!("Error: {}", e);
                std::process::exit(1);
            }
        }
    }
}

fn handle_boot_command(command: BootCommands) -> anyhow::Result<()> {
    match command {
        BootCommands::Unpack {
            image_path,
            out_dir,
        } => boot::unpack(&image_path, &out_dir),
        BootCommands::Repack {
            original,
            in_dir,
            output,
        } => boot::repack(&original, &in_dir, &output),
        BootCommands::Patch {
            image_path,
            new_init_path,
            output,
        } => boot::patch(&image_path, &new_init_path, &output),
        BootCommands::Info { image_path } => boot::info(&image_path),
    }
}

fn handle_mod_command(command: ModCommands) -> anyhow::Result<()> {
    match command {
        ModCommands::Show => {
            modules::list::list_modules();
            Ok(())
        }
        ModCommands::Run => {
            // TODO: 实现运行模块
            println!("TODO: Run modules");
            Ok(())
        }
    }
}
