/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use bindgen::Formatter;
use std::collections::btree_map::Entry;
use std::env;
use std::ffi::{OsStr, OsString};
use std::fs::{self, read_dir};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::str;
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
    "MOZJS_FORCE_RERUN",
    "PYTHON",
    "STLPORT_LIBS",
];

const EXTRA_FILES: &'static [&'static str] = &[
    "makefile.cargo",
    "src/rustfmt.toml",
    "src/jsglue.hpp",
    "src/jsglue.cpp",
];

fn main() {
    // https://github.com/servo/mozjs/issues/113
    env::set_var("MOZCONFIG", "");

    // https://github.com/servo/servo/issues/14759
    env::set_var("MOZ_NO_DEBUG_RTL", "1");

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let build_dir = out_dir.join("build");

    // Used by rust-mozjs downstream, don't remove.
    println!("cargo:outdir={}", build_dir.display());

    fs::create_dir_all(&build_dir).expect("could not create build dir");

    copy_jsapi(&build_dir);
    build_jsglue(&build_dir);

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

fn copy_jsapi(build_dir: &Path) {
    // TODO: run the download script
    fs::remove_dir_all(build_dir).unwrap();
    fs::create_dir(build_dir).unwrap();
    for entry in read_dir("spidermonkey/release").unwrap() {
        let path = entry.unwrap().path();
        let mut target = build_dir.to_path_buf();
        let name = path.components().last().unwrap();
        target.push(name);
        copy_dir::copy_dir(path, target).unwrap();
    }

    println!("cargo:rustc-link-search=native=/opt/wasix-sysroot/lib/wasm32-wasi");
    println!("cargo:rustc-link-search=native={}/lib", build_dir.display());
    println!("cargo:rustc-link-lib=static=js_static");
    println!("cargo:rustc-link-lib=c++");
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

    if is_apple || is_freebsd {
        result.push("-stdlib=libc++");
    }

    result
}

fn build_jsglue(build_dir: &Path) {
    let mut build = cc::Build::new();
    build.cpp(true);
    build.compiler("clang++");

    for flag in cc_flags() {
        build.flag_if_supported(flag);
    }

    if build.get_compiler().is_like_msvc() {
        build.flag_if_supported("-std:c++17");
    } else {
        build.flag("--std=c++17");
    }

    build.flag("--target=wasm32-wasi");
    build.flag("--sysroot=/opt/wasix-sysroot");

    build
        .file("src/jsglue.cpp")
        .include(build_dir.join("include"))
        .out_dir(build_dir.join("glue"))
        .compile("jsglue");
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
    "JS::NullHandleValue",
    "JS::TrueHandleValue",
    "JS::UndefinedHandleValue",
    "JSCLASS_.*",
    "JSFUN_.*",
    "JSITER_.*",
    "JSPROP_.*",
    "JSREG_.*",
    "JS_.*",
    "js::Proxy.*",
];

/// Functions we want to generate bindings to.
const WHITELIST_FUNCTIONS: &'static [&'static str] = &[
    "ExceptionStackOrNull",
    "glue::.*",
    "JS::.*",
    "js::.*",
    "JS_.*",
    ".*_TO_JSID",
    "JS_DeprecatedStringHasLatin1Chars",
    "JS_ForOfIteratorInit",
];

/// Functions we do not want to generate bindings to.
const BLACKLIST_FUNCTIONS: &'static [&'static str] = &[
    "JS::CopyAsyncStack",
    "JS::CreateError",
    "JS::DecodeMultiStencilsOffThread",
    "JS::DecodeStencilOffThread",
    "JS::EncodeStencil",
    "JS::FinishDecodeMultiStencilsOffThread",
    "JS::FinishIncrementalEncoding",
    "JS::FromPropertyDescriptor",
    "JS::GetExceptionCause",
    "JS::GetOptimizedEncodingBuildId",
    "JS::GetScriptTranscodingBuildId",
    "JS::dbg::FireOnGarbageCollectionHook",
    "JS_EncodeStringToUTF8BufferPartial",
    "JS_GetErrorType",
    "JS_GetOwnPropertyDescriptorById",
    "JS_GetOwnPropertyDescriptor",
    "JS_GetOwnUCPropertyDescriptor",
    "JS_GetPropertyDescriptorById",
    "JS_GetPropertyDescriptor",
    "JS_GetUCPropertyDescriptor",
    "JS_NewLatin1String",
    "JS_NewUCStringDontDeflate",
    "JS_NewUCString",
    "JS_PCToLineNumber",
    "js::AppendUnique",
    "js::SetPropertyIgnoringNamedGetter",
    "JS::FinishOffThreadStencil",
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
    // Classes we don't use and we cannot generate theri
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

    let ignored_extensions = ["pyc", "o", "so", "dll", "dylib"];

    path.extension().map_or(false, |extension| {
        ignored_extensions
            .iter()
            .any(|&ignored| extension == ignored)
    })
}
