/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use bindgen::callbacks::ParseCallbacks;
use bindgen::Formatter;
use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use std::env;
use std::env::VarError;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::str;
use std::sync::LazyLock;
use std::time::Instant;
use tar::Archive;
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

// For `cc-rs` `TARGET_XX` variables override non prefixed variables,
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
];

const EXTRA_FILES: &'static [&'static str] = &["makefile.cargo", "src/jsapi.cpp"];

/// Which version of moztools we expect
#[cfg(windows)]
const MOZTOOLS_VERSION: &str = "4.0";

fn main() {
    // https://github.com/servo/mozjs/issues/113
    env::set_var("MOZCONFIG", "");

    // https://github.com/servo/servo/issues/14759
    env::set_var("MOZ_NO_DEBUG_RTL", "1");

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let build_dir = out_dir.join("build");

    // Used by mozjs downstream, don't remove.
    println!("cargo:outdir={}", build_dir.display());

    // Check if we can link with pre-built archive , and decide if it needs to build from source.
    let mut build_from_source = should_build_from_source();
    if !build_from_source {
        if let Err(e) = link_static_lib_binaries(&build_dir) {
            println!("cargo:warning=Failed to link pre-built archive by {e}. Building from source instead.");
            build_from_source = true;
        }
    }

    if build_from_source {
        fs::create_dir_all(&build_dir).expect("could not create build dir");
        //TODO: use this and remove `no-rust-unicode-bidi.patch`
        //cbindgen_bidi(&build_dir);
        build_spidermonkey(&build_dir);
        build_jsapi(&build_dir);
        build_jsapi_bindings(&build_dir);
        jsglue::build(&build_dir);

        // If this env variable is set, create the compressed tarball of spidermonkey.
        if env::var_os("MOZJS_CREATE_ARCHIVE").is_some() {
            compress_static_lib(&build_dir).expect("Failed to compress static lib binaries.");
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

/// Returns the value of an environment variable as a boolean if set
///
/// If the variable is set without a value, that is interpreted as `true`
///
/// ### Panics
///
/// Panics if the value was set, but could not be interpreted as a boolean.
fn env_var_bool_value(name: &str) -> Option<bool> {
    let Some(os_val) = env::var_os(name) else {
        return None;
    };
    let val_lowercase = os_val.to_string_lossy().to_lowercase();
    match val_lowercase.as_str() {
        "1" | "on" | "true" | "yes" | "" => Some(true),
        "0" | "off" | "false" | "no" => Some(false),
        _other => {
            panic!("Expected `{name}` to have a boolean-ish value, but got `{os_val:?}`");
        }
    }
}

/// Check env variable conditions to decide if we need to link pre-built archive first.
/// And then return bool value to notify if we need to build from source instead.
fn should_build_from_source() -> bool {
    if let Some(from_source) = env_var_bool_value("MOZJS_FROM_SOURCE") {
        if from_source {
            println!(
                "Environment variable MOZJS_FROM_SOURCE is set. Building from source directly."
            );
        }
        from_source
    } else if env_var_bool_value("MOZJS_CREATE_ARCHIVE") == Some(true) {
        println!(
            "Environment variable MOZJS_CREATE_ARCHIVE is set. Building from source directly."
        );
        true
    } else if env::var_os("MOZJS_ARCHIVE").is_some() {
        false
    } else if env::var_os("CARGO_FEATURE_DEBUGMOZJS").is_some() {
        println!("debug-mozjs feature is enabled. Building from source directly.");
        true
    } else {
        false
    }
}

/// Returns the Rust version bindgen should target
pub(crate) fn minimum_rust_target() -> bindgen::RustTarget {
    let Ok(rust_target) = bindgen::RustTarget::stable(80, 0) else {
        // `InvalidRustTarget` does not implement Debug, so we manually panic.
        panic!("Unsupported Rust target")
    };
    rust_target
}

#[cfg(not(windows))]
fn find_make() -> OsString {
    if let Some(make) = env::var_os("MAKE") {
        make
    } else {
        match Command::new("gmake").status() {
            Ok(gmake) => {
                if gmake.success() {
                    OsStr::new("gmake").to_os_string()
                } else {
                    OsStr::new("make").to_os_string()
                }
            }
            Err(_) => OsStr::new("make").to_os_string(),
        }
    }
}

fn cc_flags() -> Vec<&'static str> {
    let mut result = vec!["-DRUST_BINDGEN", "-DSTATIC_JS_API"];

    if env::var_os("CARGO_FEATURE_DEBUGMOZJS").is_some() {
        result.extend(&["-DJS_GC_ZEAL", "-DDEBUG", "-DJS_DEBUG"]);
    }

    let target = env::var("TARGET").unwrap();
    if target.contains("windows") {
        result.extend(&[
            "-std=c++17",
            "-DWIN32",
            // Don't use reinterpret_cast() in offsetof(),
            // since it's not a constant expression, so can't
            // be used in static_assert().
            "-D_CRT_USE_BUILTIN_OFFSETOF",
        ]);
    } else {
        result.extend(&[
            "-std=gnu++17",
            "-fno-sized-deallocation",
            "-Wno-unused-parameter",
            "-Wno-invalid-offsetof",
            "-Wno-unused-private-field",
        ]);
    }

    let is_apple = target.contains("apple");
    let is_freebsd = target.contains("freebsd");
    let is_ohos = target.contains("ohos");

    if is_apple || is_freebsd || is_ohos {
        result.push("-stdlib=libc++");
    }

    result
}

#[cfg(windows)]
fn cargo_target_dir() -> PathBuf {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let mut dir = out_dir.as_path();
    while let Some(target_dir) = dir.parent() {
        if target_dir.file_name().unwrap().to_string_lossy() == "target" {
            return target_dir.to_path_buf();
        }
        dir = target_dir;
    }
    panic!("$OUT_DIR is not in target")
}

#[cfg(windows)]
fn find_moztools() -> Option<PathBuf> {
    let cargo_target_dir = cargo_target_dir();
    let deps_dir = cargo_target_dir.join("dependencies");
    let moztools_path = deps_dir.join("moztools").join(MOZTOOLS_VERSION);

    if moztools_path.exists() {
        Some(moztools_path)
    } else {
        None
    }
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

fn build_spidermonkey(build_dir: &Path) {
    let target = env::var("TARGET").unwrap();
    let make;

    #[cfg(windows)]
    {
        let moztools = if let Some(moztools) = env::var_os("MOZTOOLS_PATH") {
            PathBuf::from(moztools)
        } else if let Some(moztools) = find_moztools() {
            // moztools already in target/dependencies/moztools-*
            moztools
        } else if let Some(moz_build) = env::var_os("MOZILLABUILD") {
            // For now we also support mozilla build
            PathBuf::from(moz_build)
        } else if let Some(moz_build) = env::var_os("MOZILLA_BUILD") {
            // For now we also support mozilla build
            PathBuf::from(moz_build)
        } else {
            panic!(
                "MozTools or MozillaBuild not found!\n \
                Follow instructions on: https://github.com/servo/mozjs?tab=readme-ov-file#windows"
            );
        };
        let mut paths = Vec::new();
        paths.push(moztools.join("msys2").join("usr").join("bin"));
        paths.push(moztools.join("bin"));
        paths.extend(env::split_paths(&env::var_os("PATH").unwrap()));
        env::set_var("PATH", &env::join_paths(paths).unwrap());

        // https://searchfox.org/mozilla-esr115/source/python/mozbuild/mozbuild/util.py#1396
        env::set_var("MOZILLABUILD", moztools);

        make = OsStr::new("mozmake").to_os_string();
    }

    #[cfg(not(windows))]
    {
        make = find_make();
    }

    let mut cmd = Command::new(make.clone());

    // Set key environment variables, such as AR, CC, CXX based on what `cc-rs`
    // would choose.
    for var_base in SM_TARGET_ENV_VARS {
        if let Some(value) = get_cc_rs_env_os(var_base) {
            cmd.env(var_base, value);
        }
    }

    let encoding_c_mem_include_dir = env::var("DEP_ENCODING_C_MEM_INCLUDE_DIR").unwrap();
    let mut cppflags = OsString::from("-I");
    cppflags.push(OsString::from(
        encoding_c_mem_include_dir.replace("\\", "/"),
    ));
    cppflags.push(" ");
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
        cmd.env("PKG_CONFIG_PATH", pkg_config_path);
    }
    if let Ok(include) = std::env::var("DEP_Z_INCLUDE") {
        cppflags.push(format!("-I{include}").replace("\\", "/"));
        cppflags.push(" ");
    }
    cppflags.push(get_cc_rs_env_os("CPPFLAGS").unwrap_or_default());
    cmd.env("CPPFLAGS", cppflags);

    if let Some(makeflags) = env::var_os("CARGO_MAKEFLAGS") {
        cmd.env("MAKEFLAGS", makeflags);
    }

    if target.contains("apple") || target.contains("freebsd") || target.contains("ohos") {
        cmd.env("CXXFLAGS", "-stdlib=libc++");
    }

    let cargo_manifest_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let result = cmd
        .args(&["-R", "-f"])
        .arg(cargo_manifest_dir.join("makefile.cargo"))
        .current_dir(&build_dir)
        .env("SRC_DIR", &cargo_manifest_dir.join("mozjs"))
        .env("NO_RUST_PANIC_HOOK", "1")
        .status()
        .expect(&format!("Failed to run `{:?}`", make));
    assert!(result.success());
    if target.contains("windows") {
        let mut make_static = cc::Build::new();
        make_static.out_dir(build_dir.join("js/src/build"));
        std::fs::read_to_string(build_dir.join("js/src/build/js_static_lib.list"))
            .unwrap()
            .lines()
            .map(String::from)
            .for_each(|obj| {
                make_static.object(obj);
            });
        make_static.static_flag(true);
        make_static.compile("js_static");
    }

    println!(
        "cargo:rustc-link-search=native={}/js/src/build",
        build_dir.display()
    );
    println!("cargo:rustc-link-lib=static=js_static"); // Must come before c++
    if target.contains("windows") {
        println!(
            "cargo:rustc-link-search=native={}/dist/bin",
            build_dir.display()
        );
        println!("cargo:rustc-link-lib=winmm");
        println!("cargo:rustc-link-lib=psapi");
        println!("cargo:rustc-link-lib=user32");
        println!("cargo:rustc-link-lib=Dbghelp");
        if target.contains("gnu") {
            println!("cargo:rustc-link-lib=stdc++");
        }
    } else if target.contains("apple") || target.contains("freebsd") || target.contains("ohos") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }
}

fn build_jsapi(build_dir: &Path) {
    let mut build = cc::Build::new();
    build.cpp(true);

    for flag in cc_flags() {
        build.flag_if_supported(flag);
    }

    let config = format!("{}/js/src/js-confdefs.h", build_dir.display());
    if build.get_compiler().is_like_msvc() {
        build.flag_if_supported("-std:c++17");
        build.flag("-FI");
    } else {
        build.flag("-std=c++17");
        build.flag("-include");
    }
    build
        .flag(&config)
        .file("src/jsapi.cpp")
        .include(build_dir.join("dist/include"))
        .include(build_dir.join("js/src"))
        .out_dir(build_dir)
        .compile("jsapi");
}

/// Invoke bindgen on the JSAPI headers to produce raw FFI bindings for use from
/// Rust.
///
/// To add or remove which functions, types, and variables get bindings
/// generated, see the `const` configuration variables below.
fn build_jsapi_bindings(build_dir: &Path) {
    // By default, constructors, destructors and methods declared in .h files are inlined,
    // so their symbols aren't available. Adding the -fkeep-inlined-functions option
    // causes the jsapi library to bloat from 500M to 6G, so that's not an option.
    let mut config = bindgen::CodegenConfig::all();
    config &= !bindgen::CodegenConfig::CONSTRUCTORS;
    config &= !bindgen::CodegenConfig::DESTRUCTORS;
    config &= !bindgen::CodegenConfig::METHODS;

    let mut builder = bindgen::builder()
        .rust_target(minimum_rust_target())
        .header("./src/jsapi.cpp")
        // Translate every enum with the "rustified enum" strategy. We should
        // investigate switching to the "constified module" strategy, which has
        // similar ergonomics but avoids some potential Rust UB footguns.
        .rustified_enum(".*")
        .derive_partialeq(true)
        .size_t_is_usize(true)
        .enable_cxx_namespaces()
        .with_codegen_config(config)
        .formatter(Formatter::Rustfmt)
        .clang_arg("-I")
        .clang_arg(build_dir.join("dist/include").to_str().expect("UTF-8"))
        .clang_arg("-I")
        .clang_arg(build_dir.join("js/src").to_str().expect("UTF-8"))
        .clang_arg("-x")
        .clang_arg("c++");

    let target = env::var("TARGET").unwrap();
    if target.contains("windows") {
        builder = builder.clang_arg("-fms-compatibility");
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

    for flag in cc_flags() {
        builder = builder.clang_arg(flag);
    }

    builder = builder.clang_arg("-include");
    builder = builder.clang_arg(
        build_dir
            .join("js/src/js-confdefs.h")
            .to_str()
            .expect("UTF-8"),
    );

    println!(
        "Generating bindings {:?} {}.",
        builder.command_line_flags(),
        bindgen::clang_version().full
    );

    for ty in UNSAFE_IMPL_SYNC_TYPES {
        builder = builder.raw_line(format!("unsafe impl Sync for root::{} {{}}", ty));
    }

    for ty in WHITELIST_TYPES {
        builder = builder.allowlist_type(ty);
    }

    for var in WHITELIST_VARS {
        builder = builder.allowlist_var(var);
    }

    for func in WHITELIST_FUNCTIONS {
        builder = builder.allowlist_function(func);
    }

    for func in BLACKLIST_FUNCTIONS {
        builder = builder.blocklist_function(func);
    }

    for ty in OPAQUE_TYPES {
        builder = builder.opaque_type(ty);
    }

    for ty in BLACKLIST_TYPES {
        builder = builder.blocklist_type(ty);
    }

    for &(module, raw_line) in MODULE_RAW_LINES {
        builder = builder.module_raw_line(module, raw_line);
    }

    let bindings = builder
        .generate()
        .expect("Should generate JSAPI bindings OK");

    bindings
        .write_to_file(build_dir.join("jsapi.rs"))
        .expect("Should write bindings to file OK");
}

/// JSAPI types for which we should implement `Sync`.
const UNSAFE_IMPL_SYNC_TYPES: &'static [&'static str] = &[
    "JSClass",
    "JSFunctionSpec",
    "JSNativeWrapper",
    "JSPropertySpec",
    "JSTypedMethodJitInfo",
];

/// Types which we want to generate bindings for (and every other type they
/// transitively use).
const WHITELIST_TYPES: &'static [&'static str] = &["JS.*", "js::.*", "mozilla::.*"];

/// Global variables we want to generate bindings to.
const WHITELIST_VARS: &'static [&'static str] = &[
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
];

/// Functions we want to generate bindings to.
const WHITELIST_FUNCTIONS: &'static [&'static str] = &[
    "glue::.*",
    "JS::.*",
    "js::.*",
    "JS_.*",
    "JS_DeprecatedStringHasLatin1Chars",
];

/// Functions we do not want to generate bindings to.
const BLACKLIST_FUNCTIONS: &'static [&'static str] = &[
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
];

/// Types that should be treated as an opaque blob of bytes whenever they show
/// up within a whitelisted type.
///
/// These are types which are too tricky for bindgen to handle, and/or use C++
/// features that don't have an equivalent in rust, such as partial template
/// specialization.
const OPAQUE_TYPES: &'static [&'static str] = &[
    "JS::StackGCVector.*",
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
];

/// Types for which we should NEVER generate bindings, even if it is used within
/// a type or function signature that we are generating bindings for.
const BLACKLIST_TYPES: &'static [&'static str] = &[
    // We'll be using libc::FILE.
    "FILE",
    // We provide our own definition because we need to express trait bounds in
    // the definition of the struct to make our Drop implementation correct.
    "JS::Heap",
    // We provide our own definition because SM's use of templates
    // is more than bindgen can cope with.
    "JS::Rooted",
    // We don't need them and bindgen doesn't like them.
    "JS::HandleVector",
    "JS::MutableHandleVector",
    "JS::Rooted.*Vector",
    "JS::RootedValueArray",
    "js::ProfilingStackFrame.*",
    // Classes we don't use and we cannot generate their
    // types properly from bindgen so we'll skip them for now.
    "JS::dbg::Builder",
    "JS::dbg::Builder_BuiltThing",
    "JS::dbg::Builder_Object",
    "JS::dbg::Builder_Object_Base",
    "JS::dbg::BuilderOrigin",
];

/// Definitions for types that were blacklisted
const MODULE_RAW_LINES: &'static [(&'static str, &'static str)] = &[
    ("root", "pub type FILE = ::libc::FILE;"),
    ("root::JS", "pub type Heap<T> = crate::jsgc::Heap<T>;"),
    ("root::JS", "pub type Rooted<T> = crate::jsgc::Rooted<T>;"),
];

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

/// Customization of [`bindgen::CargoCallbacks`]
///
/// This accounts for generated header files, to prevent needless rebuilds
#[derive(Debug)]
struct CustomCargoCallbacks;

impl CustomCargoCallbacks {
    pub fn new() -> Self {
        Self {}
    }
}

impl ParseCallbacks for CustomCargoCallbacks {
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

mod jsglue {
    use crate::CustomCargoCallbacks;
    use std::env;
    use std::path::{Path, PathBuf};

    fn cc_flags(bindgen: bool) -> Vec<&'static str> {
        let mut result = vec!["-DSTATIC_JS_API"];

        if env::var("CARGO_FEATURE_DEBUGMOZJS").is_ok() {
            result.push("-DDEBUG");

            // bindgen doesn't like this
            if !bindgen {
                if cfg!(target_os = "windows") {
                    result.push("-Od");
                } else {
                    result.push("-g");
                    result.push("-O0");
                }
            }
        }

        if env::var("CARGO_FEATURE_PROFILEMOZJS").is_ok() {
            result.push("-fno-omit-frame-pointer");
        }

        result.push("-Wno-c++0x-extensions");
        result.push("-Wno-return-type-c-linkage");
        result.push("-Wno-invalid-offsetof");
        result.push("-Wno-unused-parameter");

        result
    }

    pub fn build(outdir: &Path) {
        //let mut build = cxx_build::bridge("src/jsglue.rs"); // returns a cc::Build;
        let mut build = cc::Build::new();
        let include_path = outdir.join("dist/include");

        build
            .cpp(true)
            .file("src/jsglue.cpp")
            .include(&include_path);

        for flag in cc_flags(false) {
            build.flag_if_supported(flag);
        }

        let confdefs_path: PathBuf = outdir.join("js/src/js-confdefs.h");
        let msvc = if build.get_compiler().is_like_msvc() {
            build.flag("-FI");
            build.flag(&confdefs_path);
            build.define("WIN32", "");
            build.flag("-Zi");
            build.flag("-GR-");
            build.flag("-std:c++17");
            true
        } else {
            build.flag("-fPIC");
            build.flag("-fno-rtti");
            build.flag("-std=c++17");
            build.flag("-include");
            build.flag(&confdefs_path);
            false
        };

        build.out_dir(outdir);
        build.compile("jsglue");
        println!("cargo:rerun-if-changed=src/jsglue.cpp");
        let mut builder = bindgen::Builder::default()
            .header("./src/jsglue.cpp")
            .rust_target(super::minimum_rust_target())
            .parse_callbacks(Box::new(CustomCargoCallbacks::new()))
            .size_t_is_usize(true)
            .formatter(bindgen::Formatter::Rustfmt)
            .clang_arg("-x")
            .clang_arg("c++")
            .clang_args(cc_flags(true))
            .clang_args(["-I", &include_path.to_string_lossy()])
            .enable_cxx_namespaces()
            .allowlist_file("./src/jsglue.cpp")
            .allowlist_recursively(false)
            .clang_args(["-include", &confdefs_path.to_str().expect("UTF-8")]);

        if msvc {
            builder = builder.clang_args([
                "-fms-compatibility",
                "-DWIN32",
                "-D_CRT_USE_BUILTIN_OFFSETOF",
                "-std=c++17",
            ])
        } else {
            builder = builder.clang_args(["-fPIC", "-fno-rtti", "-std=c++17"])
        }

        for ty in BLACKLIST_TYPES {
            builder = builder.blocklist_type(ty);
        }

        for ty in OPAQUE_TYPES {
            builder = builder.opaque_type(ty);
        }

        for &(module, raw_line) in MODULE_RAW_LINES {
            builder = builder.module_raw_line(module, raw_line);
        }
        let bindings = builder
            .generate()
            .expect("Unable to generate bindings to jsglue");

        bindings
            .write_to_file(outdir.join("gluebindings.rs"))
            .expect("Couldn't write bindings!");
    }

    /// Types that have generic arguments must be here or else bindgen does not generate <T>
    /// as it treats them as opaque types
    const BLACKLIST_TYPES: &'static [&'static str] = &[
        "JS::.*",
        "already_AddRefed",
        // we don't want it null
        "EncodedStringCallback",
    ];

    /// Types that should be treated as an opaque blob of bytes whenever they show
    /// up within a whitelisted type.
    ///
    /// These are types which are too tricky for bindgen to handle, and/or use C++
    /// features that don't have an equivalent in rust, such as partial template
    /// specialization.
    const OPAQUE_TYPES: &'static [&'static str] = &[
        "JS::Auto.*Impl",
        "JS::StackGCVector.*",
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
    ];

    /// Map mozjs_sys mod namespaces to bindgen mod namespaces
    const MODULE_RAW_LINES: &'static [(&'static str, &'static str)] = &[
        ("root", "pub(crate) use crate::jsapi::*;"),
        ("root", "pub use crate::glue::EncodedStringCallback;"),
        ("root::js", "pub(crate) use crate::jsapi::js::*;"),
        ("root::mozilla", "pub(crate) use crate::jsapi::mozilla::*;"),
        ("root::JS", "pub(crate) use crate::jsapi::JS::*;"),
    ];
}

// Get cargo target directory. There's no env variable for build script yet.
// See https://github.com/rust-lang/cargo/issues/9661 for more info.
fn get_cargo_target_dir(build_dir: &Path) -> Option<&Path> {
    let skip_triple = std::env::var("TARGET").unwrap() == std::env::var("HOST").unwrap();
    let skip_parent_dirs = if skip_triple { 5 } else { 6 };
    let mut current = build_dir;
    for _ in 0..skip_parent_dirs {
        current = current.parent()?;
    }

    Some(current)
}

/// Compress spidermonkey build into a tarball with necessary static binaries and bindgen wrappers.
fn compress_static_lib(build_dir: &Path) -> Result<(), std::io::Error> {
    let target = env::var("TARGET").unwrap();
    let target_dir = get_cargo_target_dir(build_dir).unwrap().display();
    let tar_gz = File::create(format!("{}/libmozjs-{}.tar.gz", target_dir, target))?;
    let enc = GzEncoder::new(tar_gz, Compression::default());
    let mut tar = tar::Builder::new(enc);

    if target.contains("windows") {
        // This is the static library of spidermonkey.
        tar.append_file(
            "js/src/build/js_static.lib",
            &mut File::open(build_dir.join("js/src/build/js_static.lib")).unwrap(),
        )?;
        // The bindgen binaries and generated rust files for mozjs.
        tar.append_file(
            "jsapi.lib",
            &mut File::open(build_dir.join("jsapi.lib")).unwrap(),
        )?;
        tar.append_file(
            "jsglue.lib",
            &mut File::open(build_dir.join("jsglue.lib")).unwrap(),
        )?;
        tar.append_file(
            "jsapi.rs",
            &mut File::open(build_dir.join("jsapi.rs")).unwrap(),
        )?;
        tar.append_file(
            "gluebindings.rs",
            &mut File::open(build_dir.join("gluebindings.rs")).unwrap(),
        )?;
    } else {
        let strip_bin = get_cc_rs_env_os("STRIP").unwrap_or_else(|| "strip".into());
        // Strip symbols from the static binary since it could bump up to 1.6GB on Linux.
        // TODO: Maybe we could separate symbols for thos who still want the debug ability.
        // https://github.com/GabrielMajeri/separate-symbols
        let mut strip = Command::new(strip_bin);
        if !target.contains("apple") {
            strip.arg("--strip-debug");
        };
        let status = strip
            .arg(build_dir.join("js/src/build/libjs_static.a"))
            .status()
            .unwrap();
        assert!(status.success());

        // This is the static library of spidermonkey.
        tar.append_file(
            "js/src/build/libjs_static.a",
            &mut File::open(build_dir.join("js/src/build/libjs_static.a")).unwrap(),
        )?;
        // The bindgen binaries and generated rust files for mozjs.
        tar.append_file(
            "libjsapi.a",
            &mut File::open(build_dir.join("libjsapi.a")).unwrap(),
        )?;
        tar.append_file(
            "libjsglue.a",
            &mut File::open(build_dir.join("libjsglue.a")).unwrap(),
        )?;
        tar.append_file(
            "jsapi.rs",
            &mut File::open(build_dir.join("jsapi.rs")).unwrap(),
        )?;
        tar.append_file(
            "gluebindings.rs",
            &mut File::open(build_dir.join("gluebindings.rs")).unwrap(),
        )?;
    }
    Ok(())
}

/// Decompress the archive of spidermonkey build to to build directory.
fn decompress_static_lib(archive: &Path, build_dir: &Path) -> Result<(), std::io::Error> {
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
fn download_archive(base: Option<&str>) -> Result<PathBuf, std::io::Error> {
    let base = base.unwrap_or("https://github.com/servo/mozjs/releases");
    let version = env::var("CARGO_PKG_VERSION").unwrap();
    let target = env::var("TARGET").unwrap();
    let archive_path = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("libmozjs.tar.gz");
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
                "{base}/download/mozjs-sys-v{version}/libmozjs-{target}.tar.gz"
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

/// Link against the static library archive instead of building SpiderMonkey from source.
fn link_static_lib_binaries(build_dir: &Path) -> Result<(), std::io::Error> {
    if let Ok(archive) = env::var("MOZJS_ARCHIVE") {
        // If the archive variable is present, assume it's a URL base to download from.
        let archive = download_archive(Some(&archive)).unwrap_or(PathBuf::from(archive));
        // Panic directly since the archive is specified manually.
        decompress_static_lib(&archive, build_dir).unwrap();
    } else {
        let archive = download_archive(None)?;
        decompress_static_lib(&archive, build_dir)?;
    };

    // Link static lib binaries
    let target = env::var("TARGET").unwrap();
    println!(
        "cargo:rustc-link-search=native={}/js/src/build",
        build_dir.display()
    );
    println!("cargo:rustc-link-lib=static=js_static"); // Must come before c++
    if target.contains("windows") {
        println!("cargo:rustc-link-lib=winmm");
        println!("cargo:rustc-link-lib=psapi");
        println!("cargo:rustc-link-lib=user32");
        println!("cargo:rustc-link-lib=Dbghelp");
        if target.contains("gnu") {
            println!("cargo:rustc-link-lib=stdc++");
        }
    } else if target.contains("apple") || target.contains("freebsd") || target.contains("ohos") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }
    // Link bindgen binaries
    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=static=jsapi");
    println!("cargo:rustc-link-lib=static=jsglue");
    Ok(())
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
    // For mozjs we should be correct to always use the target compiler, as it seems
    // very unlikely that anybody would use mozjs in build-tooling.
    let kind = "TARGET";
    let target_u = target.replace('-', "_");
    let res = get_env(&format!("{}_{}", var_base, target))
        .or_else(|| get_env(&format!("{}_{}", var_base, target_u)))
        .or_else(|| get_env(&format!("{}_{}", kind, var_base)))
        .or_else(|| get_env(var_base))?;
    Some(res)
}
