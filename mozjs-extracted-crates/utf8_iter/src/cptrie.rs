// Copyright Mozilla Foundation
//
// Licensed under the Apache License (Version 2.0), or the MIT license,
// (the "Licenses") at your option. You may not use this file except in
// compliance with one of the Licenses. You may obtain copies of the
// Licenses at:
//
//    https://www.apache.org/licenses/LICENSE-2.0
//    https://opensource.org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Licenses is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the Licenses for the specific language governing permissions and
// limitations under the Licenses.

use crate::in_inclusive_range8;
use crate::Utf8CharIndicesWithTrie;
use crate::UTF8_DATA;
use core::iter::FusedIterator;
use core::marker::PhantomData;
use icu_collections::codepointtrie::AbstractCodePointTrie;
use icu_collections::codepointtrie::TrieValue;
use icu_collections::codepointtrie::WithTrie;

/// Iterator by `char` and `icu_collections::codepointtrie::TrieValue`
/// over `&[u8]` that contains potentially-invalid UTF-8. See the
/// crate documentation.
#[derive(Debug)]
pub struct Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    remaining: &'slice [u8],
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    /// Creates the iterator from a byte slice.
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            remaining: bytes,
            trie,
            phantom: PhantomData,
        }
    }

    /// Views the current remaining data in the iterator as a subslice
    /// of the original slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.remaining
    }

    #[inline(never)]
    fn next_fallback(&mut self) -> Option<(char, V)> {
        if self.remaining.is_empty() {
            return None;
        }
        let first = self.remaining[0];
        if first < 0x80 {
            self.remaining = &self.remaining[1..];
            // SAFETY: We just checked the precondition of `ascii()` above.
            return Some((char::from(first), unsafe { self.trie.ascii(first) }));
        }
        if !in_inclusive_range8(first, 0xC2, 0xF4) || self.remaining.len() == 1 {
            self.remaining = &self.remaining[1..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        let second = self.remaining[1];
        let (lower_bound, upper_bound) = match first {
            0xE0 => (0xA0, 0xBF),
            0xED => (0x80, 0x9F),
            0xF0 => (0x90, 0xBF),
            0xF4 => (0x80, 0x8F),
            _ => (0x80, 0xBF),
        };
        if !in_inclusive_range8(second, lower_bound, upper_bound) {
            self.remaining = &self.remaining[1..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        if first < 0xE0 {
            self.remaining = &self.remaining[2..];
            let high_five = u32::from(first) & 0b11_111;
            let low_six = u32::from(second) & 0b111_111;
            // SAFETY: `high_five` and `low_six` conform to the
            // precondition of `utf8_two_byte` by construction.
            let v = unsafe { self.trie.utf8_two_byte(high_five, low_six) };
            let point = (high_five << 6) | low_six;
            // SAFETY: `point` is in the scalar value range, because
            // we've checked that `first` is a valid lead byte and
            // we've then masked five bits from `first` and six bits
            // from `second`.
            return Some((unsafe { char::from_u32_unchecked(point) }, v));
        }
        if self.remaining.len() == 2 {
            self.remaining = &self.remaining[2..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        let third = self.remaining[2];
        if !in_inclusive_range8(third, 0x80, 0xBF) {
            self.remaining = &self.remaining[2..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        if first < 0xF0 {
            self.remaining = &self.remaining[3..];
            let high_ten = ((u32::from(first) & 0b1111) << 6) | (u32::from(second) & 0b111_111);
            let low_six = u32::from(third) & 0b111_111;
            // SAFETY: `high_ten` and `low_six` conform to the
            // precondition of `utf8_three_byte` by construction.
            let v = unsafe { self.trie.utf8_three_byte(high_ten, low_six) };
            let point = (high_ten << 6) | low_six;
            // SAFETY: `point` is in the scalar value range, because
            // we've checked that `first` is a valid lead byte and
            // we've then masked four bits from `first` and six bits
            // from both `second` and `third`.
            return Some((unsafe { char::from_u32_unchecked(point) }, v));
        }
        // At this point, we have a valid 3-byte prefix of a
        // four-byte sequence that has to be incomplete, because
        // otherwise `next()` would have succeeded.
        self.remaining = &self.remaining[3..];
        Some(('\u{FFFD}', self.trie.bmp(0xFFFD)))
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn clone(&self) -> Self {
        Self {
            remaining: self.remaining,
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        // This loop is only broken out of as goto forward
        #[allow(clippy::never_loop)]
        loop {
            if self.remaining.len() < 4 {
                break;
            }
            let first = self.remaining[0];
            if first < 0x80 {
                self.remaining = &self.remaining[1..];
                // SAFETY: We just checked the precondition of `ascii()` above.
                return Some((char::from(first), unsafe { self.trie.ascii(first) }));
            }
            let second = self.remaining[1];
            if in_inclusive_range8(first, 0xC2, 0xDF) {
                if !in_inclusive_range8(second, 0x80, 0xBF) {
                    break;
                }
                self.remaining = &self.remaining[2..];
                let high_five = u32::from(first) & 0b11_111;
                let low_six = u32::from(second) & 0b111_111;
                // SAFETY: `high_five` and `low_six` conform to the
                // precondition of `utf8_two_byte` by construction.
                let v = unsafe { self.trie.utf8_two_byte(high_five, low_six) };
                let point = (high_five << 6) | low_six;
                // SAFETY: `point` is in the scalar value range, because
                // we've checked that `first` is a valid lead byte and
                // we've then masked five bits from `first` and six bits
                // from `second`.
                return Some((unsafe { char::from_u32_unchecked(point) }, v));
            }
            // This table-based formulation was benchmark-based in encoding_rs,
            // but it hasn't been re-benchmarked in this iterator context.
            let third = self.remaining[2];
            if first < 0xF0 {
                if ((UTF8_DATA.table[usize::from(second)]
                    & UTF8_DATA.table[usize::from(first) + 0x80])
                    | (third >> 6))
                    != 2
                {
                    break;
                }
                self.remaining = &self.remaining[3..];
                let high_ten = ((u32::from(first) & 0b1111) << 6) | (u32::from(second) & 0b111_111);
                let low_six = u32::from(third) & 0b111_111;
                // SAFETY: `high_ten` and `low_six` conform to the
                // precondition of `utf8_three_byte` by construction.
                let v = unsafe { self.trie.utf8_three_byte(high_ten, low_six) };
                let point = (high_ten << 6) | low_six;
                // SAFETY: `point` is in the scalar value range, because
                // we've checked that `first` is a valid lead byte and
                // we've then masked four bits from `first` and six bits
                // from both `second` and `third`.
                return Some((unsafe { char::from_u32_unchecked(point) }, v));
            }
            let fourth = self.remaining[3];
            if (u16::from(
                UTF8_DATA.table[usize::from(second)] & UTF8_DATA.table[usize::from(first) + 0x80],
            ) | u16::from(third >> 6)
                | (u16::from(fourth & 0xC0) << 2))
                != 0x202
            {
                break;
            }
            let point = ((u32::from(first) & 0x7) << 18)
                | ((u32::from(second) & 0x3F) << 12)
                | ((u32::from(third) & 0x3F) << 6)
                | (u32::from(fourth) & 0x3F);
            self.remaining = &self.remaining[4..];
            // SAFETY: We've validated that `first` is a valid four-byte lead,
            // taken 3 low bits from it, and six low bits from each trail.
            return Some((
                unsafe { char::from_u32_unchecked(point) },
                self.trie.supplementary(point),
            ));
        }
        self.next_fallback()
    }
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn next_back(&mut self) -> Option<(char, V)> {
        if self.remaining.is_empty() {
            return None;
        }
        let mut attempt = 1;
        for b in self.remaining.iter().rev() {
            if b & 0xC0 != 0x80 {
                let (head, tail) = self.remaining.split_at(self.remaining.len() - attempt);
                let mut inner = Utf8CharsWithTrie::new(tail, self.trie);
                let candidate = inner.next();
                if inner.as_slice().is_empty() {
                    self.remaining = head;
                    return candidate;
                }
                break;
            }
            if attempt == 4 {
                break;
            }
            attempt += 1;
        }

        self.remaining = &self.remaining[..self.remaining.len() - 1];
        Some(('\u{FFFD}', self.trie.bmp(0xFFFD)))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf8CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

/// Convenience trait that adds `chars_with_trie()` and `char_indices_with_trie()` methods
/// similar to the ones `icu_collections::codepointtrie::CharsWithTrieEx` adds to string
/// slices to `u8` slices.
pub trait Utf8CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf8CharsWithTrie<'slice, 'trie, T, V>;
    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieEx<'slice, 'trie, T, V> for [u8]
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    #[inline]
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf8CharsWithTrie<'slice, 'trie, T, V> {
        Utf8CharsWithTrie::new(self, trie)
    }

    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    #[inline]
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V> {
        Utf8CharIndicesWithTrie::new(self, trie)
    }
}

// --

/// Iterator by `char` and `icu_collections::codepointtrie::TrieValue`
/// over `&[u8]` that contains potentially-invalid UTF-8. Uses `V::default()`
/// for ASCII instead of reading from the trie. See the
/// crate documentation.
#[derive(Debug)]
pub struct Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    remaining: &'slice [u8],
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    /// Creates the iterator from a byte slice.
    pub fn new(bytes: &'slice [u8], trie: &'trie T) -> Self {
        Self {
            remaining: bytes,
            trie,
            phantom: PhantomData,
        }
    }

    /// Views the current remaining data in the iterator as a subslice
    /// of the original slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u8] {
        self.remaining
    }

    #[inline(never)]
    fn next_fallback(&mut self) -> Option<(char, V)> {
        if self.remaining.is_empty() {
            return None;
        }
        let first = self.remaining[0];
        if first < 0x80 {
            self.remaining = &self.remaining[1..];
            return Some((char::from(first), V::default()));
        }
        if !in_inclusive_range8(first, 0xC2, 0xF4) || self.remaining.len() == 1 {
            self.remaining = &self.remaining[1..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        let second = self.remaining[1];
        let (lower_bound, upper_bound) = match first {
            0xE0 => (0xA0, 0xBF),
            0xED => (0x80, 0x9F),
            0xF0 => (0x90, 0xBF),
            0xF4 => (0x80, 0x8F),
            _ => (0x80, 0xBF),
        };
        if !in_inclusive_range8(second, lower_bound, upper_bound) {
            self.remaining = &self.remaining[1..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        if first < 0xE0 {
            self.remaining = &self.remaining[2..];
            let high_five = u32::from(first) & 0b11_111;
            let low_six = u32::from(second) & 0b111_111;
            // SAFETY: `high_five` and `low_six` conform to the
            // precondition of `utf8_two_byte` by construction.
            let v = unsafe { self.trie.utf8_two_byte(high_five, low_six) };
            let point = (high_five << 6) | low_six;
            // SAFETY: `point` is in the scalar value range, because
            // we've checked that `first` is a valid lead byte and
            // we've then masked five bits from `first` and six bits
            // from `second`.
            return Some((unsafe { char::from_u32_unchecked(point) }, v));
        }
        if self.remaining.len() == 2 {
            self.remaining = &self.remaining[2..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        let third = self.remaining[2];
        if !in_inclusive_range8(third, 0x80, 0xBF) {
            self.remaining = &self.remaining[2..];
            return Some(('\u{FFFD}', self.trie.bmp(0xFFFD)));
        }
        if first < 0xF0 {
            self.remaining = &self.remaining[3..];
            let high_ten = ((u32::from(first) & 0b1111) << 6) | (u32::from(second) & 0b111_111);
            let low_six = u32::from(third) & 0b111_111;
            // SAFETY: `high_ten` and `low_six` conform to the
            // precondition of `utf8_three_byte` by construction.
            let v = unsafe { self.trie.utf8_three_byte(high_ten, low_six) };
            let point = (high_ten << 6) | low_six;
            // SAFETY: `point` is in the scalar value range, because
            // we've checked that `first` is a valid lead byte and
            // we've then masked four bits from `first` and six bits
            // from both `second` and `third`.
            return Some((unsafe { char::from_u32_unchecked(point) }, v));
        }
        // At this point, we have a valid 3-byte prefix of a
        // four-byte sequence that has to be incomplete, because
        // otherwise `next()` would have succeeded.
        self.remaining = &self.remaining[3..];
        Some(('\u{FFFD}', self.trie.bmp(0xFFFD)))
    }
}

impl<'slice, 'trie, T, V> Clone for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn clone(&self) -> Self {
        Self {
            remaining: self.remaining,
            trie: self.trie,
            phantom: PhantomData,
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V>
    for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        // This loop is only broken out of as goto forward
        #[allow(clippy::never_loop)]
        loop {
            if self.remaining.len() < 4 {
                break;
            }
            let first = self.remaining[0];
            if first < 0x80 {
                self.remaining = &self.remaining[1..];
                return Some((char::from(first), V::default()));
            }
            let second = self.remaining[1];
            if in_inclusive_range8(first, 0xC2, 0xDF) {
                if !in_inclusive_range8(second, 0x80, 0xBF) {
                    break;
                }
                self.remaining = &self.remaining[2..];
                let high_five = u32::from(first) & 0b11_111;
                let low_six = u32::from(second) & 0b111_111;
                // SAFETY: `high_five` and `low_six` conform to the
                // precondition of `utf8_two_byte` by construction.
                let v = unsafe { self.trie.utf8_two_byte(high_five, low_six) };
                let point = (high_five << 6) | low_six;
                // SAFETY: `point` is in the scalar value range, because
                // we've checked that `first` is a valid lead byte and
                // we've then masked five bits from `first` and six bits
                // from `second`.
                return Some((unsafe { char::from_u32_unchecked(point) }, v));
            }
            // This table-based formulation was benchmark-based in encoding_rs,
            // but it hasn't been re-benchmarked in this iterator context.
            let third = self.remaining[2];
            if first < 0xF0 {
                if ((UTF8_DATA.table[usize::from(second)]
                    & UTF8_DATA.table[usize::from(first) + 0x80])
                    | (third >> 6))
                    != 2
                {
                    break;
                }
                self.remaining = &self.remaining[3..];
                let high_ten = ((u32::from(first) & 0b1111) << 6) | (u32::from(second) & 0b111_111);
                let low_six = u32::from(third) & 0b111_111;
                // SAFETY: `high_ten` and `low_six` conform to the
                // precondition of `utf8_three_byte` by construction.
                let v = unsafe { self.trie.utf8_three_byte(high_ten, low_six) };
                let point = (high_ten << 6) | low_six;
                // SAFETY: `point` is in the scalar value range, because
                // we've checked that `first` is a valid lead byte and
                // we've then masked four bits from `first` and six bits
                // from both `second` and `third`.
                return Some((unsafe { char::from_u32_unchecked(point) }, v));
            }
            let fourth = self.remaining[3];
            if (u16::from(
                UTF8_DATA.table[usize::from(second)] & UTF8_DATA.table[usize::from(first) + 0x80],
            ) | u16::from(third >> 6)
                | (u16::from(fourth & 0xC0) << 2))
                != 0x202
            {
                break;
            }
            let point = ((u32::from(first) & 0x7) << 18)
                | ((u32::from(second) & 0x3F) << 12)
                | ((u32::from(third) & 0x3F) << 6)
                | (u32::from(fourth) & 0x3F);
            self.remaining = &self.remaining[4..];
            // SAFETY: We've validated that `first` is a valid four-byte lead,
            // taken 3 low bits from it, and six low bits from each trail.
            return Some((
                unsafe { char::from_u32_unchecked(point) },
                self.trie.supplementary(point),
            ));
        }
        self.next_fallback()
    }
}

impl<'slice, 'trie, T, V> DoubleEndedIterator
    for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn next_back(&mut self) -> Option<(char, V)> {
        if self.remaining.is_empty() {
            return None;
        }
        let mut attempt = 1;
        for b in self.remaining.iter().rev() {
            if b & 0xC0 != 0x80 {
                let (head, tail) = self.remaining.split_at(self.remaining.len() - attempt);
                let mut inner = Utf8CharsWithTrieDefaultForAscii::new(tail, self.trie);
                let candidate = inner.next();
                if inner.as_slice().is_empty() {
                    self.remaining = head;
                    return candidate;
                }
                break;
            }
            if attempt == 4 {
                break;
            }
            attempt += 1;
        }

        self.remaining = &self.remaining[..self.remaining.len() - 1];
        Some(('\u{FFFD}', self.trie.bmp(0xFFFD)))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
}

/// Convenience trait that adds `chars_with_trie_default_for_ascii()` and `char_indices_with_trie_default_for_ascii()` methods
/// similar to the ones `icu_collections::codepointtrie::CharsWithTrieEx` adds to string
/// slices to `u8` slices.
pub trait Utf8CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V>
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V>;
    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf8CharsWithTrieDefaultForAsciiEx<'slice, 'trie, T, V> for [u8]
where
    V: TrieValue + Default,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    #[inline]
    fn chars_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharsWithTrieDefaultForAscii<'slice, 'trie, T, V> {
        Utf8CharsWithTrieDefaultForAscii::new(self, trie)
    }

    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    #[inline]
    fn char_indices_with_trie_default_for_ascii(
        &'slice self,
        trie: &'trie T,
    ) -> Utf8CharIndicesWithTrie<'slice, 'trie, T, V> {
        Utf8CharIndicesWithTrie::new(self, trie)
    }
}
