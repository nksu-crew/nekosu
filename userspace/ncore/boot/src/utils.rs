use anyhow::{Context, Result, bail};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::mem;
use std::path::{Path, PathBuf};

/// packed struct からコピーで読む（未アライン安全）
pub fn read_hdr<T: Copy>(data: &[u8]) -> Option<T> {
    if data.len() < mem::size_of::<T>() {
        return None;
    }
    Some(unsafe { std::ptr::read_unaligned(data.as_ptr() as *const T) })
}

/// packed struct をバイト列として参照する
pub fn hdr_as_bytes<T: Copy>(hdr: &T) -> &[u8] {
    unsafe { std::slice::from_raw_parts(hdr as *const T as *const u8, mem::size_of::<T>()) }
}

pub fn page_align(size: usize, page_size: usize) -> usize {
    (size + page_size - 1) & !(page_size - 1)
}

/// data[offset..] から size バイト取り出し、page_size でアライン後の次オフセットを返す
pub fn take_aligned(
    data: &[u8],
    offset: usize,
    size: usize,
    page_size: usize,
) -> Result<(Vec<u8>, usize)> {
    if size == 0 {
        return Ok((vec![], offset));
    }
    let end = offset + size;
    if end > data.len() {
        bail!(
            "image truncated: need {} bytes at offset {}, have {}",
            size,
            offset,
            data.len()
        );
    }
    let next = offset + page_align(size, page_size);
    Ok((data[offset..end].to_vec(), next))
}

pub fn write_padded(out: &mut Vec<u8>, data: &[u8], page_size: usize) {
    if data.is_empty() {
        return;
    }
    out.extend_from_slice(data);
    let pad = page_align(data.len(), page_size) - data.len();
    out.extend(std::iter::repeat(0u8).take(pad));
}

pub fn nullterm_str(bytes: &[u8]) -> &str {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    std::str::from_utf8(&bytes[..end]).unwrap_or("<invalid utf8>")
}

pub fn read_file(path: &Path) -> Result<Vec<u8>> {
    let mut f = File::open(path).with_context(|| format!("open {:?}", path))?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;
    Ok(buf)
}

pub fn write_file(path: &Path, data: &[u8]) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut f = File::create(path).with_context(|| format!("create {:?}", path))?;
    f.write_all(data)?;
    Ok(())
}

pub fn read_component(dir: &Path, name: &str) -> Vec<u8> {
    let p = dir.join(name);
    if p.exists() {
        read_file(&p).unwrap_or_default()
    } else {
        vec![]
    }
}

pub fn temp_dir(base: &Path) -> Result<PathBuf> {
    let dir = base
        .parent()
        .unwrap_or(Path::new("."))
        .join(format!(".ncore_tmp_{}", std::process::id()));
    fs::create_dir_all(&dir)?;
    Ok(dir)
}
