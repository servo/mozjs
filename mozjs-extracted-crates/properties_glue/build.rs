fn main() {
    let out_dir = std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap());
    let include_dir = out_dir.join("include");
    std::fs::create_dir_all(&include_dir).unwrap();
    println!("cargo:include={}", include_dir.to_str().unwrap());
    cbindgen::generate(".")
        .expect("Unable to generate bindings")
        .write_to_file(include_dir.join("properties_glue.h"));
}
