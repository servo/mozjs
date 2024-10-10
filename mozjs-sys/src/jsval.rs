/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use crate::jsapi::JSContext;
use crate::jsapi::JSObject;
use crate::jsapi::JSString;
use crate::jsapi::JSValueType;
use crate::jsapi::JS::BigInt;
use crate::jsapi::JS::Symbol;
use crate::jsapi::JS::TraceKind;
use crate::jsapi::JS::Value;

use std::default::Default;
use std::ffi::c_void;

pub type JSVal = Value;

#[cfg(target_pointer_width = "64")]
const JSVAL_TAG_SHIFT: usize = 47;

#[cfg(target_pointer_width = "32")]
const JSVAL_TAG_SHIFT: usize = 32;

const JSVAL_TAG_MAX_DOUBLE: u32 = 0x1FFF0;

#[cfg(target_pointer_width = "32")]
const JSVAL_TAG_CLEAR: u32 = 0xFFFFFF80;

#[cfg(target_pointer_width = "64")]
const JSVAL_TAG_BOX: u32 = JSVAL_TAG_MAX_DOUBLE;

#[cfg(target_pointer_width = "32")]
const JSVAL_TAG_BOX: u32 = JSVAL_TAG_CLEAR;

const CELL_ALIGN_MASK: u64 = (1 << 3) - 1;

#[repr(u32)]
#[allow(dead_code)]
enum ValueTag {
    INT32 = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_INT32 as u32),
    UNDEFINED = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_UNDEFINED as u32),
    NULL = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_NULL as u32),
    BOOLEAN = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_BOOLEAN as u32),
    MAGIC = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_MAGIC as u32),
    STRING = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_STRING as u32),
    SYMBOL = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_SYMBOL as u32),
    PRIVATE_GCTHING = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_PRIVATE_GCTHING as u32),
    BIGINT = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_BIGINT as u32),
    OBJECT = JSVAL_TAG_BOX | (JSValueType::JSVAL_TYPE_OBJECT as u32),
}

#[repr(u64)]
#[allow(dead_code)]
enum ValueShiftedTag {
    MAX_DOUBLE = ((JSVAL_TAG_MAX_DOUBLE as u64) << JSVAL_TAG_SHIFT) | 0xFFFFFFFF,
    INT32 = (ValueTag::INT32 as u64) << JSVAL_TAG_SHIFT,
    UNDEFINED = (ValueTag::UNDEFINED as u64) << JSVAL_TAG_SHIFT,
    NULL = (ValueTag::NULL as u64) << JSVAL_TAG_SHIFT,
    BOOLEAN = (ValueTag::BOOLEAN as u64) << JSVAL_TAG_SHIFT,
    MAGIC = (ValueTag::MAGIC as u64) << JSVAL_TAG_SHIFT,
    STRING = (ValueTag::STRING as u64) << JSVAL_TAG_SHIFT,
    SYMBOL = (ValueTag::SYMBOL as u64) << JSVAL_TAG_SHIFT,
    PRIVATE_GCTHING = (ValueTag::PRIVATE_GCTHING as u64) << JSVAL_TAG_SHIFT,
    BIGINT = (ValueTag::BIGINT as u64) << JSVAL_TAG_SHIFT,
    OBJECT = (ValueTag::OBJECT as u64) << JSVAL_TAG_SHIFT,
}

const JSVAL_PAYLOAD_MASK: u64 = 0x00007FFFFFFFFFFF;

#[inline(always)]
fn BuildJSVal(tag: ValueTag, payload: u64) -> JSVal {
    JSVal {
        asBits_: ((tag as u32 as u64) << JSVAL_TAG_SHIFT) | payload,
    }
}

#[cfg(target_pointer_width = "64")]
#[inline(always)]
fn AssertGCPointerValid(bits: u64) {
    assert_eq!(bits >> JSVAL_TAG_SHIFT, 0)
}

#[cfg(target_pointer_width = "32")]
#[inline(always)]
fn AssertGCPointerValid(bits: u64) {}

#[cfg(target_pointer_width = "64")]
#[inline(always)]
fn AssertGCPointerAlignment(bits: u64) {
    assert_eq!(bits & CELL_ALIGN_MASK, 0);
}

#[cfg(target_pointer_width = "32")]
#[inline(always)]
fn AssertGCPointerAlignment(bits: u64) {}

#[cfg(target_pointer_width = "64")]
#[inline(always)]
fn IsValidUserModePointer(bits: u64) -> bool {
    bits & 0xFFFF_0000_0000_0000 == 0
}

#[cfg(target_pointer_width = "32")]
#[inline(always)]
fn IsValidUserModePointer(_: u64) -> bool {
    true
}

#[inline(always)]
pub fn Int32Value(i: i32) -> JSVal {
    BuildJSVal(ValueTag::INT32, i as u32 as u64)
}

#[inline(always)]
pub fn UndefinedValue() -> JSVal {
    BuildJSVal(ValueTag::UNDEFINED, 0)
}

#[inline(always)]
pub fn NullValue() -> JSVal {
    BuildJSVal(ValueTag::NULL, 0)
}

#[inline(always)]
pub fn DoubleValue(f: f64) -> JSVal {
    let val = JSVal {
        asBits_: f.to_bits(),
    };
    assert!(val.is_double());
    val
}

#[inline(always)]
pub fn UInt32Value(ui: u32) -> JSVal {
    if ui > 0x7fffffff {
        DoubleValue(ui as f64)
    } else {
        Int32Value(ui as i32)
    }
}

#[inline(always)]
pub fn StringValue(s: &JSString) -> JSVal {
    let bits = s as *const JSString as usize as u64;
    AssertGCPointerValid(bits);
    BuildJSVal(ValueTag::STRING, bits)
}

#[inline(always)]
pub fn SymbolValue(s: &Symbol) -> JSVal {
    let bits = s as *const Symbol as usize as u64;
    AssertGCPointerValid(bits);
    BuildJSVal(ValueTag::SYMBOL, bits)
}

#[inline(always)]
pub fn BigIntValue(s: &BigInt) -> JSVal {
    let bits = s as *const BigInt as usize as u64;
    AssertGCPointerValid(bits);
    BuildJSVal(ValueTag::BIGINT, bits)
}

#[inline(always)]
pub fn BooleanValue(b: bool) -> JSVal {
    BuildJSVal(ValueTag::BOOLEAN, b as u64)
}

#[inline(always)]
pub fn ObjectValue(o: *mut JSObject) -> JSVal {
    let bits = o as usize as u64;
    AssertGCPointerValid(bits);
    BuildJSVal(ValueTag::OBJECT, bits)
}

#[inline(always)]
pub fn ObjectOrNullValue(o: *mut JSObject) -> JSVal {
    if o.is_null() {
        NullValue()
    } else {
        ObjectValue(o)
    }
}

#[inline(always)]
pub fn PrivateValue(o: *const c_void) -> JSVal {
    let ptrBits = o as usize as u64;
    assert!(IsValidUserModePointer(ptrBits));
    JSVal { asBits_: ptrBits }
}

impl JSVal {
    #[inline(always)]
    fn asBits(&self) -> u64 {
        self.asBits_
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    fn payload(&self) -> u64 {
        self.asBits_ & JSVAL_PAYLOAD_MASK
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    fn payload(&self) -> u32 {
        (self.asBits_ & 0xFFFFFFFF) as u32
    }

    #[inline(always)]
    fn toTag(&self) -> u64 {
        self.asBits_ >> JSVAL_TAG_SHIFT
    }

    #[inline(always)]
    pub fn is_undefined(&self) -> bool {
        self.asBits() == ValueShiftedTag::UNDEFINED as u64
    }

    #[inline(always)]
    pub fn is_null(&self) -> bool {
        self.asBits() == ValueShiftedTag::NULL as u64
    }

    #[inline(always)]
    pub fn is_null_or_undefined(&self) -> bool {
        self.is_null() || self.is_undefined()
    }

    #[inline(always)]
    pub fn is_int32(&self) -> bool {
        self.toTag() == ValueTag::INT32 as u64
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    pub fn is_double(&self) -> bool {
        self.asBits() <= ValueShiftedTag::MAX_DOUBLE as u64
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    pub fn is_double(&self) -> bool {
        (self.asBits() >> JSVAL_TAG_SHIFT) as u32 <= JSVAL_TAG_CLEAR
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    pub fn is_number(&self) -> bool {
        const JSVAL_UPPER_EXCL_SHIFTED_TAG_OF_NUMBER_SET: u64 = ValueShiftedTag::BOOLEAN as u64;
        self.asBits() < JSVAL_UPPER_EXCL_SHIFTED_TAG_OF_NUMBER_SET
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    pub fn is_number(&self) -> bool {
        assert_ne!(self.toTag(), JSVAL_TAG_CLEAR as u64);
        const JSVAL_UPPER_INCL_TAG_OF_NUMBER_SET: u64 = ValueTag::INT32 as u64;
        self.toTag() <= JSVAL_UPPER_INCL_TAG_OF_NUMBER_SET
    }

    #[inline(always)]
    pub fn is_string(&self) -> bool {
        self.toTag() == ValueTag::STRING as u64
    }

    #[inline(always)]
    pub fn is_symbol(&self) -> bool {
        self.toTag() == ValueTag::SYMBOL as u64
    }

    #[inline(always)]
    pub fn is_bigint(&self) -> bool {
        self.toTag() == ValueTag::BIGINT as u64
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    pub fn is_object(&self) -> bool {
        assert!(self.toTag() <= ValueTag::OBJECT as u64);
        self.asBits() >= ValueShiftedTag::OBJECT as u64
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    pub fn is_object(&self) -> bool {
        self.toTag() == ValueTag::OBJECT as u64
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    pub fn is_primitive(&self) -> bool {
        const JSVAL_UPPER_EXCL_SHIFTED_TAG_OF_PRIMITIVE_SET: u64 = ValueShiftedTag::OBJECT as u64;
        self.asBits() < JSVAL_UPPER_EXCL_SHIFTED_TAG_OF_PRIMITIVE_SET
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    pub fn is_primitive(&self) -> bool {
        const JSVAL_UPPER_EXCL_TAG_OF_PRIMITIVE_SET: u64 = ValueTag::OBJECT as u64;
        self.toTag() < JSVAL_UPPER_EXCL_TAG_OF_PRIMITIVE_SET
    }

    #[inline(always)]
    pub fn is_object_or_null(&self) -> bool {
        self.is_object() || self.is_null()
    }

    #[inline(always)]
    pub fn is_numeric(&self) -> bool {
        self.is_number() || self.is_bigint()
    }

    #[cfg(target_pointer_width = "64")]
    #[inline(always)]
    pub fn is_gcthing(&self) -> bool {
        const JSVAL_LOWER_INCL_SHIFTED_TAG_OF_GCTHING_SET: u64 = ValueShiftedTag::STRING as u64;
        self.asBits() >= JSVAL_LOWER_INCL_SHIFTED_TAG_OF_GCTHING_SET
    }

    #[cfg(target_pointer_width = "32")]
    #[inline(always)]
    pub fn is_gcthing(&self) -> bool {
        const JSVAL_LOWER_INCL_TAG_OF_GCTHING_SET: u64 = ValueTag::STRING as u64;
        self.toTag() >= JSVAL_LOWER_INCL_TAG_OF_GCTHING_SET
    }

    #[inline(always)]
    pub fn is_boolean(&self) -> bool {
        (self.asBits() >> JSVAL_TAG_SHIFT) == ValueTag::BOOLEAN as u64
    }

    #[inline(always)]
    pub fn is_magic(&self) -> bool {
        self.toTag() == ValueTag::MAGIC as u64
    }

    #[inline(always)]
    pub fn to_int32(&self) -> i32 {
        assert!(self.is_int32());
        (self.asBits() & 0xFFFFFFFF) as i32
    }

    #[inline(always)]
    pub fn to_double(&self) -> f64 {
        assert!(self.is_double());
        f64::from_bits(self.asBits())
    }

    #[inline(always)]
    pub fn to_number(&self) -> f64 {
        assert!(self.is_number());
        if self.is_double() {
            self.to_double()
        } else {
            self.to_int32() as f64
        }
    }

    #[inline(always)]
    pub fn to_string(&self) -> *mut JSString {
        assert!(self.is_string());
        let ptrBits = self.payload();
        AssertGCPointerAlignment(ptrBits as u64);
        ptrBits as usize as *mut JSString
    }

    #[inline(always)]
    pub fn to_symbol(&self) -> *mut Symbol {
        assert!(self.is_symbol());
        let ptrBits = self.payload();
        AssertGCPointerAlignment(ptrBits as u64);
        ptrBits as usize as *mut Symbol
    }

    #[inline(always)]
    pub fn to_bigint(&self) -> *mut BigInt {
        assert!(self.is_bigint());
        let ptrBits = self.payload();
        AssertGCPointerAlignment(ptrBits as u64);
        ptrBits as usize as *mut BigInt
    }

    #[inline(always)]
    pub fn to_object(&self) -> *mut JSObject {
        assert!(self.is_object());
        self.to_object_or_null()
    }

    #[inline(always)]
    #[cfg(target_pointer_width = "64")]
    pub fn to_object_or_null(&self) -> *mut JSObject {
        assert!(self.is_object_or_null());
        let ptrBits = self.payload();
        AssertGCPointerAlignment(ptrBits);
        ptrBits as usize as *mut JSObject
    }

    #[inline(always)]
    #[cfg(target_pointer_width = "32")]
    pub fn to_object_or_null(&self) -> *mut JSObject {
        assert!(self.is_object_or_null());
        let ptrBits: u32 = (self.asBits() & 0x00000000FFFFFFFF) as u32;
        ptrBits as *mut JSObject
    }

    #[inline(always)]
    pub fn to_gcthing(&self) -> *mut c_void {
        assert!(self.is_gcthing());
        let ptrBits = self.payload();
        AssertGCPointerAlignment(ptrBits as u64);
        ptrBits as *mut c_void
    }

    #[inline(always)]
    pub fn to_boolean(&self) -> bool {
        assert!(self.is_boolean());
        self.payload() != 0
    }

    #[inline(always)]
    pub fn is_private(&self) -> bool {
        self.is_double() && IsValidUserModePointer(self.asBits())
    }

    #[inline(always)]
    pub fn to_private(&self) -> *const c_void {
        assert!(self.is_private());
        self.asBits() as usize as *const c_void
    }

    #[inline(always)]
    pub fn is_markable(&self) -> bool {
        self.is_gcthing() && !self.is_null()
    }

    #[inline(always)]
    pub fn trace_kind(&self) -> TraceKind {
        assert!(self.is_markable());
        if self.is_object() {
            TraceKind::Object
        } else if self.is_string() {
            TraceKind::String
        } else if self.is_symbol() {
            TraceKind::Symbol
        } else {
            TraceKind::BigInt
        }
    }
}

impl Default for JSVal {
    #[inline(always)]
    fn default() -> JSVal {
        UndefinedValue()
    }
}

#[inline(always)]
pub unsafe fn JS_ARGV(_cx: *mut JSContext, vp: *mut JSVal) -> *mut JSVal {
    vp.offset(2)
}

#[inline(always)]
pub unsafe fn JS_CALLEE(_cx: *mut JSContext, vp: *mut JSVal) -> JSVal {
    *vp
}

// These tests make sure that the Rust definitions agree with the C++ definitions.
#[test]
fn test_representation_agreement() {
    // Annoyingly, we can't check JSObject, JSString, etc. without creating a runtime,
    // since the constructor has checks that fail if we try mocking.  There are no-check
    // versions of the setters, but they're private.
    use crate::jsapi::glue::*;
    let mut val1 = UndefinedValue();
    let mut val2;

    unsafe {
        JS_ValueSetBoolean(&mut val1, true);
    }
    val2 = BooleanValue(true);
    assert_agreement(val1, val2);

    unsafe {
        JS_ValueSetDouble(&mut val1, 3.14159);
    }
    val2 = DoubleValue(3.14159);
    assert_agreement(val1, val2);

    unsafe {
        JS_ValueSetInt32(&mut val1, 37);
    }
    val2 = Int32Value(37);
    assert_agreement(val1, val2);

    unsafe {
        JS_ValueSetNull(&mut val1);
    }
    val2 = NullValue();
    assert_agreement(val1, val2);
}

#[cfg(test)]
fn assert_agreement(val1: JSVal, val2: JSVal) {
    use crate::jsapi::glue::*;

    assert_eq!(val1.asBits(), val2.asBits());

    assert_eq!(unsafe { JS_ValueIsBoolean(&val1) }, val2.is_boolean());
    if val2.is_boolean() {
        assert_eq!(unsafe { JS_ValueToBoolean(&val1) }, val2.to_boolean());
    }

    assert_eq!(unsafe { JS_ValueIsDouble(&val1) }, val2.is_double());
    if val2.is_double() {
        assert_eq!(unsafe { JS_ValueToDouble(&val1) }, val2.to_double());
    }

    assert_eq!(unsafe { JS_ValueIsInt32(&val1) }, val2.is_int32());
    if val2.is_int32() {
        assert_eq!(unsafe { JS_ValueToInt32(&val1) }, val2.to_int32());
    }

    assert_eq!(unsafe { JS_ValueIsNumber(&val1) }, val2.is_number());
    if val2.is_number() {
        assert_eq!(unsafe { JS_ValueToNumber(&val1) }, val2.to_number());
    }

    assert_eq!(unsafe { JS_ValueIsNull(&val1) }, val2.is_null());

    assert_eq!(unsafe { JS_ValueIsUndefined(&val1) }, val2.is_undefined());
}
