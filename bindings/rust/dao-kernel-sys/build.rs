fn main() {
    if let Ok(directory) = std::env::var("DAO_KERNEL_LIB_DIR") {
        println!("cargo:rustc-link-search=native={directory}");
    }
    println!("cargo:rustc-link-lib=dylib=dao_kernel");
}
