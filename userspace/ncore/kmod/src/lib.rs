use std::collections::HashMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;

use goblin::elf::{Elf, section_header};

const KPTR_PATH: &str = "/proc/sys/kernel/kptr_restrict";

struct KptrGuard {
    saved: u8,
    initialized: bool,
}

impl KptrGuard {
    fn open() -> io::Result<Self> {
        let mut file = OpenOptions::new().read(true).write(true).open(KPTR_PATH)?;

        let mut buf = [0u8; 1];
        let initialized = if file.read_exact(&mut buf).is_ok() && buf[0] >= b'0' && buf[0] <= b'9' {
            true
        } else {
            false
        };
        let saved = buf[0];

        file.seek(SeekFrom::Start(0))?;
        file.write_all(b"1")?;

        Ok(KptrGuard { saved, initialized })
    }

    fn restore(&self) -> io::Result<()> {
        if !self.initialized {
            return Ok(());
        }
        let mut file = OpenOptions::new().write(true).open(KPTR_PATH)?;
        file.write_all(&[self.saved])?;
        Ok(())
    }
}

impl Drop for KptrGuard {
    fn drop(&mut self) {
        let _ = self.restore();
    }
}

fn normalize_symbol<'a>(sym: &'a str, buf: &'a mut String) -> &'a str {
    buf.clear();
    let dollar_pos = sym.find('$');
    let llvm_pos = sym.find(".llvm.");
    let cut = match (dollar_pos, llvm_pos) {
        (Some(d), Some(l)) => Some(std::cmp::min(d, l)),
        (Some(d), None) => Some(d),
        (None, Some(l)) => Some(l),
        (None, None) => None,
    };
    match cut {
        Some(pos) => {
            buf.push_str(&sym[..pos]);
            buf.as_str()
        }
        None => sym,
    }
}

fn hex_to_u64(s: &str) -> Option<u64> {
    u64::from_str_radix(s, 16).ok()
}

fn parse_kallsyms() -> io::Result<HashMap<String, u64>> {
    let guard = KptrGuard::open()?;
    let content = fs::read_to_string("/proc/kallsyms")?;
    let mut symbols = HashMap::with_capacity(262144);
    let mut buf = String::new();

    for line in content.lines() {
        let mut parts = line.split_whitespace();
        let addr_str = match parts.next() {
            Some(s) => s,
            None => continue,
        };
        let addr = match hex_to_u64(addr_str) {
            Some(a) => a,
            None => continue,
        };
        // skip type
        if parts.next().is_none() {
            continue;
        }
        let name = match parts.next() {
            Some(n) => n,
            None => continue,
        };
        let normalized = normalize_symbol(name, &mut buf);
        symbols.insert(normalized.to_owned(), addr);
    }
    drop(guard);
    Ok(symbols)
}

fn patch_and_load(data: &mut Vec<u8>, ksyms: &HashMap<String, u64>) -> Result<(), String> {
    let elf = Elf::parse(data).map_err(|e| format!("failed to parse ELF: {}", e))?;

    let symtab = &elf.syms;
    let strtab = &elf.strtab;

    let symtab_section = elf
        .section_headers
        .iter()
        .find(|sh| elf.shdr_strtab.get_at(sh.sh_name) == Some(".symtab"))
        .ok_or(".symtab section not found")?;

    let symtab_offset = symtab_section.sh_offset as usize;
    let sym_entry_size = symtab_section.sh_entsize as usize;

    if !elf.is_64 {
        return Err("ELF32 unsupported".into());
    }

    let mut nbuf = String::new();
    let mut modifications = Vec::new();
    let mut missing_symbols = Vec::new();

    for (i, sym) in symtab.iter().enumerate() {
        if sym.st_shndx != section_header::SHN_UNDEF as usize || sym.st_name == 0 {
            continue;
        }

        let raw_name = strtab.get_at(sym.st_name).ok_or("failed to get string")?;
        let name = normalize_symbol(raw_name, &mut nbuf);

        if let Some(&addr) = ksyms.get(name) {
            println!("Patching symbol {} -> 0x{:x}", name, addr);
            modifications.push((symtab_offset + i * sym_entry_size, addr));
        } else {
            missing_symbols.push(name.to_owned());
        }
    }

    if !missing_symbols.is_empty() {
        return Err(format!("missing symbols: {}", missing_symbols.join(", ")));
    }

    for (offset, addr) in modifications {
        let entry = data
            .get_mut(offset..offset + 24)
            .ok_or_else(|| format!("symbol offset {} out of bounds", offset))?;

        entry[6..8].copy_from_slice(&(section_header::SHN_ABS as u16).to_le_bytes());
        entry[8..16].copy_from_slice(&addr.to_le_bytes());
    }

    rustix::system::init_module(data, c"").map_err(|e| e.to_string())
}

pub fn load(path: &Path) -> Result<(), String> {
    let ksyms = parse_kallsyms().map_err(|e| format!("parse_kallsyms failed: {}", e))?;
    let mut file = File::open(path).map_err(|e| format!("open {}: {}", path.display(), e))?;
    let metadata = file.metadata().map_err(|e| format!("metadata: {}", e))?;
    let fsz = metadata.len() as usize;
    let mut data = vec![0u8; fsz];
    file.read_exact(&mut data)
        .map_err(|e| format!("read: {}", e))?;
    patch_and_load(&mut data, &ksyms)
}
