use anyhow::{Result, bail};
use std::mem;

use crate::types::*;
use crate::utils::*;

pub fn parse(data: &[u8]) -> Result<BootImage> {
    if data.len() < 44 {
        bail!("image too small");
    }
    if &data[..8] != BOOT_MAGIC {
        bail!("bad boot magic");
    }
    // header_version は v0〜v4 すべてで offset 40 の u32
    let version = u32::from_le_bytes(data[40..44].try_into().unwrap());

    match version {
        0 => {
            let hdr: boot_img_hdr_v0 =
                read_hdr(data).ok_or_else(|| anyhow::anyhow!("buffer too small for v0 header"))?;
            let ps = hdr.page_size as usize;
            if ps == 0 {
                bail!("page_size is 0");
            }
            let off = ps; // header は 1 page
            let (kernel, off) = take_aligned(data, off, hdr.kernel_size as usize, ps)?;
            let (ramdisk, off) = take_aligned(data, off, hdr.ramdisk_size as usize, ps)?;
            let (second, _) = take_aligned(data, off, hdr.second_size as usize, ps)?;
            Ok(BootImage::V0 {
                hdr,
                kernel,
                ramdisk,
                second,
            })
        }
        1 => {
            let hdr: boot_img_hdr_v1 =
                read_hdr(data).ok_or_else(|| anyhow::anyhow!("buffer too small for v1 header"))?;
            let ps = hdr._base.page_size as usize;
            if ps == 0 {
                bail!("page_size is 0");
            }
            let off = ps;
            let (kernel, off) = take_aligned(data, off, hdr._base.kernel_size as usize, ps)?;
            let (ramdisk, off) = take_aligned(data, off, hdr._base.ramdisk_size as usize, ps)?;
            let (second, off) = take_aligned(data, off, hdr._base.second_size as usize, ps)?;
            let (recovery_dtbo, _) = take_aligned(data, off, hdr.recovery_dtbo_size as usize, ps)?;
            Ok(BootImage::V1 {
                hdr,
                kernel,
                ramdisk,
                second,
                recovery_dtbo,
            })
        }
        2 => {
            let hdr: boot_img_hdr_v2 =
                read_hdr(data).ok_or_else(|| anyhow::anyhow!("buffer too small for v2 header"))?;
            let ps = hdr._base._base.page_size as usize;
            if ps == 0 {
                bail!("page_size is 0");
            }
            let off = ps;
            let (kernel, off) = take_aligned(data, off, hdr._base._base.kernel_size as usize, ps)?;
            let (ramdisk, off) =
                take_aligned(data, off, hdr._base._base.ramdisk_size as usize, ps)?;
            let (second, off) = take_aligned(data, off, hdr._base._base.second_size as usize, ps)?;
            let (recovery_dtbo, off) =
                take_aligned(data, off, hdr._base.recovery_dtbo_size as usize, ps)?;
            let (dtb, _) = take_aligned(data, off, hdr.dtb_size as usize, ps)?;
            Ok(BootImage::V2 {
                hdr,
                kernel,
                ramdisk,
                second,
                recovery_dtbo,
                dtb,
            })
        }
        3 => {
            let hdr: boot_img_hdr_v3 =
                read_hdr(data).ok_or_else(|| anyhow::anyhow!("buffer too small for v3 header"))?;
            let ps = 4096usize;
            let off = page_align(mem::size_of::<boot_img_hdr_v3>(), ps);
            let (kernel, off) = take_aligned(data, off, hdr.kernel_size as usize, ps)?;
            let (ramdisk, _) = take_aligned(data, off, hdr.ramdisk_size as usize, ps)?;
            Ok(BootImage::V3 {
                hdr,
                kernel,
                ramdisk,
            })
        }
        4 => {
            let hdr: boot_img_hdr_v4 =
                read_hdr(data).ok_or_else(|| anyhow::anyhow!("buffer too small for v4 header"))?;
            let ps = 4096usize;
            let off = page_align(mem::size_of::<boot_img_hdr_v4>(), ps);
            let (kernel, off) = take_aligned(data, off, hdr._base.kernel_size as usize, ps)?;
            let (ramdisk, off) = take_aligned(data, off, hdr._base.ramdisk_size as usize, ps)?;
            let (signature, _) = take_aligned(data, off, hdr.signature_size as usize, ps)?;
            Ok(BootImage::V4 {
                hdr,
                kernel,
                ramdisk,
                signature,
            })
        }
        _ => bail!("unsupported boot image version {}", version),
    }
}

pub fn parse_vendor(data: &[u8]) -> Result<VendorBootImage> {
    if data.len() < 12 {
        bail!("vendor image too small");
    }
    if &data[..8] != VENDOR_BOOT_MAGIC {
        bail!("bad vendor boot magic");
    }
    let version = u32::from_le_bytes(data[8..12].try_into().unwrap());

    match version {
        3 => {
            let hdr: vendor_boot_img_hdr_v3 = read_hdr(data)
                .ok_or_else(|| anyhow::anyhow!("buffer too small for vendor v3 header"))?;
            let ps = hdr.page_size as usize;
            let off = page_align(mem::size_of::<vendor_boot_img_hdr_v3>(), ps);
            let (vendor_ramdisk, off) =
                take_aligned(data, off, hdr.vendor_ramdisk_size as usize, ps)?;
            let (dtb, _) = take_aligned(data, off, hdr.dtb_size as usize, ps)?;
            Ok(VendorBootImage::V3 {
                hdr,
                vendor_ramdisk,
                dtb,
            })
        }
        4 => {
            let hdr: vendor_boot_img_hdr_v4 = read_hdr(data)
                .ok_or_else(|| anyhow::anyhow!("buffer too small for vendor v4 header"))?;
            let ps = hdr._base.page_size as usize;
            let off = page_align(mem::size_of::<vendor_boot_img_hdr_v4>(), ps);
            let (vendor_ramdisk, off) =
                take_aligned(data, off, hdr._base.vendor_ramdisk_size as usize, ps)?;
            let (dtb, off) = take_aligned(data, off, hdr._base.dtb_size as usize, ps)?;
            // ramdisk table
            let table_off = off;
            let table_size = hdr.vendor_ramdisk_table_size as usize;
            let entry_size = hdr.vendor_ramdisk_table_entry_size as usize;
            let entry_num = hdr.vendor_ramdisk_table_entry_num as usize;
            if entry_size != mem::size_of::<vendor_ramdisk_table_entry_v4>() {
                bail!(
                    "unexpected ramdisk table entry size: {} vs {}",
                    entry_size,
                    mem::size_of::<vendor_ramdisk_table_entry_v4>()
                );
            }
            let table_end = table_off + table_size;
            if table_end > data.len() {
                bail!("vendor ramdisk table out of bounds");
            }
            let mut ramdisk_table = Vec::with_capacity(entry_num);
            for i in 0..entry_num {
                let entry_off = table_off + i * entry_size;
                let entry: vendor_ramdisk_table_entry_v4 = read_hdr(&data[entry_off..])
                    .ok_or_else(|| anyhow::anyhow!("ramdisk table entry {} out of bounds", i))?;
                ramdisk_table.push(entry);
            }
            let bc_off = table_off + page_align(table_size, ps);
            let (bootconfig, _) = take_aligned(data, bc_off, hdr.bootconfig_size as usize, ps)?;
            Ok(VendorBootImage::V4 {
                hdr,
                vendor_ramdisk,
                dtb,
                ramdisk_table,
                bootconfig,
            })
        }
        _ => bail!("unsupported vendor boot image version {}", version),
    }
}
