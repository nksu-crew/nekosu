use anyhow::{Context, Result};
use cpio::{NewcBuilder, NewcReader};
use std::io::{Cursor, Read, Write};

pub struct CpioEntry {
    pub name: String,
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
    pub data: Vec<u8>,
}

pub fn parse_cpio(data: &[u8]) -> Result<Vec<CpioEntry>> {
    let mut entries = Vec::new();
    let mut cursor = Cursor::new(data);

    loop {
        let mut reader = NewcReader::new(cursor).context("Failed to initialize CPIO reader")?;

        let (name, mode, uid, gid, file_size, is_trailer) = {
            let entry = reader.entry();
            (
                entry.name().to_string(),
                entry.mode(),
                entry.uid(),
                entry.gid(),
                entry.file_size(),
                entry.is_trailer(),
            )
        };

        if is_trailer {
            break;
        }

        let size = file_size.try_into().context("file too large")?;

        let mut entry_data = Vec::new();
        entry_data.reserve_exact(size);
        reader.read_to_end(&mut entry_data)?;

        entries.push(CpioEntry {
            name,
            mode,
            uid,
            gid,
            data: entry_data,
        });

        cursor = reader
            .finish()
            .context("Failed to advance to next CPIO entry")?;
    }

    Ok(entries)
}

pub fn write_cpio(entries: &[CpioEntry]) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut ino = 300u32;

    for entry in entries {
        let builder = NewcBuilder::new(&entry.name)
            .mode(entry.mode)
            .uid(entry.uid)
            .gid(entry.gid)
            .ino(ino);

        let mut writer = builder.write(&mut out, entry.data.len() as u32);

        writer
            .write_all(&entry.data)
            .context(format!("Failed to write data for '{}'", entry.name))?;

        writer
            .finish()
            .context(format!("Failed to finish entry '{}'", entry.name))?;

        ino += 1;
    }

    let trailer_builder = NewcBuilder::new("TRAILER!!!").nlink(1);
    let trailer_writer = trailer_builder.write(&mut out, 0);
    trailer_writer
        .finish()
        .context("Failed to finish trailer")?;

    let rem = out.len() % 512;
    if rem != 0 {
        let pad_len = 512 - rem;
        out.write_all(&vec![0u8; pad_len])
            .context("Failed to write padding")?;
    }

    Ok(out)
}
