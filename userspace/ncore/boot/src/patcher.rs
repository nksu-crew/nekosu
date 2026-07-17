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
    } else if data.starts_with(&[0x02, 0x21, 0x4c, 0x18]) {
        "lz4_legacy"
    } else if data.starts_with(b"\x04\x22\x4d\x18") {
        "lz4_frame"
    } else if data.starts_with(&[0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00]) {
        "xz"
    } else if data.starts_with(b"070701") || data.starts_with(b"070702") {
        "none"
    } else {
        "unknown"
    }
}

const LZ4_LEGACY_BLOCK_SIZE: usize = 0x800000;
const LZ4_LEGACY_MAGIC: u32 = 0x184c2102;
const LZ4HC_CLEVEL_MAX: i32 = 12;

fn decompress_lz4_legacy(data: &[u8]) -> Result<Vec<u8>> {
    use lz4::block::decompress_to_buffer;

    let mut pos = 0usize;
    let mut out = Vec::new();
    let mut out_buf = vec![0u8; LZ4_LEGACY_BLOCK_SIZE];

    while pos + 4 <= data.len() {
        let mut block_size = u32::from_le_bytes(data[pos..pos + 4].try_into().unwrap());
        pos += 4;
        if block_size == LZ4_LEGACY_MAGIC {
            if pos + 4 > data.len() {
                break;
            }
            block_size = u32::from_le_bytes(data[pos..pos + 4].try_into().unwrap());
            pos += 4;
        }
        let block_size = block_size as usize;
        if block_size == 0 || pos + block_size > data.len() {
            break;
        }
        let block = &data[pos..pos + block_size];
        pos += block_size;

        let n = decompress_to_buffer(block, Some(LZ4_LEGACY_BLOCK_SIZE as i32), &mut out_buf)
            .map_err(|e| anyhow::anyhow!("lz4 block decompress: {}", e))?;
        out.extend_from_slice(&out_buf[..n]);
    }
    Ok(out)
}

fn compress_lz4_legacy(data: &[u8]) -> Result<Vec<u8>> {
    use lz4::block::{CompressionMode, compress_bound, compress_to_buffer};

    let mut out = Vec::with_capacity(data.len());
    out.extend_from_slice(&LZ4_LEGACY_MAGIC.to_le_bytes());

    let bound = compress_bound(LZ4_LEGACY_BLOCK_SIZE).unwrap_or(LZ4_LEGACY_BLOCK_SIZE);
    let mut out_buf = vec![0u8; bound];

    for chunk in data.chunks(LZ4_LEGACY_BLOCK_SIZE) {
        let n = compress_to_buffer(
            chunk,
            Some(CompressionMode::HIGHCOMPRESSION(LZ4HC_CLEVEL_MAX)),
            false,
            &mut out_buf,
        )
        .map_err(|e| anyhow::anyhow!("lz4 block compress: {}", e))?;
        out.extend_from_slice(&(n as u32).to_le_bytes());
        out.extend_from_slice(&out_buf[..n]);
    }
    Ok(out)
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
        "lz4_legacy" => decompress_lz4_legacy(data),
        "none" => Ok(data.to_vec()),
        fmt => {
            let head = &data[..data.len().min(16)];
            let hex: Vec<String> = head.iter().map(|b| format!("{:02x}", b)).collect();
            let ascii: String = head
                .iter()
                .map(|&b| if b.is_ascii_graphic() { b as char } else { '.' })
                .collect();
            eprintln!(
                "ramdisk header ({} bytes total): {}  |{}|",
                data.len(),
                hex.join(" "),
                ascii
            );
            bail!("unsupported ramdisk compression: {}", fmt)
        }
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
        "lz4_legacy" => compress_lz4_legacy(data),
        "none" => Ok(data.to_vec()),
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
