fn main() {
    // todo: Should we do this here or in the mozjs-sys build-script?
    println!("cargo:rustc-link-lib=mimalloc");
}