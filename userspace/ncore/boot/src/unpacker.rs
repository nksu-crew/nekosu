use anyhow::Result;
use std::fs;
use std::path::Path;

use crate::parser::*;
use crate::types::*;
use crate::utils::*;

pub fn unpack(image_path: &Path, out_dir: &Path) -> Result<()> {
    let data = read_file(image_path)?;
    fs::create_dir_all(out_dir)?;

    // vendor boot か通常 boot か magic で判定
    if data.len() >= 8 && &data[..8] == VENDOR_BOOT_MAGIC {
        let image = parse_vendor(&data)?;
        unpack_vendor(image, out_dir)
    } else {
        let image = parse(&data)?;
        unpack_boot(image, out_dir)
    }
}

pub fn unpack_boot(image: BootImage, out_dir: &Path) -> Result<()> {
    match image {
        BootImage::V0 {
            hdr,
            kernel,
            ramdisk,
            second,
        } => {
            write_file(&out_dir.join("kernel"), &kernel)?;
            write_file(&out_dir.join("ramdisk"), &ramdisk)?;
            if !second.is_empty() {
                write_file(&out_dir.join("second"), &second)?;
            }
            write_hdr_info_v0(out_dir, &hdr)?;
        }
        BootImage::V1 {
            hdr,
            kernel,
            ramdisk,
            second,
            recovery_dtbo,
        } => {
            write_file(&out_dir.join("kernel"), &kernel)?;
            write_file(&out_dir.join("ramdisk"), &ramdisk)?;
            if !second.is_empty() {
                write_file(&out_dir.join("second"), &second)?;
            }
            if !recovery_dtbo.is_empty() {
                write_file(&out_dir.join("recovery_dtbo"), &recovery_dtbo)?;
            }
            write_hdr_info_v1(out_dir, &hdr)?;
        }
        BootImage::V2 {
            hdr,
            kernel,
            ramdisk,
            second,
            recovery_dtbo,
            dtb,
        } => {
            write_file(&out_dir.join("kernel"), &kernel)?;
            write_file(&out_dir.join("ramdisk"), &ramdisk)?;
            if !second.is_empty() {
                write_file(&out_dir.join("second"), &second)?;
            }
            if !recovery_dtbo.is_empty() {
                write_file(&out_dir.join("recovery_dtbo"), &recovery_dtbo)?;
            }
            if !dtb.is_empty() {
                write_file(&out_dir.join("dtb"), &dtb)?;
            }
            write_hdr_info_v2(out_dir, &hdr)?;
        }
        BootImage::V3 {
            hdr,
            kernel,
            ramdisk,
        } => {
            write_file(&out_dir.join("kernel"), &kernel)?;
            write_file(&out_dir.join("ramdisk"), &ramdisk)?;
            write_hdr_info_v3(out_dir, &hdr)?;
        }
        BootImage::V4 {
            hdr,
            kernel,
            ramdisk,
            signature,
        } => {
            write_file(&out_dir.join("kernel"), &kernel)?;
            write_file(&out_dir.join("ramdisk"), &ramdisk)?;
            if !signature.is_empty() {
                write_file(&out_dir.join("signature"), &signature)?;
            }
            write_hdr_info_v4(out_dir, &hdr)?;
        }
    }
    println!("Unpacked to {:?}", out_dir);
    Ok(())
}

pub fn unpack_vendor(image: VendorBootImage, out_dir: &Path) -> Result<()> {
    match image {
        VendorBootImage::V3 {
            hdr,
            vendor_ramdisk,
            dtb,
        } => {
            write_file(&out_dir.join("vendor_ramdisk"), &vendor_ramdisk)?;
            if !dtb.is_empty() {
                write_file(&out_dir.join("dtb"), &dtb)?;
            }
            write_vendor_hdr_info_v3(out_dir, &hdr)?;
        }
        VendorBootImage::V4 {
            hdr,
            vendor_ramdisk,
            dtb,
            ramdisk_table,
            bootconfig,
        } => {
            write_file(&out_dir.join("vendor_ramdisk"), &vendor_ramdisk)?;
            if !dtb.is_empty() {
                write_file(&out_dir.join("dtb"), &dtb)?;
            }
            if !bootconfig.is_empty() {
                write_file(&out_dir.join("bootconfig"), &bootconfig)?;
            }
            // ramdisk table を JSON 風テキストで保存
            let mut table_txt = String::new();
            for (i, e) in ramdisk_table.iter().enumerate() {
                let (ramdisk_size, ramdisk_offset, ramdisk_type) =
                    (e.ramdisk_size, e.ramdisk_offset, e.ramdisk_type);
                table_txt.push_str(&format!(
                    "[{}] size={} offset={} type={} name={}\n",
                    i,
                    ramdisk_size,
                    ramdisk_offset,
                    ramdisk_type,
                    nullterm_str(&e.ramdisk_name),
                ));
            }
            write_file(&out_dir.join("ramdisk_table.txt"), table_txt.as_bytes())?;
            write_vendor_hdr_info_v4(out_dir, &hdr)?;
        }
    }
    println!("Unpacked vendor boot to {:?}", out_dir);
    Ok(())
}

fn write_hdr_info_v0(dir: &Path, hdr: &boot_img_hdr_v0) -> Result<()> {
    let (kernel_addr, ramdisk_addr, second_addr, tags_addr, page_size, os_version) = (
        hdr.kernel_addr,
        hdr.ramdisk_addr,
        hdr.second_addr,
        hdr.tags_addr,
        hdr.page_size,
        hdr.os_version,
    );
    let s = format!(
        "kernel_addr={:#010x}\nramdisk_addr={:#010x}\nsecond_addr={:#010x}\ntags_addr={:#010x}\npage_size={}\nos_version={:#010x}\nname={}\ncmdline={}\nextra_cmdline={}\n",
        kernel_addr,
        ramdisk_addr,
        second_addr,
        tags_addr,
        page_size,
        os_version,
        nullterm_str(&hdr.name),
        nullterm_str(&hdr.cmdline),
        nullterm_str(&hdr.extra_cmdline),
    );
    write_file(&dir.join("header"), s.as_bytes())
}

fn write_hdr_info_v1(dir: &Path, hdr: &boot_img_hdr_v1) -> Result<()> {
    write_hdr_info_v0(dir, &hdr._base)?;
    let (recovery_dtbo_offset, header_size) = (hdr.recovery_dtbo_offset, hdr.header_size);
    let s = format!(
        "recovery_dtbo_offset={:#018x}\nheader_size={}\n",
        recovery_dtbo_offset, header_size,
    );
    use std::io::Write as _;
    let mut f = fs::OpenOptions::new()
        .append(true)
        .open(dir.join("header"))?;
    f.write_all(s.as_bytes())?;
    Ok(())
}

fn write_hdr_info_v2(dir: &Path, hdr: &boot_img_hdr_v2) -> Result<()> {
    write_hdr_info_v1(dir, &hdr._base)?;
    let dtb_addr = hdr.dtb_addr;
    let s = format!("dtb_addr={:#018x}\n", dtb_addr);
    use std::io::Write as _;
    let mut f = fs::OpenOptions::new()
        .append(true)
        .open(dir.join("header"))?;
    f.write_all(s.as_bytes())?;
    Ok(())
}

fn write_hdr_info_v3(dir: &Path, hdr: &boot_img_hdr_v3) -> Result<()> {
    let (os_version, header_size) = (hdr.os_version, hdr.header_size);
    let s = format!(
        "os_version={:#010x}\nheader_size={}\ncmdline={}\n",
        os_version,
        header_size,
        nullterm_str(&hdr.cmdline),
    );
    write_file(&dir.join("header"), s.as_bytes())
}

fn write_hdr_info_v4(dir: &Path, hdr: &boot_img_hdr_v4) -> Result<()> {
    write_hdr_info_v3(dir, &hdr._base)?;
    let signature_size = hdr.signature_size;
    let s = format!("signature_size={}\n", signature_size);
    use std::io::Write as _;
    let mut f = fs::OpenOptions::new()
        .append(true)
        .open(dir.join("header"))?;
    f.write_all(s.as_bytes())?;
    Ok(())
}

fn write_vendor_hdr_info_v3(dir: &Path, hdr: &vendor_boot_img_hdr_v3) -> Result<()> {
    let (page_size, kernel_addr, ramdisk_addr, tags_addr, dtb_addr, dtb_size) = (
        hdr.page_size,
        hdr.kernel_addr,
        hdr.ramdisk_addr,
        hdr.tags_addr,
        hdr.dtb_addr,
        hdr.dtb_size,
    );
    let s = format!(
        "page_size={}\nkernel_addr={:#010x}\nramdisk_addr={:#010x}\ntags_addr={:#010x}\ndtb_addr={:#018x}\ndtb_size={}\nname={}\ncmdline={}\n",
        page_size,
        kernel_addr,
        ramdisk_addr,
        tags_addr,
        dtb_addr,
        dtb_size,
        nullterm_str(&hdr.name),
        nullterm_str(&hdr.cmdline),
    );
    write_file(&dir.join("header"), s.as_bytes())
}

fn write_vendor_hdr_info_v4(dir: &Path, hdr: &vendor_boot_img_hdr_v4) -> Result<()> {
    write_vendor_hdr_info_v3(dir, &hdr._base)?;
    let (vendor_ramdisk_table_size, vendor_ramdisk_table_entry_num, bootconfig_size) = (
        hdr.vendor_ramdisk_table_size,
        hdr.vendor_ramdisk_table_entry_num,
        hdr.bootconfig_size,
    );
    let s = format!(
        "vendor_ramdisk_table_size={}\nvendor_ramdisk_table_entry_num={}\nbootconfig_size={}\n",
        vendor_ramdisk_table_size, vendor_ramdisk_table_entry_num, bootconfig_size,
    );
    use std::io::Write as _;
    let mut f = fs::OpenOptions::new()
        .append(true)
        .open(dir.join("header"))?;
    f.write_all(s.as_bytes())?;
    Ok(())
}
