use anyhow::Result;
use std::path::Path;

use crate::parser::*;
use crate::types::*;
use crate::utils::*;

pub fn info(image_path: &Path) -> Result<()> {
    let data = read_file(image_path)?;

    if data.len() >= 8 && &data[..8] == VENDOR_BOOT_MAGIC {
        let image = parse_vendor(&data)?;
        info_vendor(image);
    } else {
        let image = parse(&data)?;
        info_boot(image);
    }
    Ok(())
}

pub fn info_boot(image: BootImage) {
    match image {
        BootImage::V0 {
            hdr,
            kernel,
            ramdisk,
            second,
        } => {
            let (page_size, kernel_addr, ramdisk_addr, second_addr, tags_addr, os_version) = (
                hdr.page_size,
                hdr.kernel_addr,
                hdr.ramdisk_addr,
                hdr.second_addr,
                hdr.tags_addr,
                hdr.os_version,
            );
            println!("Type        : boot v0");
            println!("Page size   : {}", page_size);
            println!(
                "Kernel      : {} bytes  addr={:#010x}",
                kernel.len(),
                kernel_addr
            );
            println!(
                "Ramdisk     : {} bytes  addr={:#010x}",
                ramdisk.len(),
                ramdisk_addr
            );
            println!(
                "Second      : {} bytes  addr={:#010x}",
                second.len(),
                second_addr
            );
            println!("Tags addr   : {:#010x}", tags_addr);
            println!("OS version  : {:#010x}", os_version);
            println!("Name        : {}", nullterm_str(&hdr.name));
            println!("Cmdline     : {}", nullterm_str(&hdr.cmdline));
            println!("Extra cmdline: {}", nullterm_str(&hdr.extra_cmdline));
        }
        BootImage::V1 {
            hdr,
            kernel,
            ramdisk,
            second,
            recovery_dtbo,
        } => {
            let (page_size, recovery_dtbo_offset, header_size) = (
                hdr._base.page_size,
                hdr.recovery_dtbo_offset,
                hdr.header_size,
            );
            println!("Type        : boot v1");
            println!("Page size   : {}", page_size);
            println!("Kernel      : {} bytes", kernel.len());
            println!("Ramdisk     : {} bytes", ramdisk.len());
            println!("Second      : {} bytes", second.len());
            println!(
                "Recovery dtbo: {} bytes  offset={:#018x}",
                recovery_dtbo.len(),
                recovery_dtbo_offset
            );
            println!("Header size : {}", header_size);
            println!("Cmdline     : {}", nullterm_str(&hdr._base.cmdline));
        }
        BootImage::V2 {
            hdr,
            kernel,
            ramdisk,
            second,
            recovery_dtbo,
            dtb,
        } => {
            let (page_size, dtb_addr) = (hdr._base._base.page_size, hdr.dtb_addr);
            println!("Type        : boot v2");
            println!("Page size   : {}", page_size);
            println!("Kernel      : {} bytes", kernel.len());
            println!("Ramdisk     : {} bytes", ramdisk.len());
            println!("Second      : {} bytes", second.len());
            println!("Recovery dtbo: {} bytes", recovery_dtbo.len());
            println!("DTB         : {} bytes  addr={:#018x}", dtb.len(), dtb_addr);
            println!("Cmdline     : {}", nullterm_str(&hdr._base._base.cmdline));
        }
        BootImage::V3 {
            hdr,
            kernel,
            ramdisk,
        } => {
            let (header_size, os_version) = (hdr.header_size, hdr.os_version);
            println!("Type        : boot v3");
            println!("Kernel      : {} bytes", kernel.len());
            println!("Ramdisk     : {} bytes", ramdisk.len());
            println!("Header size : {}", header_size);
            println!("OS version  : {:#010x}", os_version);
            println!("Cmdline     : {}", nullterm_str(&hdr.cmdline));
        }
        BootImage::V4 {
            hdr,
            kernel,
            ramdisk,
            signature,
        } => {
            let os_version = hdr._base.os_version;
            println!("Type        : boot v4");
            println!("Kernel      : {} bytes", kernel.len());
            println!("Ramdisk     : {} bytes", ramdisk.len());
            println!("Signature   : {} bytes", signature.len());
            println!("OS version  : {:#010x}", os_version);
            println!("Cmdline     : {}", nullterm_str(&hdr._base.cmdline));
        }
    }
}

pub fn info_vendor(image: VendorBootImage) {
    match image {
        VendorBootImage::V3 {
            hdr,
            vendor_ramdisk,
            dtb,
        } => {
            let (page_size, kernel_addr, ramdisk_addr, tags_addr, dtb_addr, dtb_size) = (
                hdr.page_size,
                hdr.kernel_addr,
                hdr.ramdisk_addr,
                hdr.tags_addr,
                hdr.dtb_addr,
                hdr.dtb_size,
            );
            println!("Type           : vendor boot v3");
            println!("Page size      : {}", page_size);
            println!("Kernel addr    : {:#010x}", kernel_addr);
            println!("Ramdisk addr   : {:#010x}", ramdisk_addr);
            println!("Vendor ramdisk : {} bytes", vendor_ramdisk.len());
            println!(
                "DTB            : {} bytes  addr={:#018x}",
                dtb.len(),
                dtb_addr
            );
            println!("DTB size hdr   : {}", dtb_size);
            println!("Tags addr      : {:#010x}", tags_addr);
            println!("Name           : {}", nullterm_str(&hdr.name));
            println!("Cmdline        : {}", nullterm_str(&hdr.cmdline));
        }
        VendorBootImage::V4 {
            hdr,
            vendor_ramdisk,
            dtb,
            ramdisk_table,
            bootconfig,
        } => {
            let (
                page_size,
                vendor_ramdisk_table_size,
                vendor_ramdisk_table_entry_num,
                bootconfig_size,
            ) = (
                hdr._base.page_size,
                hdr.vendor_ramdisk_table_size,
                hdr.vendor_ramdisk_table_entry_num,
                hdr.bootconfig_size,
            );
            println!("Type           : vendor boot v4");
            println!("Page size      : {}", page_size);
            println!("Vendor ramdisk : {} bytes", vendor_ramdisk.len());
            println!("DTB            : {} bytes", dtb.len());
            println!("Bootconfig     : {} bytes", bootconfig.len());
            println!("Ramdisk table  : {} entries", ramdisk_table.len());
            for (i, e) in ramdisk_table.iter().enumerate() {
                let (rs, ro, rt) = (e.ramdisk_size, e.ramdisk_offset, e.ramdisk_type);
                println!(
                    "  [{}] size={} offset={} type={} name={}",
                    i,
                    rs,
                    ro,
                    rt,
                    nullterm_str(&e.ramdisk_name),
                );
            }
            println!("Table size hdr : {}", vendor_ramdisk_table_size);
            println!("Table entries  : {}", vendor_ramdisk_table_entry_num);
            println!("Bootconfig size: {}", bootconfig_size);
            println!("Cmdline        : {}", nullterm_str(&hdr._base.cmdline));
        }
    }
}
