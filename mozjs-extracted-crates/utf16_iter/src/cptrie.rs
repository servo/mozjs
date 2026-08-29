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

use crate::Utf16CharIndicesWithTrie;
use core::iter::FusedIterator;
use core::marker::PhantomData;
use icu_collections::codepointtrie::AbstractCodePointTrie;
use icu_collections::codepointtrie::TrieValue;
use icu_collections::codepointtrie::WithTrie;

/// Iterator by `char` and `icu_collections::codepointtrie::TrieValue`
/// over `&[u16]` that contains potentially-invalid UTF-16. See the
/// crate documentation.
#[derive(Debug)]
pub struct Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    remaining: &'slice [u16],
    trie: &'trie T,
    phantom: PhantomData<V>,
}

impl<'slice, 'trie, T, V> Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    /// Creates the iterator from a `u16` slice.
    pub fn new(code_units: &'slice [u16], trie: &'trie T) -> Self {
        Self {
            remaining: code_units,
            trie,
            phantom: PhantomData,
        }
    }

    /// Views the current remaining data in the iterator as a subslice
    /// of the original slice.
    #[inline(always)]
    pub fn as_slice(&self) -> &'slice [u16] {
        self.remaining
    }

    #[inline(never)]
    fn surrogate_next(&mut self, surrogate_base: u16, first: u16) -> (char, V) {
        if surrogate_base <= (0xDBFF - 0xD800) {
            if let Some((&low, tail_tail)) = self.remaining.split_first() {
                if crate::in_inclusive_range16(low, 0xDC00, 0xDFFF) {
                    self.remaining = tail_tail;
                    let code_point = (u32::from(first) << 10) + u32::from(low)
                        - (((0xD800u32 << 10) - 0x10000u32) + 0xDC00u32);
                    return unsafe {
                        (
                            char::from_u32_unchecked(code_point),
                            self.trie.supplementary(code_point),
                        )
                    };
                }
            }
        }
        ('\u{FFFD}', self.trie.bmp(0xFFFD))
    }

    #[inline(never)]
    fn surrogate_next_back(&mut self, last: u16) -> (char, V) {
        if crate::in_inclusive_range16(last, 0xDC00, 0xDFFF) {
            if let Some((&high, head_head)) = self.remaining.split_last() {
                if crate::in_inclusive_range16(high, 0xD800, 0xDBFF) {
                    self.remaining = head_head;
                    let code_point = (u32::from(high) << 10) + u32::from(last)
                        - (((0xD800u32 << 10) - 0x10000u32) + 0xDC00u32);
                    return unsafe {
                        (
                            char::from_u32_unchecked(code_point),
                            self.trie.supplementary(code_point),
                        )
                    };
                }
            }
        }
        ('\u{FFFD}', self.trie.bmp(0xFFFD))
    }
}

impl<'slice, 'trie, T, V> Clone for Utf16CharsWithTrie<'slice, 'trie, T, V>
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

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn trie(&self) -> &'trie T {
        self.trie
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (char, V);

    #[inline(always)]
    fn next(&mut self) -> Option<Self::Item> {
        let (&first, tail) = self.remaining.split_first()?;
        self.remaining = tail;
        let surrogate_base = first.wrapping_sub(0xD800);
        if surrogate_base > (0xDFFF - 0xD800) {
            return Some((
                unsafe { char::from_u32_unchecked(u32::from(first)) },
                self.trie.bmp(first),
            ));
        }
        Some(self.surrogate_next(surrogate_base, first))
    }
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    fn next_back(&mut self) -> Option<Self::Item> {
        let (&last, head) = self.remaining.split_last()?;
        self.remaining = head;
        if !crate::in_inclusive_range16(last, 0xD800, 0xDFFF) {
            return Some((
                unsafe { char::from_u32_unchecked(u32::from(last)) },
                self.trie.bmp(last),
            ));
        }
        Some(self.surrogate_next_back(last))
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf16CharsWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

/// Convenience trait that adds `chars_with_trie()` and `char_indices_with_trie()` methods
/// similar to the ones `icu_collections::codepointtrie::CharsWithTrieEx` adds to string
/// slices to `u16` slices.
pub trait Utf16CharsWithTrieEx<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf16CharsWithTrie<'slice, 'trie, T, V>;
    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf16CharIndicesWithTrie<'slice, 'trie, T, V>;
}

impl<'slice, 'trie, T, V> Utf16CharsWithTrieEx<'slice, 'trie, T, V> for [u16]
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    /// Convenience method for creating an UTF-16 iterator
    /// with trie values for the slice.
    #[inline]
    fn chars_with_trie(&'slice self, trie: &'trie T) -> Utf16CharsWithTrie<'slice, 'trie, T, V> {
        Utf16CharsWithTrie::new(self, trie)
    }

    /// Convenience method for creating a code unit index and
    /// UTF-16 iterator with trie values for the slice.
    #[inline]
    fn char_indices_with_trie(
        &'slice self,
        trie: &'trie T,
    ) -> Utf16CharIndicesWithTrie<'slice, 'trie, T, V> {
        Utf16CharIndicesWithTrie::new(self, trie)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_forward() {
        let trie = icu_collections::codepointtrie::planes::get_planes_trie();
        let s = &[0xD83Eu16, 0xDD73u16, 0xD83Eu16, 0x00E4u16, 0xD83Eu16];
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next(), Some(('🥳', 1)));
        assert_eq!(iter.next(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next(), Some(('\u{00E4}', 0)));
        assert_eq!(iter.next(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_backwards() {
        let trie = icu_collections::codepointtrie::planes::get_planes_trie();
        let s = &[0xD83Eu16, 0xDD73u16, 0xD83Eu16, 0x00E4u16, 0xD83Eu16];
        let mut iter = s.chars_with_trie(&trie);
        assert_eq!(iter.next_back(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next_back(), Some(('\u{00E4}', 0)));
        assert_eq!(iter.next_back(), Some(('\u{FFFD}', 0)));
        assert_eq!(iter.next_back(), Some(('🥳', 1)));
        assert_eq!(iter.next_back(), None);
    }
}
