fn main() {
    println!("cargo:rustc-link-arg=--export=__tls_base");
}
