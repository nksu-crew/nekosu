use anyhow::{Result, ensure};
use log::info;
use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

mod defs {
    pub const BINARY_DIR: &str = "/data/adb/ksu/bin";
    pub const BUSYBOX_PATH: &str = "/data/adb/ksu/bin/busybox";
    pub const MODULE_DIR: &str = "/data/adb/modules";
}

pub fn get_common_env(module_id: Option<&str>) -> Vec<(String, String)> {
    let mut envs = vec![
        ("ASH_STANDALONE".to_string(), "1".to_string()),
        ("KSU".to_string(), "true".to_string()),
        (
            "PATH".to_string(),
            format!(
                "{}:{}",
                env::var("PATH").unwrap_or_default(),
                defs::BINARY_DIR
            ),
        ),
    ];

    if let Some(id) = module_id {
        envs.push(("KSU_MODULE".to_string(), id.to_string()));
    }
    envs
}

pub fn run_script<P: AsRef<Path>>(path: P, module_id: Option<&str>, wait: bool) -> Result<()> {
    let path = path.as_ref();
    ensure!(path.exists(), "Script not found: {}", path.display());

    info!("Executing script: {}", path.display());

    let mut cmd = Command::new(defs::BUSYBOX_PATH);
    cmd.arg("sh").arg(path).envs(get_common_env(module_id));

    if let Some(parent) = path.parent() {
        cmd.current_dir(parent);
    }

    if wait {
        let status = cmd.status()?;
        ensure!(status.success(), "Script failed with status: {}", status);
    } else {
        cmd.spawn()?;
    }

    Ok(())
}

/// 执行特定模块的指定阶段脚本 (如 post-fs-data.sh, service.sh)
pub fn run_module_stage(module_id: &str, stage: &str) -> Result<()> {
    let script_path = PathBuf::from(defs::MODULE_DIR)
        .join(module_id)
        .join(format!("{}.sh", stage));

    run_script(script_path, Some(module_id), true)
}
