fn main() {
    println!("cargo:rerun-if-changed=src/bootimg.h");
    bindgen::Builder::default()
        .header("src/bootimg.h")
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg("-std=c++17")
        .use_core()
        .derive_default(true)
        .derive_debug(true)
        .allowlist_type("boot_img_hdr_v[0-9]")
        .allowlist_type("vendor_boot_img_hdr_v[0-9]")
        .allowlist_type("vendor_ramdisk_table_entry_v4")
        .generate()
        .expect("bindgen failed")
        .write_to_file(
            std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("bootimg_bindgen.rs"),
        )
        .unwrap();
}
