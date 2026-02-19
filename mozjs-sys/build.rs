/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use bindgen::callbacks::ParseCallbacks;
use bindgen::{CodegenConfig, RustTarget};
use std::ffi::{OsStr, OsString};
use std::fmt::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::{env, fs};
use walkdir::WalkDir;

const ENV_VARS: &'static [&'static str] = &[
    "AR",
    "AS",
    "CC",
    "CFLAGS",
    "CLANGFLAGS",
    "CPP",
    "CPPFLAGS",
    "CXX",
    "CXXFLAGS",
    "MAKE",
    "MOZTOOLS_PATH",
    "MOZJS_ARCHIVE",
    "MOZJS_CREATE_ARCHIVE",
    "MOZJS_FORCE_RERUN",
    "MOZJS_FROM_SOURCE",
    "PYTHON",
    "STLPORT_LIBS",
];

// For `cc-rs`, `TARGET_XX` variables override non prefixed variables,
// so we should mimic this behavior when building spidermonkey to have a consistent experience.
const SM_TARGET_ENV_VARS: &'static [&'static str] = &[
    "AR",
    "AS",
    "CC",
    "CFLAGS",
    "CLANGFLAGS",
    "CPP",
    "CPPFLAGS",
    "CXX",
    "CXXFLAGS",
    "READELF",
    "OBJCOPY",
    "WASI_SDK_PATH",
];

const EXTRA_FILES: &'static [&'static str] = &["makefile.cargo"];

/// The version of moztools we expect.
#[cfg(windows)]
const MOZTOOLS_VERSION: &str = "4.0";

fn main() {
    // https://github.com/servo/mozjs/issues/113
    env::set_var("MOZCONFIG", "");

    // https://github.com/servo/servo/issues/14759
    env::set_var("MOZ_NO_DEBUG_RTL", "1");

    if let Some(path) = wasi_sdk() {
        env::set_var(
            "WASI_SYSROOT",
            PathBuf::from(&path).join("share").join("wasi-sysroot"),
        );
        env::set_var("TARGET_CC", PathBuf::from(&path).join("bin").join("clang"));
        env::set_var(
            "TARGET_CXX",
            PathBuf::from(&path).join("bin").join("clang++"),
        );
    }

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let build_dir = out_dir.join("build");

    // Check if we can link with pre-built archive, and decide if it needs to build from source.
    let mut build_from_source = should_build_from_source();
    if !build_from_source {
        if let Ok(archive) = env::var("MOZJS_ARCHIVE") {
            // If the archive variable is present, assume it's a URL base to download from.
            let archive =
                archive::download_archive(Some(&archive)).unwrap_or(PathBuf::from(archive));
            // Panic directly since the archive is specified manually.
            archive::decompress_static_lib(&archive, &build_dir).unwrap();
        } else {
            let result = archive::download_archive(None)
                .and_then(|archive| archive::decompress_static_lib(&archive, &build_dir));
            if let Err(e) = result {
                println!("cargo:warning=Failed to link pre-built archive by {e}. Building from source instead.");
                build_from_source = true;
            }
        }

        if !build_from_source {
            link_static_lib_binaries(&build_dir);
            link_bindgen_static_lib_binaries(&build_dir);
        }
    }

    if build_from_source {
        fs::create_dir_all(&build_dir).expect("could not create build dir");
        // TODO: use this and remove `no-rust-unicode-bidi.patch`
        // cbindgen_bidi(&build_dir);
        build_spidermonkey(&build_dir);
        build(&build_dir, BuildTarget::JSApi);
        build_bindings(&build_dir, BuildTarget::JSApi);
        build(&build_dir, BuildTarget::JSGlue);
        build_bindings(&build_dir, BuildTarget::JSGlue);

        // If this env variable is set, create the compressed tarball of spidermonkey.
        if env::var_os("MOZJS_CREATE_ARCHIVE").is_some() {
            archive::compress_static_lib(&build_dir)
                .expect("Failed to compress static lib binaries.");
        }
    }

    if env::var_os("MOZJS_FORCE_RERUN").is_none() {
        for var in ENV_VARS {
            println!("cargo:rerun-if-env-changed={}", var);
        }

        for entry in WalkDir::new("mozjs") {
            let entry = entry.unwrap();
            let path = entry.path();
            if !ignore(path) {
                println!("cargo:rerun-if-changed={}", path.display());
            }
        }

        for file in EXTRA_FILES {
            println!("cargo:rerun-if-changed={}", file);
        }
    }
}

fn build_spidermonkey(build_dir: &Path) {
    let target = env::var("TARGET").unwrap();
    let make;

    #[cfg(windows)]
    {
        let moztools = find_moztools(build_dir);

        let mut paths = Vec::new();
        paths.push(join_path(&moztools, "msys2/usr/bin"));
        paths.push(join_path(&moztools, "/bin"));
        paths.extend(env::split_paths(&env::var_os("PATH").unwrap()));
        env::set_var("PATH", &env::join_paths(paths).unwrap());

        // https://searchfox.org/mozilla-esr115/source/python/mozbuild/mozbuild/util.py#1396
        env::set_var("MOZILLABUILD", &moztools);

        make = OsStr::new("mozmake").to_os_string();
    }

    #[cfg(not(windows))]
    {
        make = find_make();
    }

    let mut cmd = Command::new(&make);

    // Set key environment variables, such as AR, CC, CXX based on what `cc-rs`
    // would choose.
    for var_base in SM_TARGET_ENV_VARS {
        if let Some(value) = get_cc_rs_env_os(var_base) {
            cmd.env(var_base, value);
        }
    }

    // Tell python to not write bytecode cache files, since this will pollute
    // the source directory.
    cmd.env("PYTHONDONTWRITEBYTECODE", "1");

    let encoding_c_mem_include_dir = env::var("DEP_ENCODING_C_MEM_INCLUDE_DIR").unwrap();
    let mut cppflags = OsString::from(format!(
        "-I{} ",
        encoding_c_mem_include_dir.replace("\\", "/")
    ));

    if cfg!(all(feature = "libz-rs", feature = "libz-sys")) {
        panic!("Cannot enable both 'libz-rs' and 'libz-sys' features at the same time. Choose only one.");
    } else if cfg!(not(any(feature = "libz-rs", feature = "libz-sys"))) {
        panic!("Must enable one of the 'libz-rs' or 'libz-sys' features.");
    }

    if cfg!(feature = "libz-sys") {
        // add zlib.pc into pkg-config's search path
        // this is only needed when libz-sys builds zlib from source
        if let Ok(zlib_root_dir) = env::var("DEP_Z_ROOT") {
            let mut pkg_config_path = OsString::from(format!(
                "{}/lib/pkgconfig",
                zlib_root_dir.replace("\\", "/")
            ));
            if let Some(env_pkg_config_path) = get_cc_rs_env_os("PKG_CONFIG_PATH") {
                pkg_config_path.push(":");
                pkg_config_path.push(env_pkg_config_path);
            }
            cmd.env("PKG_CONFIG_PATH", &pkg_config_path);
            // If we are cross compiling, we have patched SM to use this env var instead of empty string
            cmd.env("TARGET_PKG_CONFIG_PATH", pkg_config_path);
        }

        if let Ok(include) = env::var("DEP_Z_INCLUDE") {
            write!(cppflags, "-I{} ", include.replace("\\", "/")).unwrap();
        }
    }

    cppflags.push(get_cc_rs_env_os("CPPFLAGS").unwrap_or_default());
    cmd.env("CPPFLAGS", cppflags);

    if let Some(makeflags) = env::var_os("CARGO_MAKEFLAGS") {
        cmd.env("MAKEFLAGS", makeflags);
    }

    let mut cxxflags = vec![];

    if target.contains("apple") || target.contains("freebsd") || target.contains("ohos") {
        cxxflags.push(String::from("-stdlib=libc++"));
    }

    let base_cxxflags = env::var("CXXFLAGS").unwrap_or_default();
    let mut cxxflags = cxxflags.join(" ");
    cxxflags.push_str(&base_cxxflags);
    cmd.env("CXXFLAGS", cxxflags);

    let cargo_manifest_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let result = cmd
        .args(&["-R", "-f"])
        .arg(cargo_manifest_dir.join("makefile.cargo"))
        .current_dir(&build_dir)
        .env("SRC_DIR", &cargo_manifest_dir.join("mozjs"))
        .env("NO_RUST_PANIC_HOOK", "1")
        .output()
        .expect(&format!("Failed to run `{:?}`", make));
    if !result.status.success() {
        println!(
            "stderr output:\n{}",
            String::from_utf8(result.stderr).unwrap()
        );
        let stdout = String::from_utf8(result.stdout).unwrap();
        println!("build output:\n{}", stdout,);
    }
    assert!(result.status.success());

    if target.contains("windows") {
        let mut make_static = cc::Build::new();
        make_static.out_dir(join_path(build_dir, "js/src/build"));
        fs::read_to_string(join_path(build_dir, "js/src/build/js_static_lib.list"))
            .unwrap()
            .lines()
            .map(String::from)
            .for_each(|obj| {
                make_static.object(obj);
            });
        make_static.compile("js_static");
    }

    link_static_lib_binaries(build_dir);
}

/*
fn cbindgen_bidi(build_dir: &Path) {
    /// Appends intl/bidi to `root`
    fn root_to_bidi(root: &Pah) -> PathBuf {
        root.join("intl").join("bidi")
    }
    let mozjs_sys_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    cbindgen::Builder::new()
      .with_crate(root_to_bidi(mozjs_sys_dir.join("mozjs")).join("rust").join("unicode-bidi-ffi"))
      .generate()
      .expect("Unable to generate bindings")
      .write_to_file(root_to_bidi(build_dir).join("unicode_bidi_ffi_generated.h"));
}
*/

fn build(build_dir: &Path, target: BuildTarget) {
    let mut build = cc::Build::new();
    build.cpp(true).file(target.path());

    for flag in cc_flags(false) {
        build.flag_if_supported(flag);
    }

    if let Ok(android_api) = env::var("ANDROID_API_LEVEL").as_deref() {
        build.define("__ANDROID_MIN_SDK_VERSION__", android_api);
    }

    build.flag(include_file_flag(build.get_compiler().is_like_msvc()));
    build.flag(&js_config_path(build_dir));

    for path in target.include_paths(build_dir) {
        build.include(path);
    }

    build.out_dir(build_dir).compile(target.output());
}

/// Invoke bindgen to produce raw FFI bindings for use from Rust.
///
/// To add or remove which functions, types, and variables get bindings
/// generated, see the `const` configuration variables in the `bindings` module.
fn build_bindings(build_dir: &Path, target: BuildTarget) {
    // By default, constructors, destructors and methods declared in .h files are inlined,
    // so their symbols aren't available. Adding the -fkeep-inlined-functions option
    // causes the jsapi library to bloat from 500M to 6G, so that's not an option.
    let mut config = CodegenConfig::all();
    config &= !CodegenConfig::CONSTRUCTORS;
    config &= !CodegenConfig::DESTRUCTORS;
    config &= !CodegenConfig::METHODS;

    let mut builder = bindgen::builder()
        .rust_target(minimum_rust_target())
        .header(target.path())
        // Translate every enum with the "rustified enum" strategy. We should
        // investigate switching to the "constified module" strategy, which has
        // similar ergonomics but avoids some potential Rust UB footguns.
        .rustified_enum(".*")
        .derive_partialeq(true)
        .size_t_is_usize(true)
        .enable_cxx_namespaces()
        .with_codegen_config(config)
        .clang_args(cc_flags(true));

    if env::var("TARGET").unwrap().contains("wasi") {
        builder = builder
            .clang_arg("--sysroot")
            .clang_arg(env::var("WASI_SYSROOT").unwrap().to_string());
    }

    if target == BuildTarget::JSGlue {
        builder = builder
            .parse_callbacks(Box::new(JSGlueCargoCallbacks::default()))
            .allowlist_file(target.path())
            .allowlist_recursively(false);
    }

    for path in target.include_paths(build_dir) {
        builder = builder.clang_args(&["-I", &path]);
    }

    if let Some(flags) = get_cc_rs_env("CXXFLAGS") {
        for flag in flags.split_whitespace() {
            builder = builder.clang_arg(flag);
        }
    }

    if let Some(flags) = get_cc_rs_env("CLANGFLAGS") {
        for flag in flags.split_whitespace() {
            builder = builder.clang_arg(flag);
        }
    }

    let target_env = env::var("TARGET").unwrap();
    builder = builder.clang_args(&[
        include_file_flag(target_env.contains("windows")),
        &js_config_path(build_dir),
    ]);

    println!(
        "Generating bindings {:?} {}.",
        builder.command_line_flags(),
        bindgen::clang_version().full
    );

    for ty in target.unsafe_impl_sync_types() {
        builder = builder.raw_line(format!("unsafe impl Sync for root::{} {{}}", ty));
    }

    for ty in target.whitelist_types() {
        builder = builder.allowlist_type(ty);
    }

    for var in target.whitelist_vars() {
        builder = builder.allowlist_var(var);
    }

    for func in target.whitelist_functions() {
        builder = builder.allowlist_function(func);
    }

    for ty in target.blacklist_types() {
        builder = builder.blocklist_type(ty);
    }

    for func in target.blacklist_functions() {
        builder = builder.blocklist_function(func);
    }

    for ty in target.opaque_types() {
        builder = builder.opaque_type(ty);
    }

    for &(module, raw_line) in target.module_raw_lines() {
        builder = builder.module_raw_line(module, raw_line);
    }

    let bindings = builder.generate().expect("Should generate bindings OK");

    bindings
        .write_to_file(build_dir.join(target.output_bindings()))
        .expect("Should write bindings to file OK");
}

fn link_static_lib_binaries(build_dir: &Path) {
    let target = env::var("TARGET").unwrap();
    println!(
        "cargo:rustc-link-search=native={}",
        join_path(build_dir, "js/src/build").display()
    );
    println!("cargo:rustc-link-lib=static=js_static"); // Must come before c++

    if target.contains("windows") {
        println!("cargo:rustc-link-lib=winmm");
        println!("cargo:rustc-link-lib=psapi");
        println!("cargo:rustc-link-lib=user32");
        println!("cargo:rustc-link-lib=Dbghelp");
        println!("cargo:rustc-link-lib=advapi32");
    } else if target.contains("ohos") {
        println!("cargo:rustc-link-lib=hilog_ndk.z");
    }
    if let Some(cxxstdlib) = env::var("CXXSTDLIB").ok() {
        println!("cargo:rustc-link-lib={cxxstdlib}");
    } else if target.contains("apple") || target.contains("freebsd") || target.contains("ohos") {
        println!("cargo:rustc-link-lib=c++");
    } else if target.contains("windows") && target.contains("gnu") {
        println!("cargo:rustc-link-lib=stdc++");
    } else if !target.contains("windows") && !target.contains("wasi") {
        // The build works without this for WASI, and specifying it means
        // needing to use the WASI-SDK's clang for linking, which is annoying.
        println!("cargo:rustc-link-lib=stdc++")
    }

    if target.contains("wasi") {
        println!("cargo:rustc-link-lib=wasi-emulated-getpid");
    }
}

fn link_bindgen_static_lib_binaries(build_dir: &Path) {
    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=static=jsapi");
    println!("cargo:rustc-link-lib=static=jsglue");
}

/// Check env variable conditions to decide if we need to link pre-built archive first.
/// And then return bool value to notify if we need to build from source instead.
fn should_build_from_source() -> bool {
    if env::var_os("MOZJS_FROM_SOURCE").is_some() {
        println!("Environment variable MOZJS_FROM_SOURCE is set. Building from source directly.");
        true
    } else if env::var_os("MOZJS_CREATE_ARCHIVE").is_some() {
        println!(
            "Environment variable MOZJS_CREATE_ARCHIVE is set. Building from source directly."
        );
        true
    } else if env::var_os("MOZJS_ARCHIVE").is_some() {
        false
    } else if env::var_os("CARGO_FEATURE_INTL").is_none() {
        println!("intl feature is disabled. Building from source directly.");
        true
    } else if !env::var_os("CARGO_FEATURE_JIT").is_some() {
        println!("jit feature is NOT enabled. Building from source directly.");
        true
    } else {
        false
    }
}

/// Returns the Rust version bindgen should target
fn minimum_rust_target() -> RustTarget {
    match RustTarget::stable(80, 0) {
        Ok(target) => target,
        Err(e) => panic!("Unsupported: {e}"),
    }
}

fn cc_flags(bindgen: bool) -> Vec<&'static str> {
    let mut flags = Vec::new();

    let target = env::var("TARGET").unwrap();

    if target.contains("windows") {
        if bindgen {
            flags.push("--driver-mode=cl");
        }

        flags.extend(&[
            "-std:c++17",
            "-Zi",
            "-GR-",
            "-DWIN32",
            // Don't use reinterpret_cast() in offsetof(),
            // since it's not a constant expression, so can't
            // be used in static_assert().
            "-D_CRT_USE_BUILTIN_OFFSETOF",
        ]);
    } else {
        flags.extend(&[
            "-std=gnu++17",
            "-std=c++17",
            "-xc++",
            "-fPIC",
            "-fno-rtti",
            "-fno-sized-deallocation",
            "-Wno-c++0x-extensions",
            "-Wno-return-type-c-linkage",
            "-Wno-unused-parameter",
            "-Wno-invalid-offsetof",
            "-Wno-unused-private-field",
        ]);

        if env::var_os("CARGO_FEATURE_PROFILEMOZJS").is_some() {
            flags.push("-fno-omit-frame-pointer");
        }

        if target.contains("wasi") {
            // Unconditionally target p1 for now. Even if the application
            // targets p2, an adapter will take care of it.
            flags.push("--target=wasm32-wasip1");
            flags.push("-fvisibility=default");
        }
    }

    flags.extend(&["-DSTATIC_JS_API", "-DRUST_BINDGEN"]);
    if env::var_os("CARGO_FEATURE_DEBUGMOZJS").is_some() {
        flags.extend(&["-DJS_GC_ZEAL", "-DDEBUG", "-DJS_DEBUG"]);

        if !bindgen {
            if target.contains("windows") {
                flags.push("-Od");
            } else {
                flags.extend(&["-g", "-O0"]);
            }
        }
    }

    let is_apple = target.contains("apple");
    let is_freebsd = target.contains("freebsd");
    let is_ohos = target.contains("ohos");

    if is_apple || is_freebsd || is_ohos {
        flags.push("-stdlib=libc++");
    }

    if target.contains("wasi") {
        flags.push("-D_WASI_EMULATED_GETPID");
    }

    flags
}

fn include_file_flag(msvc_like: bool) -> &'static str {
    if msvc_like {
        "-FI"
    } else {
        "-include"
    }
}

fn js_config_path(build_dir: &Path) -> String {
    build_dir
        .join("js")
        .join("src")
        .join("js-confdefs.h")
        .display()
        .to_string()
}

fn wasi_sdk() -> Option<OsString> {
    if env::var("TARGET").unwrap().contains("wasi") {
        get_cc_rs_env_os("WASI_SDK_PATH")
    } else {
        None
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BuildTarget {
    JSApi,
    JSGlue,
}

impl BuildTarget {
    fn path(self) -> &'static str {
        match self {
            BuildTarget::JSApi => "./src/jsapi.cpp",
            BuildTarget::JSGlue => "./src/jsglue.cpp",
        }
    }

    fn output(self) -> &'static str {
        match self {
            BuildTarget::JSApi => "jsapi",
            BuildTarget::JSGlue => "jsglue",
        }
    }

    fn output_bindings(self) -> &'static str {
        match self {
            BuildTarget::JSApi => "jsapi.rs",
            BuildTarget::JSGlue => "gluebindings.rs",
        }
    }

    fn include_paths(self, build_dir: &Path) -> Vec<String> {
        let mut paths = Vec::with_capacity(2);
        paths.push(build_dir.join("dist").join("include").display().to_string());
        if self == BuildTarget::JSApi {
            paths.push(build_dir.join("js").join("src").display().to_string());
        }
        paths
    }
}

/// Customization of [`bindgen::CargoCallbacks`]
///
/// This accounts for generated header files, to prevent needless rebuilds
#[derive(Debug, Default)]
struct JSGlueCargoCallbacks;

impl ParseCallbacks for JSGlueCargoCallbacks {
    fn header_file(&self, filename: &str) {
        println!("cargo:rerun-if-changed={}", filename);
    }
    fn include_file(&self, filename: &str) {
        // These header files are generated by the build-script
        // so cargo checking for changes would only cause needless rebuilds.
        if !filename.contains("dist/include") {
            println!("cargo:rerun-if-changed={}", filename);
        }
    }

    fn read_env_var(&self, key: &str) {
        println!("cargo:rerun-if-env-changed={}", key);
    }
}

#[cfg(windows)]
fn cargo_target_dir(mut dir: &Path) -> PathBuf {
    while let Some(target_dir) = dir.parent() {
        if target_dir.file_name().unwrap().to_string_lossy() == "target" {
            return target_dir.to_path_buf();
        }
        dir = target_dir;
    }
    panic!("$OUT_DIR is not in target");
}

#[cfg(windows)]
fn find_moztools(build_dir: &Path) -> PathBuf {
    if let Some(moztools) = env::var_os("MOZTOOLS_PATH") {
        return PathBuf::from(moztools);
    }

    let cargo_target_dir = cargo_target_dir(build_dir);
    let moztools_path = join_path(
        &cargo_target_dir,
        &format!("dependencies/moztools/{MOZTOOLS_VERSION}"),
    );

    if moztools_path.exists() {
        return moztools_path;
    }

    // For now, we also support mozilla build
    if let Some(moz_build) = env::var_os("MOZILLABUILD").or_else(|| env::var_os("MOZILLA_BUILD")) {
        return PathBuf::from(moz_build);
    }

    panic!(
        "MozTools or MozillaBuild not found!\n \
                Follow instructions on: https://github.com/servo/mozjs?tab=readme-ov-file#windows"
    );
}

#[cfg(not(windows))]
fn find_make() -> OsString {
    if let Some(make) = env::var_os("MAKE") {
        return make;
    }

    match Command::new("gmake").args(&["--version"]).status() {
        Ok(gmake) if gmake.success() => OsStr::new("gmake").to_os_string(),
        _ => OsStr::new("make").to_os_string(),
    }
}

/// Rerun this build script if files under mozjs/ changed, unless this returns true.
/// Keep this in sync with .gitignore
fn ignore(path: &Path) -> bool {
    // Python pollutes a bunch of source directories with pyc and so files,
    // making cargo believe that the crate needs a rebuild just because a
    // directory's mtime changed.
    if path.is_dir() {
        return true;
    }

    if path.ends_with("js/src/configure") {
        return true;
    }

    let ignored_extensions = ["pyc", "o", "so", "dll", "dylib"];

    path.extension().map_or(false, |extension| {
        ignored_extensions
            .iter()
            .any(|&ignored| extension == ignored)
    })
}

impl BuildTarget {
    /// Types for which we should implement `Sync`.
    fn unsafe_impl_sync_types(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                "JSClass",
                "JSFunctionSpec",
                "JSNativeWrapper",
                "JSPropertySpec",
                "JSTypedMethodJitInfo",
            ],
            BuildTarget::JSGlue => &[],
        }
    }

    /// Types which we want to generate bindings for (and every other type they
    /// transitively use).
    fn whitelist_types(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &["JS.*", "js::.*", "mozilla::.*"],
            BuildTarget::JSGlue => &[],
        }
    }

    /// Global variables we want to generate bindings to.
    fn whitelist_vars(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                "JS::FalseHandleValue",
                "JS::NullHandleValue",
                "JS::TrueHandleValue",
                "JS::UndefinedHandleValue",
                "JSCLASS_.*",
                "JSFUN_.*",
                "JSITER_.*",
                "JSPROP_.*",
                "JS_.*",
                "js::Proxy.*",
            ],
            BuildTarget::JSGlue => &[],
        }
    }

    /// Functions we want to generate bindings to.
    fn whitelist_functions(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                "glue::.*",
                "JS::.*",
                "js::.*",
                "JS_.*",
                "JS_DeprecatedStringHasLatin1Chars",
            ],
            BuildTarget::JSGlue => &[],
        }
    }

    /// Types for which we should NEVER generate bindings, even if it is used within
    /// a type or function signature that we are generating bindings for.
    ///
    /// Types that have generic arguments must be here or else, bindgen does not generate <T>
    /// as it treats them as opaque types.
    fn blacklist_types(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                // We'll be using libc::FILE.
                "FILE",
                // We provide our own definition because we need to express trait bounds in
                // the definition of the struct to make our Drop implementation correct.
                "JS::Heap",
                // We provide our own definition because SM's use of templates
                // is more than bindgen can cope with.
                "JS::Rooted",
                // We don't need them and bindgen doesn't like them.
                "JS::StackGCVector.*",
                "JS::RootedVector_Vec",
                "JS::RootedVector_Base",
                "JS::HandleVector",
                "JS::MutableHandleVector",
                "JS::Rooted.*Vector",
                "JS::RootedValueArray",
                "js::ProfilingStackFrame.*",
                // Classes that we don't use, and that we cannot generate their
                // types properly from bindgen, so we'll skip them for now.
                "JS::dbg::Builder",
                "JS::dbg::Builder_BuiltThing",
                "JS::dbg::Builder_Object",
                "JS::dbg::Builder_Object_Base",
                "JS::dbg::BuilderOrigin",
                "JS::RootedTuple",
                "mozilla::external::AtomicRefCounted",
                "mozilla::ProfilerStringView",
                "mozilla::ProfilerString8View",
                "mozilla::ProfilerString16View",
            ],
            BuildTarget::JSGlue => &[
                "JS::.*",
                "already_AddRefed",
                // we don't want it null
                "EncodedStringCallback",
            ],
        }
    }

    /// Functions we do not want to generate bindings to.
    fn blacklist_functions(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                "JS::CopyAsyncStack",
                "JS::CreateError",
                "JS::DecodeMultiStencilsOffThread",
                "JS::DecodeStencilOffThread",
                "JS::DescribeScriptedCaller",
                "JS::EncodeStencil",
                "JS::FinishDecodeMultiStencilsOffThread",
                "JS::FinishIncrementalEncoding",
                "JS::FromPropertyDescriptor",
                "JS::GetExceptionCause",
                "JS::GetModulePrivate",
                "JS::GetOptimizedEncodingBuildId",
                "JS::GetPromiseResult",
                "JS::GetRegExpFlags",
                "JS::GetScriptPrivate",
                "JS::GetScriptTranscodingBuildId",
                "JS::GetScriptedCallerPrivate",
                "JS::MaybeGetScriptPrivate",
                "JS::NewArrayBufferWithContents",
                "JS::NewExternalArrayBuffer",
                "JS::dbg::FireOnGarbageCollectionHook",
                "JS_EncodeStringToUTF8BufferPartial",
                "JS_GetEmptyStringValue",
                "JS_GetErrorType",
                "JS_GetOwnPropertyDescriptorById",
                "JS_GetOwnPropertyDescriptor",
                "JS_GetOwnUCPropertyDescriptor",
                "JS_GetPropertyDescriptorById",
                "JS_GetPropertyDescriptor",
                "JS_GetReservedSlot",
                "JS_GetUCPropertyDescriptor",
                "JS_NewLatin1String",
                "JS_NewUCStringDontDeflate",
                "JS_NewUCString",
                "JS_PCToLineNumber",
                "js::AppendUnique",
                "js::SetPropertyIgnoringNamedGetter",
                "std::.*",
            ],
            BuildTarget::JSGlue => &[],
        }
    }

    /// Types that should be treated as an opaque blob of bytes whenever they show
    /// up within a whitelisted type.
    ///
    /// These are types which are too tricky for bindgen to handle, and/or use C++
    /// features that don't have an equivalent in rust, such as partial template
    /// specialization.
    fn opaque_types(self) -> &'static [&'static str] {
        match self {
            BuildTarget::JSApi => &[
                "JS::EnvironmentChain",
                "JS::PersistentRooted.*",
                "JS::detail::CallArgsBase",
                "js::detail::UniqueSelector.*",
                "mozilla::BufferList",
                "mozilla::Maybe.*",
                "mozilla::UniquePtr.*",
                "mozilla::Variant",
                "mozilla::Hash.*",
                "mozilla::detail::Hash.*",
                "RefPtr_Proxy.*",
                "std::.*",
                "mozilla::baseprofiler::BaseProfilerProcessId",
                "mozilla::baseprofiler::BaseProfilerThreadId",
                "mozilla::MarkerThreadId",
            ],
            BuildTarget::JSGlue => &[
                "JS::Auto.*Impl",
                "JS::PersistentRooted.*",
                "JS::detail::CallArgsBase.*",
                "js::detail::UniqueSelector.*",
                "mozilla::BufferList",
                "mozilla::Maybe.*",
                "mozilla::UniquePtr.*",
                "mozilla::Variant",
                "mozilla::Hash.*",
                "mozilla::detail::Hash.*",
                "RefPtr_Proxy.*",
            ],
        }
    }

    /// Raw lines that go at the start of each module.
    fn module_raw_lines(self) -> &'static [(&'static str, &'static str)] {
        match self {
            BuildTarget::JSApi => &[
                ("root", "pub type FILE = ::libc::FILE;"),
                ("root::JS", "pub type Heap<T> = crate::jsgc::Heap<T>;"),
                ("root::JS", "pub type Rooted<T> = crate::jsgc::Rooted<T>;"),
                ("root::JS", "pub type StackGCVector<T, AllocPolicy> = crate::jsgc::StackGCVector<T, AllocPolicy>;"),
            ],
            BuildTarget::JSGlue => &[
                ("root", "pub(crate) use crate::jsapi::*;"),
                ("root", "pub use crate::glue::EncodedStringCallback;"),
                ("root::js", "pub(crate) use crate::jsapi::js::*;"),
                ("root::mozilla", "pub(crate) use crate::jsapi::mozilla::*;"),
                ("root::JS", "pub(crate) use crate::jsapi::JS::*;"),
            ],
        }
    }
}

mod archive {
    use super::{get_cc_rs_env_os, join_path};

    use flate2::read::GzDecoder;
    use flate2::write::GzEncoder;
    use flate2::Compression;
    use std::env::VarError;
    use std::fs::File;
    use std::path::{Path, PathBuf};
    use std::process::Command;
    use std::sync::LazyLock;
    use std::time::Instant;
    use std::{env, fs};
    use tar::Archive;

    // Get cargo target directory. There's no env variable for build script yet.
    // See https://github.com/rust-lang/cargo/issues/9661 for more info.
    fn get_cargo_target_dir(build_dir: &Path) -> Option<&Path> {
        let skip_triple = env::var_os("TARGET").unwrap() == env::var_os("HOST").unwrap();
        let skip_parent_dirs = if skip_triple { 5 } else { 6 };
        let mut current = build_dir;
        for _ in 0..skip_parent_dirs {
            current = current.parent()?;
        }

        Some(current)
    }

    /// Compress spidermonkey build into a tarball with necessary static binaries and bindgen wrappers.
    pub(crate) fn compress_static_lib(build_dir: &Path) -> Result<(), std::io::Error> {
        let target = env::var("TARGET").unwrap();
        let target_dir = get_cargo_target_dir(build_dir).unwrap().display();
        let tar_gz = File::create(format!("{}/{}", target_dir, archive()))?;
        let enc = GzEncoder::new(tar_gz, Compression::default());
        let mut tar = tar::Builder::new(enc);

        if target.contains("windows") {
            // This is the static library of spidermonkey.
            tar.append_file(
                "js/src/build/js_static.lib",
                &mut File::open(join_path(build_dir, "js/src/build/js_static.lib"))?,
            )?;

            // The bindgen binaries and generated rust files for mozjs.
            tar.append_file(
                "jsapi.lib",
                &mut File::open(join_path(build_dir, "jsapi.lib"))?,
            )?;
            tar.append_file(
                "jsglue.lib",
                &mut File::open(join_path(build_dir, "jsglue.lib"))?,
            )?;

            tar.append_file(
                "jsapi.rs",
                &mut File::open(join_path(build_dir, "jsapi.rs"))?,
            )?;
            tar.append_file(
                "gluebindings.rs",
                &mut File::open(join_path(build_dir, "gluebindings.rs"))?,
            )?;
        } else {
            if env::var_os("CARGO_FEATURE_DEBUGMOZJS").is_none() {
                let strip_bin = get_cc_rs_env_os("STRIP").unwrap_or_else(|| "strip".into());
                // Strip symbols from the static binary since it could bump up to 1.6GB on Linux.
                // TODO: Maybe we could separate symbols for those who still want the debug ability.
                // https://github.com/GabrielMajeri/separate-symbols
                let mut strip = Command::new(strip_bin);
                if !target.contains("apple") {
                    strip.arg("--strip-debug");
                };
                let status = strip
                    .arg(join_path(build_dir, "js/src/build/libjs_static.a"))
                    .status()?;
                assert!(status.success());
            }

            // This is the static library of spidermonkey.
            tar.append_file(
                "js/src/build/libjs_static.a",
                &mut File::open(join_path(build_dir, "js/src/build/libjs_static.a"))?,
            )?;

            // The bindgen binaries and generated rust files for mozjs.
            tar.append_file(
                "libjsapi.a",
                &mut File::open(join_path(build_dir, "libjsapi.a"))?,
            )?;
            tar.append_file(
                "libjsglue.a",
                &mut File::open(join_path(build_dir, "libjsglue.a"))?,
            )?;

            tar.append_file(
                "jsapi.rs",
                &mut File::open(join_path(build_dir, "jsapi.rs"))?,
            )?;
            tar.append_file(
                "gluebindings.rs",
                &mut File::open(join_path(build_dir, "gluebindings.rs"))?,
            )?;
        }

        Ok(())
    }

    /// Returns name of libmozjs archive
    pub(crate) fn archive() -> String {
        let target = env::var("TARGET").unwrap();
        let features = if env::var_os("CARGO_FEATURE_DEBUGMOZJS").is_some() {
            "-debugmozjs"
        } else {
            ""
        };
        format!("libmozjs-{target}{features}.tar.gz")
    }

    /// Decompress the archive of spidermonkey build to build directory.
    pub(crate) fn decompress_static_lib(
        archive: &Path,
        build_dir: &Path,
    ) -> Result<(), std::io::Error> {
        // Try to open the archive from provided path. If it doesn't exist, try to open it as relative
        // path from workspace.
        let tar_gz = File::open(archive).unwrap_or({
            let mut workspace_dir = get_cargo_target_dir(build_dir).unwrap().to_path_buf();
            workspace_dir.pop();
            File::open(workspace_dir.join(archive))?
        });
        let tar = GzDecoder::new(tar_gz);
        let mut archive = Archive::new(tar);
        archive.unpack(build_dir)?;
        Ok(())
    }

    static ATTESTATION_AVAILABLE: LazyLock<bool> = LazyLock::new(|| {
        Command::new("gh")
            .arg("attestation")
            .arg("--help")
            .output()
            .is_ok_and(|output| output.status.success())
    });

    enum AttestationType {
        /// Fallback to compiling from source on failure
        Lenient,
        /// Abort the build on failure
        Strict,
    }

    enum ArtifactAttestation {
        /// Do not verify the attestation artifact.
        Disabled,
        /// Verify the attestation artifact
        Enabled(AttestationType),
    }

    impl ArtifactAttestation {
        const ENV_VAR_NAME: &'static str = "MOZJS_ATTESTATION";

        fn from_env_str(value: &str) -> Self {
            match value {
                "0" | "off" | "false" => ArtifactAttestation::Disabled,
                "1" | "on" | "true" | "lenient" => {
                    ArtifactAttestation::Enabled(AttestationType::Lenient)
                }
                "2" | "strict" | "force" => ArtifactAttestation::Enabled(AttestationType::Strict),
                other => {
                    println!(
                        "cargo:warning=`{}` set to unsupported value: {other}",
                        Self::ENV_VAR_NAME
                    );
                    ArtifactAttestation::Enabled(AttestationType::Lenient)
                }
            }
        }

        fn from_env() -> Self {
            match env::var(Self::ENV_VAR_NAME) {
                Ok(value) => {
                    let lower = value.to_lowercase();
                    return Self::from_env_str(&lower);
                }
                Err(VarError::NotPresent) => {}
                Err(VarError::NotUnicode(_)) => {
                    println!(
                        "cargo:warning={} value must be valid unicode.",
                        Self::ENV_VAR_NAME
                    );
                }
            }
            ArtifactAttestation::Disabled
        }
    }

    /// Use GitHub artifact attestation to verify the artifact is not corrupt.
    fn attest_artifact(kind: AttestationType, archive_path: &Path) -> Result<(), std::io::Error> {
        let start = Instant::now();
        if !*ATTESTATION_AVAILABLE {
            println!(
                "cargo:warning=Artifact attestation enabled, but not available. \
                     Please refer to the documentation for available values for {}",
                ArtifactAttestation::ENV_VAR_NAME
            );
        }
        let mut attestation_cmd = Command::new("gh");
        attestation_cmd
            .arg("attestation")
            .arg("verify")
            .arg(&archive_path)
            .arg("-R")
            .arg("servo/mozjs");

        let attestation_duration = start.elapsed();
        eprintln!(
            "Artifact evaluation took {} ms",
            attestation_duration.as_millis()
        );

        if let Err(output) = attestation_cmd.output() {
            println!("cargo:warning=Failed to verify the artifact downloaded from CI: {output:?}");
            // Remove the file so the build-script will redownload next time.
            let _ = fs::remove_file(&archive_path).inspect_err(|e| {
                println!("cargo:warning=Failed to delete archive: {e}");
            });
            match kind {
                AttestationType::Strict => panic!("Artifact verification failed!"),
                AttestationType::Lenient => {
                    return Err(std::io::Error::from(std::io::ErrorKind::InvalidData));
                }
            }
        }
        Ok(())
    }

    /// Download the SpiderMonkey archive with cURL using the provided base URL. If it's None,
    /// it will use `servo/mozjs`'s release page as the base URL.
    pub(crate) fn download_archive(base: Option<&str>) -> Result<PathBuf, std::io::Error> {
        let base = base.unwrap_or("https://github.com/servo/mozjs/releases");
        let version = env::var("CARGO_PKG_VERSION").unwrap();
        let archive_path = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join(&archive());

        if !archive_path.exists() {
            eprintln!("Trying to download prebuilt mozjs static library from Github Releases");
            let curl_start = Instant::now();
            if !Command::new("curl")
                .arg("-L")
                .arg("-f")
                .arg("-s")
                .arg("-o")
                .arg(&archive_path)
                .arg(format!(
                    "{base}/download/mozjs-sys-v{version}/{}",
                    archive()
                ))
                .status()?
                .success()
            {
                return Err(std::io::Error::from(std::io::ErrorKind::NotFound));
            }
            eprintln!(
                "Successfully downloaded mozjs archive in {} ms",
                curl_start.elapsed().as_millis()
            );
            let attestation = ArtifactAttestation::from_env();
            if let ArtifactAttestation::Enabled(kind) = attestation {
                attest_artifact(kind, &archive_path)?;
            }
        }

        Ok(archive_path)
    }
}

/// Joins paths component by component to reduce mixing of `\` and `/` in windows paths.
fn join_path(base: &Path, additional: &str) -> PathBuf {
    let mut base = PathBuf::from(base);
    for component in additional
        .trim_start_matches('/')
        .trim_end_matches('/')
        .split("/")
    {
        base.push(component);
    }
    base
}

/// Returns the value `cc-rs` would use for `var_base`
///
/// Since we build parts of our code without cc-rs by directly invoking spidermonkey,
/// we should first adjust key environment variables like `CC`, `CXX`, `AR` etc. to
/// have the values that users of `cc-rs` would expect.
///
/// Adapted from https://github.com/rust-lang/cc-rs/blob/3ba23569a623074748a3030f382afd22483555df/src/lib.rs#L3617
fn get_cc_rs_env(var_base: &str) -> Option<String> {
    get_cc_rs_env_os(var_base).map(|val| val.to_str().expect("Not a valid string.").to_string())
}

/// Like `get_cc_rs_env()` but returns the OsString value.
fn get_cc_rs_env_os(var_base: &str) -> Option<OsString> {
    fn get_env(var: &str) -> Option<OsString> {
        println!("cargo:rerun-if-env-changed={}", var);
        let value = env::var_os(var)?;
        Some(value)
    }

    let target = env::var("TARGET").expect("Cargo should set TARGET");
    // `cc-rs` does `if host == target { "HOST" } else { "TARGET" }`, which is not
    // correct when cross-compiling to the same target-triple (e.g. different sysroot).
    // For mozjs, we should be correct to always use the target compiler, as it seems
    // very unlikely that anybody would use mozjs in build-tooling.
    let kind = "TARGET";
    let target_u = target.replace('-', "_");

    get_env(&format!("{}_{}", var_base, target))
        .or_else(|| get_env(&format!("{}_{}", var_base, target_u)))
        .or_else(|| get_env(&format!("{}_{}", kind, var_base)))
        .or_else(|| get_env(var_base))
}
