// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    if cfg!(target_os = "windows") {
        mozilla_build();
    } else {
        autospider();
    }
    link();
}

fn run_logged_command(cmd: &mut Command) {
    println!("Running command: {:?}", cmd);
    let result = cmd.status().expect("Should spawn child OK");
    assert!(result.success(), "child should exit OK");
}

fn choose_python() -> String {
    let mut choices = vec![];
    if let Ok(python) = env::var("PYTHON") {
        choices.push(python);
    }
    choices.push("python2.7".into());
    choices.push("python2.7.exe".into());
    choices.push("python2".into());
    choices.push("python2.exe".into());
    choices.push("python".into());
    choices.push("python.exe".into());

    for python in choices {
        if {
            Command::new(&python)
                .args(&["-c", "print 'Hello, World'"])
                .output()
                .ok()
                .map_or(false, |out| {
                    String::from_utf8_lossy(&out.stdout).trim() == "Hello, World"
                })
        } {
            return python;
        }
    }

    panic!("Could not find an acceptable Python")
}

fn autospider() {
    let out_dir = env::var("OUT_DIR").expect("Should have env var OUT_DIR");

    let js_src = env::var("CARGO_MANIFEST_DIR")
        .expect("Should have env var CARGO_MANIFEST_DIR");

    env::set_var("MAKEFLAGS", "-j6");
    env::set_current_dir(&js_src).unwrap();

    let variant = if cfg!(feature = "debugmozjs") {
        "plaindebug"
    } else {
        "plain"
    };

    let python = choose_python();
    run_logged_command(
        Command::new(&python)
            .args(&["./devtools/automation/autospider.py",
                    "--build-only",
                    "--objdir", &out_dir,
                    variant])
            .env("SOURCE", &js_src)
            .env("PWD", &js_src)
            .env("AUTOMATION", "1")
            .env("PYTHON", &python)
    );
}

const MOZILLA_BUILD_URL: &'static str =
    "https://ftp.mozilla.org/pub/mozilla.org/mozilla/libraries/win32/MozillaBuildSetup-Latest.exe";

fn mozilla_build() {
    let out_dir = PathBuf::from(
        env::var("OUT_DIR")
            .expect("Should have env var OUT_DIR")
    );
    let js_src = PathBuf::from(
        env::var("CARGO_MANIFEST_DIR")
            .expect("Should have env var CARGO_MANIFEST_DIR")
    );

    if !Path::new(r#"C:\mozilla-build"#).exists() {
        // Download mozillabuild.exe
        let powershell_command = format!(
            "& {{ (New-Object Net.WebClient).DownloadFile('{}', '{}') }}",
            MOZILLA_BUILD_URL,
            out_dir.join("mozillabuild.exe").display(),
        );
        run_logged_command(
            Command::new("powershell").args(&["-command", &powershell_command])
        );

        // Install mozillabuild
        run_logged_command(
            Command::new("./mozillabuild.exe").arg("/S").current_dir(&out_dir)
        );
    }

    // Run autoconf.
    run_logged_command(
        Command::new(r#"C:\mozilla-build\start-shell.bat"#)
            .current_dir(&js_src)
            .arg("autoconf2.13")
    );

    // Run configure.
    run_logged_command(
        Command::new(r#"C:\mozilla-build\start-shell.bat"#)
            .current_dir(&out_dir)
            .arg(js_src.join("configure"))
            .arg("--enable-nspr-build")
            .args(
                if cfg!(feature = "debugmozjs") {
                    &["--disable-optimize", "--enable-debug"]
                } else {
                    &["--enable-optimize", "--disable-debug"]
                }
            )
    );

    // Run make.
    run_logged_command(
        Command::new(r#"C:\mozilla-build\start-shell.bat"#)
            .current_dir(&out_dir)
            .arg(js_src.join("make"))
            .arg("-j6")
    );
}

fn link() {
    let out_dir = env::var("OUT_DIR").expect("Should have env var OUT_DIR");
    let target = env::var("TARGET").expect("Should have env var TARGET");

    println!("cargo:rustc-link-search=native={}/js/src/build", out_dir);

    // Statically link SpiderMonkey.
    println!("cargo:rustc-link-lib=static=js_static");

    // On windows, MacOS, and Android, mozglue is only available as a shared
    // library. On other OSes, it is only available as a static library. See
    // mozglue/build/moz.build for details.
    println!("cargo:rustc-link-search=native={}/mozglue/build", out_dir);
    if cfg!(any(target_os = "macos",
                target_os = "windows",
                target_os = "android")) {
        println!("cargo:rustc-link-lib=mozglue");
    } else {
        println!("cargo:rustc-link-lib=static=mozglue");
    }

    println!("cargo:rustc-link-search=native={}/dist/bin", out_dir);
    println!("cargo:rustc-link-lib=nspr4");

    if target.contains("windows") {
        println!("cargo:rustc-link-lib=winmm");
        if target.contains("gnu") {
            println!("cargo:rustc-link-lib=stdc++");
        }
    } else if target.contains("macos") || target.contains("freebsd") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }

    println!("cargo:outdir={}", out_dir);
}
