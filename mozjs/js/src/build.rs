// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

extern crate num_cpus;

use std::env;
// use std::path;
use std::process::{Command, Stdio};

fn run_logged_command(mut cmd: Command) {
    println!("Running command: {:?}", cmd);
    let result = cmd
        .status()
        .expect("Should spawn child OK");
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

fn main() {
    let out_dir = env::var("OUT_DIR").expect("Should have env var OUT_DIR");
    let target = env::var("TARGET").expect("Should have env var TARGET");

    let mut js_src = env::var("CARGO_MANIFEST_DIR").expect("Should have env var CARGO_MANIFEST_DIR");

    if cfg!(windows) {
        // js/src/devtools/autospider.py uses `posixpath` instead of `os.path`
        // for joining paths together for Reasons (*handwaves*) so we play along
        // and make sure that windows paths aren't completely annihilated...
        js_src = js_src.replace('\\', "/");
    }

    env::set_var("MAKEFLAGS", format!("-j{}", num_cpus::get()));
    env::set_current_dir(&js_src).unwrap();

    let variant = if cfg!(feature = "debugmozjs") {
        "plaindebug"
    } else {
        "plain"
    };

    let python = choose_python();
    let mut cmd = Command::new(&python);
    cmd.args(&["./devtools/automation/autospider.py",
               "--build-only",
               "--objdir", &out_dir,
               variant])
        .env("SOURCE", &js_src)
        .env("PWD", &js_src)
        .env("AUTOMATION", "1")
        .env("PYTHON", &python)
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit());
    run_logged_command(cmd);


    println!("cargo:rustc-link-search=native={}/js/src/build", out_dir);

    // Statically link SpiderMonkey.
    println!("cargo:rustc-link-lib=static=js_static");

    // // Dynamically link SpiderMonkey.
    // // Link `libmozjs-$VERSION.so` to `libmozjs.so`.
    // let mut cmd = Command::new("sh");
    // cmd.args(&["-c", "ln -s $(pwd)/libmozjs-*.so $(pwd)/libmozjs.so"])
    //     .current_dir({
    //         let mut js_src_build = path::PathBuf::from(&out_dir);
    //         js_src_build.push("js/src/build");
    //         js_src_build
    //     })
    //     .stdout(Stdio::inherit())
    //     .stderr(Stdio::inherit());
    // run_logged_command(cmd);

    // println!("cargo:rustc-link-lib=mozjs");

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
    } else if target.contains("macos") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        println!("cargo:rustc-link-lib=stdc++");
    }

    println!("cargo:outdir={}", out_dir);
}
