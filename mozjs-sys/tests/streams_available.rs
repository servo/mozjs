/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![cfg(feature = "streams")]

use mozjs_sys::glue::ReadableStreamUnderlyingSourceTraps;

static UNDERLYING_SOURCE_TRAPS: ReadableStreamUnderlyingSourceTraps =
    ReadableStreamUnderlyingSourceTraps {
        requestData: None,
        writeIntoReadRequestBuffer: None,
        cancel: None,
        onClosed: None,
        onErrored: None,
        finalize: None,
    };

#[test]
fn test_ok() {
    assert!(UNDERLYING_SOURCE_TRAPS.requestData.is_none());
}
