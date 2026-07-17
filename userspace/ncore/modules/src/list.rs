use serde_json::Value;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{BufRead, BufReader};
use std::path::Path;

pub fn get_modules_as_json() -> Result<Value, Box<dyn std::error::Error>> {
    let base_path = "/data/adb/modules/";
    let mut modules_map = HashMap::new();

    if let Ok(entries) = fs::read_dir(base_path) {
        for entry in entries.flatten() {
            let path = entry.path();

            if path.is_dir() {
                let prop_path = path.join("module.prop");

                if prop_path.exists() {
                    let module_name = path.file_name().unwrap().to_string_lossy().to_string();
                    if let Ok(data) = parse_prop_file(&prop_path) {
                        modules_map.insert(module_name, data);
                    }
                }
            }
        }
    }

    Ok(serde_json::to_value(modules_map)?)
}

fn parse_prop_file(path: &Path) -> Result<HashMap<String, String>, std::io::Error> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let mut map = HashMap::new();

    for line in reader.lines() {
        let line = line?;
        let trimmed = line.trim();

        if trimmed.starts_with('#') || trimmed.is_empty() {
            continue;
        }

        if let Some((key, value)) = trimmed.split_once('=') {
            map.insert(key.trim().to_string(), value.trim().to_string());
        }
    }
    Ok(map)
}

pub fn list_modules() {
    match get_modules_as_json() {
        Ok(json_data) => println!("{}", serde_json::to_string_pretty(&json_data).unwrap()),
        Err(e) => eprintln!("Error reading modules: {}", e),
    }
}
