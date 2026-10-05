/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use core::ffi::c_void;
use smallvec::SmallVec;

// The same as js::intl::INITIAL_CHAR_BUFFER_SIZE
const INLINE_SIZE: usize = 32;

type Buffer = SmallVec<[u16; INLINE_SIZE]>;

#[derive(Debug, PartialEq, Clone, Copy)]
#[repr(C)]
pub enum NormalizationForm {
    NFC = 0,
    NFKC = 1,
    NFD = 2,
    NFKD = 3,
}

#[repr(transparent)]
pub struct JSContext {
    _inner: c_void,
}

#[repr(transparent)]
pub struct JSLinearString {
    _inner: c_void,
}

extern "C" {
    fn js_call_js_normalize_utf16(
        cx: *mut JSContext,
        form: NormalizationForm,
        in_string: *mut JSLinearString,
        buffer: *mut c_void,
    ) -> bool;

    fn js_call_js_normalize_latin1(
        cx: *mut JSContext,
        form: NormalizationForm,
        in_string: *mut JSLinearString,
        buffer: *mut c_void,
    ) -> bool;

    fn js_new_ucstring_copy_n(
        cx: *mut JSContext,
        ptr: *const u16,
        len: usize,
    ) -> *mut JSLinearString;

    fn js_new_ucstring_copy_n_dont_deflate(
        cx: *mut JSContext,
        ptr: *const u16,
        len: usize,
    ) -> *mut JSLinearString;
}

#[no_mangle]
pub unsafe extern "C" fn js_normalize(
    cx: *mut JSContext,
    form: NormalizationForm,
    in_string: *mut JSLinearString,
    latin1: bool,
) -> *mut JSLinearString {
    // The purpose of this function is to establish `buffer` as a Rust type
    // on the stack. We need to call through a layer of C++ to the actual
    // normalization code so that we can use `AutoCheckCannotGC` as C++ RAII
    // that goes out of scope before we (potentially) create a new string
    // later in this function.

    // An earlier attempt used a `JSStringBuilder` as the buffer, but the
    // cases where a `JSString` ends up reusing the buffer within
    // `JSStringBuilder` are so slim, that it's better not to go ever FFI
    // for every write to the buffer and instead do a bulk copy out of
    // the buffer at the end of this function.
    let mut buffer: Buffer = SmallVec::new();
    {
        let buffer_borrow: &mut Buffer = &mut buffer;
        let buffer_ptr: *mut Buffer = buffer_borrow as *mut Buffer;
        let void_ptr: *mut c_void = buffer_ptr as *mut c_void;
        if !if latin1 {
            js_call_js_normalize_latin1(cx, form, in_string, void_ptr)
        } else {
            js_call_js_normalize_utf16(cx, form, in_string, void_ptr)
        } {
            return std::ptr::null_mut();
        }
    }

    // If nothing wrote to the buffer (and we haven't already returned), it's a
    // signal that the input is its own normalization.
    if buffer.is_empty() {
        return in_string;
    }

    // If we are normalizing to NFD and the input isn't its own normalization,
    // we know the output cannot be Latin1-only.
    if form == NormalizationForm::NFD {
        return js_new_ucstring_copy_n_dont_deflate(cx, buffer.as_ptr(), buffer.len());
    }

    return js_new_ucstring_copy_n(cx, buffer.as_ptr(), buffer.len());
}

#[no_mangle]
pub unsafe extern "C" fn js_normalize_utf16(
    form: NormalizationForm,
    ptr: *const u16,
    len: usize,
    buffer: *mut c_void,
) -> bool {
    let buffer_ptr: *mut Buffer = buffer as *mut Buffer;
    normalize_utf16(
        form,
        core::slice::from_raw_parts(ptr, len),
        &mut *buffer_ptr,
    )
}

#[no_mangle]
pub unsafe extern "C" fn js_normalize_latin1(
    form: NormalizationForm,
    ptr: *const u8,
    len: usize,
    buffer: *mut c_void,
) -> bool {
    let buffer_ptr: *mut Buffer = buffer as *mut Buffer;
    normalize_latin1(
        form,
        core::slice::from_raw_parts(ptr, len),
        &mut *buffer_ptr,
    )
}

fn maybe_reserve_buffer_space(
    form: NormalizationForm,
    input_len: usize,
    tail_len: usize,
    buffer: &mut Buffer,
) -> bool {
    if input_len <= INLINE_SIZE {
        // Let's not preallocate on the heap.
        return true;
    }
    // We're going to end up allocating on the heap. Since the allocation
    // is short-lived, let's overallocate in order to reduce the probability
    // of allocating multiple times during normalization. These are just
    // guesses.

    // Note that the normalizer itself calls the infallible `reserve`
    // with `input_len` as the argument, so it will always do nothing,
    // since we do greater than or equal to that with `try_reserve`
    // below.
    match form {
        NormalizationForm::NFC => {
            // Output typically shorter than input.
            buffer.try_reserve(input_len).is_ok()
        }
        NormalizationForm::NFKC => {
            // Output may expand a bit.
            let extra = core::cmp::max(8, tail_len / 16);
            buffer.try_reserve(input_len + extra).is_ok()
        }
        NormalizationForm::NFD | NormalizationForm::NFKD => {
            // Output may expand some more.
            // `tail_len / 8` is good for Greek
            // `tail_len / 4 + tail_len / 32` is good for Vietnamese
            // `tail_len` is good for Korean
            // Let's pick an arbitrary threshold for sizing for no
            // reallocation for Korean vs. no reallocation for Greek.
            let extra = core::cmp::max(
                16,
                if tail_len <= 1024 {
                    tail_len
                } else {
                    tail_len / 8
                },
            );
            buffer.try_reserve(input_len + extra).is_ok()
        }
    }
}

fn normalize_utf16(form: NormalizationForm, input: &[u16], buffer: &mut Buffer) -> bool {
    match form {
        NormalizationForm::NFC | NormalizationForm::NFKC => {
            let normalizer = if form == NormalizationForm::NFC {
                icu_normalizer::ComposingNormalizerBorrowed::new_nfc()
            } else {
                icu_normalizer::ComposingNormalizerBorrowed::new_nfkc()
            };
            let (head, tail) = normalizer.split_normalized_utf16(input);
            if tail.is_empty() {
                return true;
            }
            // We make an effort to do a fallible allocation...
            if !maybe_reserve_buffer_space(form, input.len(), tail.len(), buffer) {
                return false;
            }
            buffer.extend_from_slice(head);
            // ...but if more space is needed than what we reserved above and
            // allocation fails during normalization, we abort the program
            // instead of propagating the allocation error.
            let r = normalizer.normalize_utf16_to(tail, buffer);
            debug_assert!(r.is_ok());
        }
        NormalizationForm::NFD | NormalizationForm::NFKD => {
            let normalizer = if form == NormalizationForm::NFD {
                icu_normalizer::DecomposingNormalizer::new_nfd()
            } else {
                icu_normalizer::DecomposingNormalizer::new_nfkd()
            };
            let (head, tail) = normalizer.split_normalized_utf16(input);
            if tail.is_empty() {
                return true;
            }
            // We make an effort to do a fallible allocation...
            if !maybe_reserve_buffer_space(form, input.len(), tail.len(), buffer) {
                return false;
            }
            buffer.extend_from_slice(head);
            // ...but if more space is needed than what we reserved above and
            // allocation fails during normalization, we abort the program
            // instead of propagating the allocation error.
            let r = normalizer.normalize_utf16_to(tail, buffer);
            debug_assert!(r.is_ok());
        }
    }
    true
}

fn normalize_latin1(form: NormalizationForm, input: &[u8], buffer: &mut Buffer) -> bool {
    let (head, tail) = match form {
        NormalizationForm::NFKC => icu_normalizer::latin1::split_normalized_nfkc(input),
        NormalizationForm::NFD => icu_normalizer::latin1::split_normalized_nfd(input),
        NormalizationForm::NFKD => icu_normalizer::latin1::split_normalized_nfkd(input),
        NormalizationForm::NFC => {
            unreachable!("NFC should have been handled already");
        }
    };
    if tail.is_empty() {
        return true;
    }
    // We make an effort to do a fallible allocation...
    if !maybe_reserve_buffer_space(form, input.len(), tail.len(), buffer) {
        return false;
    }
    assert!(head.len() <= buffer.capacity());
    unsafe {
        // SAFETY: We have enough capacity. Exposure of slice of uninitialized
        // of integers for writing should be OK in practice:
        // https://github.com/hsivonen/encoding_rs/issues/79#issuecomment-1211870361
        //
        // For long term, see https://doc.rust-lang.org/std/vec/struct.Vec.html#method.spare_capacity_mut.
        buffer.set_len(head.len());
    }
    encoding_rs::mem::convert_latin1_to_utf16(head, buffer);
    let mut expansion_buffer: Buffer = Buffer::new();
    if expansion_buffer.try_reserve_exact(tail.len()).is_err() {
        return false;
    }
    unsafe {
        // SAFETY: We have enough capacity. Exposure of slice of uninitialized
        // of integers for writing should be OK in practice:
        // https://github.com/hsivonen/encoding_rs/issues/79#issuecomment-1211870361
        //
        // For long term, see https://doc.rust-lang.org/std/vec/struct.Vec.html#method.spare_capacity_mut.
        expansion_buffer.set_len(tail.len());
    }
    encoding_rs::mem::convert_latin1_to_utf16(tail, &mut expansion_buffer);
    let r = match form {
        NormalizationForm::NFKC => {
            icu_normalizer::latin1::normalize_nfkc_to(&expansion_buffer, buffer)
        }
        NormalizationForm::NFD => {
            icu_normalizer::latin1::normalize_nfd_to(&expansion_buffer, buffer)
        }
        NormalizationForm::NFKD => {
            icu_normalizer::latin1::normalize_nfkd_to(&expansion_buffer, buffer)
        }
        NormalizationForm::NFC => {
            unreachable!("NFC should have been handled already");
        }
    };
    debug_assert!(r.is_ok());
    true
}

// The items below are not used by SpiderMonkey but are offered through headers that
// are supposed to work in SpiderMonkey.

#[no_mangle]
pub unsafe extern "C" fn mozilla_canonical_composition(a: u32, b: u32) -> u32 {
    icu_normalizer::properties::CanonicalCompositionBorrowed::new()
        .compose(
            char::from_u32(a).unwrap_or('\u{0}'),
            char::from_u32(b).unwrap_or('\u{0}'),
        )
        .unwrap_or('\u{0}')
        .into()
}

#[no_mangle]
pub unsafe extern "C" fn mozilla_canonical_combining_class(c: u32) -> u8 {
    icu_normalizer::properties::CanonicalCombiningClassMapBorrowed::new().get32_u8(c)
}
