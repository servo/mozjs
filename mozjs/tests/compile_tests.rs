/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#[test]
fn compile_test() {
    let t = trybuild::TestCases::new();
    t.pass("tests/compile_tests/pass/*.rs");
    t.compile_fail("tests/compile_tests/fail/*.rs");
}
