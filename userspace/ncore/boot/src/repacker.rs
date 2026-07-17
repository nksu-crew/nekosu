use anyhow::Result;
use std::mem;
use std::path::Path;

use crate::parser::*;
use crate::types::*;
use crate::utils::*;

pub fn repack(original: &Path, in_dir: &Path, output: &Path) -> Result<()> {
    let orig_data = read_file(original)?;
    let mut out: Vec<u8> = Vec::new();

    if orig_data.len() >= 8 && &orig_data[..8] == VENDOR_BOOT_MAGIC {
        let orig = parse_vendor(&orig_data)?;
        repack_vendor(orig, in_dir, &mut out)?;
    } else {
        let orig = parse(&orig_data)?;
        repack_boot(orig, in_dir, &mut out)?;
    }

    write_file(output, &out)?;
    println!("Repacked to {:?}", output);
    Ok(())
}

pub fn repack_boot(orig: BootImage, in_dir: &Path, out: &mut Vec<u8>) -> Result<()> {
    let kernel = read_component(in_dir, "kernel");
    let ramdisk = read_component(in_dir, "ramdisk");

    match orig {
        BootImage::V0 {
            mut hdr, second, ..
        } => {
            let second = if in_dir.join("second").exists() {
                read_component(in_dir, "second")
            } else {
                second
            };
            let ps = hdr.page_size as usize;
            hdr.kernel_size = kernel.len() as u32;
            hdr.ramdisk_size = ramdisk.len() as u32;
            hdr.second_size = second.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &kernel, ps);
            write_padded(out, &ramdisk, ps);
            write_padded(out, &second, ps);
        }
        BootImage::V1 {
            mut hdr,
            second,
            recovery_dtbo,
            ..
        } => {
            let second = if in_dir.join("second").exists() {
                read_component(in_dir, "second")
            } else {
                second
            };
            let recovery_dtbo = if in_dir.join("recovery_dtbo").exists() {
                read_component(in_dir, "recovery_dtbo")
            } else {
                recovery_dtbo
            };
            let ps = hdr._base.page_size as usize;
            hdr._base.kernel_size = kernel.len() as u32;
            hdr._base.ramdisk_size = ramdisk.len() as u32;
            hdr._base.second_size = second.len() as u32;
            hdr.recovery_dtbo_size = recovery_dtbo.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &kernel, ps);
            write_padded(out, &ramdisk, ps);
            write_padded(out, &second, ps);
            write_padded(out, &recovery_dtbo, ps);
        }
        BootImage::V2 {
            mut hdr,
            second,
            recovery_dtbo,
            dtb,
            ..
        } => {
            let second = if in_dir.join("second").exists() {
                read_component(in_dir, "second")
            } else {
                second
            };
            let recovery_dtbo = if in_dir.join("recovery_dtbo").exists() {
                read_component(in_dir, "recovery_dtbo")
            } else {
                recovery_dtbo
            };
            let dtb = if in_dir.join("dtb").exists() {
                read_component(in_dir, "dtb")
            } else {
                dtb
            };
            let ps = hdr._base._base.page_size as usize;
            hdr._base._base.kernel_size = kernel.len() as u32;
            hdr._base._base.ramdisk_size = ramdisk.len() as u32;
            hdr._base._base.second_size = second.len() as u32;
            hdr._base.recovery_dtbo_size = recovery_dtbo.len() as u32;
            hdr.dtb_size = dtb.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &kernel, ps);
            write_padded(out, &ramdisk, ps);
            write_padded(out, &second, ps);
            write_padded(out, &recovery_dtbo, ps);
            write_padded(out, &dtb, ps);
        }
        BootImage::V3 { mut hdr, .. } => {
            let ps = 4096usize;
            hdr.kernel_size = kernel.len() as u32;
            hdr.ramdisk_size = ramdisk.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &kernel, ps);
            write_padded(out, &ramdisk, ps);
        }
        BootImage::V4 {
            mut hdr, signature, ..
        } => {
            let signature = if in_dir.join("signature").exists() {
                read_component(in_dir, "signature")
            } else {
                signature
            };
            let ps = 4096usize;
            hdr._base.kernel_size = kernel.len() as u32;
            hdr._base.ramdisk_size = ramdisk.len() as u32;
            hdr.signature_size = signature.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &kernel, ps);
            write_padded(out, &ramdisk, ps);
            write_padded(out, &signature, ps);
        }
    }
    Ok(())
}

pub fn repack_vendor(orig: VendorBootImage, in_dir: &Path, out: &mut Vec<u8>) -> Result<()> {
    match orig {
        VendorBootImage::V3 {
            mut hdr,
            vendor_ramdisk,
            dtb,
        } => {
            let vendor_ramdisk = if in_dir.join("vendor_ramdisk").exists() {
                read_component(in_dir, "vendor_ramdisk")
            } else {
                vendor_ramdisk
            };
            let dtb = if in_dir.join("dtb").exists() {
                read_component(in_dir, "dtb")
            } else {
                dtb
            };
            let ps = hdr.page_size as usize;
            hdr.vendor_ramdisk_size = vendor_ramdisk.len() as u32;
            hdr.dtb_size = dtb.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &vendor_ramdisk, ps);
            write_padded(out, &dtb, ps);
        }
        VendorBootImage::V4 {
            mut hdr,
            vendor_ramdisk,
            dtb,
            ramdisk_table,
            bootconfig,
        } => {
            let vendor_ramdisk = if in_dir.join("vendor_ramdisk").exists() {
                read_component(in_dir, "vendor_ramdisk")
            } else {
                vendor_ramdisk
            };
            let dtb = if in_dir.join("dtb").exists() {
                read_component(in_dir, "dtb")
            } else {
                dtb
            };
            let bootconfig = if in_dir.join("bootconfig").exists() {
                read_component(in_dir, "bootconfig")
            } else {
                bootconfig
            };
            let ps = hdr._base.page_size as usize;
            // ramdisk table を再シリアライズ
            let entry_size = mem::size_of::<vendor_ramdisk_table_entry_v4>();
            let mut table_bytes: Vec<u8> = Vec::with_capacity(ramdisk_table.len() * entry_size);
            for entry in &ramdisk_table {
                table_bytes.extend_from_slice(hdr_as_bytes(entry));
            }
            hdr._base.vendor_ramdisk_size = vendor_ramdisk.len() as u32;
            hdr._base.dtb_size = dtb.len() as u32;
            hdr.vendor_ramdisk_table_size = table_bytes.len() as u32;
            hdr.vendor_ramdisk_table_entry_num = ramdisk_table.len() as u32;
            hdr.vendor_ramdisk_table_entry_size = entry_size as u32;
            hdr.bootconfig_size = bootconfig.len() as u32;
            write_padded(out, hdr_as_bytes(&hdr), ps);
            write_padded(out, &vendor_ramdisk, ps);
            write_padded(out, &dtb, ps);
            write_padded(out, &table_bytes, ps);
            write_padded(out, &bootconfig, ps);
        }
    }
    Ok(())
}
