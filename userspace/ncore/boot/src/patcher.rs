use anyhow::{Result, bail};
use std::fs;
use std::io::{Read, Write};
use std::path::Path;

use crate::cpio::*;
use crate::repacker::*;
use crate::unpacker::*;
use crate::utils::*;

pub fn patch(image_path: &Path, new_init_path: &Path, output: &Path) -> Result<()> {
    let tmp = temp_dir(output)?;
    let result = (|| -> Result<()> {
        unpack(image_path, &tmp)?;
        let ramdisk_path = tmp.join("ramdisk");
        let ramdisk_data = read_file(&ramdisk_path)?;
        let patched = patch_ramdisk(&ramdisk_data, new_init_path)?;
        write_file(&ramdisk_path, &patched)?;
        repack(image_path, &tmp, output)?;
        Ok(())
    })();
    let _ = fs::remove_dir_all(&tmp);
    result
}

pub fn detect_compression(data: &[u8]) -> &'static str {
    if data.starts_with(&[0x1f, 0x8b]) {
        "gzip"
    } else if data.starts_with(&[0x02, 0x21, 0x4d, 0x18]) {
        "lz4_legacy"
    } else if data.starts_with(b"\x04\x22\x4d\x18") {
        "lz4_frame"
    } else if data.starts_with(&[0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00]) {
        "xz"
    } else {
        "unknown"
    }
}

pub fn decompress(data: &[u8]) -> Result<Vec<u8>> {
    use flate2::read::GzDecoder;
    match detect_compression(data) {
        "gzip" => {
            let mut gz = GzDecoder::new(data);
            let mut buf = Vec::new();
            gz.read_to_end(&mut buf)?;
            Ok(buf)
        }
        "lz4_legacy" => lz4_flex::decompress_size_prepended(data)
            .map_err(|e| anyhow::anyhow!("lz4 decompress: {}", e)),
        fmt => bail!("unsupported ramdisk compression: {}", fmt),
    }
}

pub fn compress(data: &[u8], fmt: &str) -> Result<Vec<u8>> {
    use flate2::Compression;
    use flate2::write::GzEncoder;
    match fmt {
        "gzip" => {
            let mut enc = GzEncoder::new(Vec::new(), Compression::default());
            enc.write_all(data)?;
            Ok(enc.finish()?)
        }
        "lz4_legacy" => Ok(lz4_flex::compress_prepend_size(data)),
        fmt => bail!("unsupported compression: {}", fmt),
    }
}

pub fn patch_ramdisk(ramdisk: &[u8], new_init: &Path) -> Result<Vec<u8>> {
    let fmt = detect_compression(ramdisk);
    let cpio_data = decompress(ramdisk)?;
    let mut entries = parse_cpio(&cpio_data)?;

    let mut found = false;
    for entry in &mut entries {
        if entry.name == "init" {
            entry.name = "init.real".to_owned();
            found = true;
        }
    }
    if !found {
        bail!("init not found in ramdisk");
    }

    let new_init_data = read_file(new_init)?;
    entries.push(CpioEntry {
        name: "init".to_owned(),
        mode: 0o100755,
        uid: 0,
        gid: 0,
        data: new_init_data,
    });

    let cpio_out = write_cpio(&entries)?;
    compress(&cpio_out, fmt)
}
