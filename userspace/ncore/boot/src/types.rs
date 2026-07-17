#![allow(non_snake_case, non_camel_case_types, dead_code, clippy::all)]

mod bindgen {
    include!(concat!(env!("OUT_DIR"), "/bootimg_bindgen.rs"));
}

pub use bindgen::*;

pub const BOOT_MAGIC: &[u8; 8] = b"ANDROID!";
pub const VENDOR_BOOT_MAGIC: &[u8; 8] = b"VNDRBOOT";

#[derive(Debug)]
pub enum BootImage {
    V0 {
        hdr: boot_img_hdr_v0,
        kernel: Vec<u8>,
        ramdisk: Vec<u8>,
        second: Vec<u8>,
    },
    V1 {
        hdr: boot_img_hdr_v1,
        kernel: Vec<u8>,
        ramdisk: Vec<u8>,
        second: Vec<u8>,
        recovery_dtbo: Vec<u8>,
    },
    V2 {
        hdr: boot_img_hdr_v2,
        kernel: Vec<u8>,
        ramdisk: Vec<u8>,
        second: Vec<u8>,
        recovery_dtbo: Vec<u8>,
        dtb: Vec<u8>,
    },
    V3 {
        hdr: boot_img_hdr_v3,
        kernel: Vec<u8>,
        ramdisk: Vec<u8>,
    },
    V4 {
        hdr: boot_img_hdr_v4,
        kernel: Vec<u8>,
        ramdisk: Vec<u8>,
        signature: Vec<u8>,
    },
}

#[derive(Debug)]
pub enum VendorBootImage {
    V3 {
        hdr: vendor_boot_img_hdr_v3,
        vendor_ramdisk: Vec<u8>,
        dtb: Vec<u8>,
    },
    V4 {
        hdr: vendor_boot_img_hdr_v4,
        vendor_ramdisk: Vec<u8>,
        dtb: Vec<u8>,
        ramdisk_table: Vec<vendor_ramdisk_table_entry_v4>,
        bootconfig: Vec<u8>,
    },
}
