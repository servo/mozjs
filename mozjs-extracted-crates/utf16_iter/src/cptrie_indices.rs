// The code in this file was adapted from the CharIndices implementation of
// the Rust standard library at revision ab32548539ec38a939c1b58599249f3b54130026
// (https://github.com/rust-lang/rust/blob/ab32548539ec38a939c1b58599249f3b54130026/library/core/src/str/iter.rs).
//
// Excerpt from https://github.com/rust-lang/rust/blob/ab32548539ec38a939c1b58599249f3b54130026/COPYRIGHT ,
// which refers to
// https://github.com/rust-lang/rust/blob/ab32548539ec38a939c1b58599249f3b54130026/LICENSE-APACHE
// and
// https://github.com/rust-lang/rust/blob/ab32548539ec38a939c1b58599249f3b54130026/LICENSE-MIT
// :
//
// For full authorship information, see the version control history or
// https://thanks.rust-lang.org
//
// Except as otherwise noted (below and/or in individual files), Rust is
// licensed under the Apache License, Version 2.0 <LICENSE-APACHE> or
// <http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT> or <http://opensource.org/licenses/MIT>, at your option.

use super::Utf16CharsWithTrie;
use core::iter::FusedIterator;

use icu_collections::codepointtrie::AbstractCodePointTrie;
use icu_collections::codepointtrie::TrieValue;
use icu_collections::codepointtrie::WithTrie;

/// An iterator over the [`char`]s  and their positions.
#[derive(Debug)]
#[must_use = "iterators are lazy and do nothing unless consumed"]
pub struct Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    front_offset: usize,
    iter: Utf16CharsWithTrie<'slice, 'trie, T, V>,
}

impl<'slice, 'trie, T, V> Clone for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn clone(&self) -> Self {
        Self {
            front_offset: self.front_offset,
            iter: self.iter.clone(),
        }
    }
}

impl<'slice, 'trie, T, V> WithTrie<'trie, T, V> for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn trie(&self) -> &'trie T {
        self.iter.trie()
    }
}

impl<'slice, 'trie, T, V> Iterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    type Item = (usize, char, V);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        let pre_len = self.as_slice().len();
        match self.iter.next() {
            None => None,
            Some((ch, v)) => {
                let index = self.front_offset;
                let len = self.as_slice().len();
                self.front_offset += pre_len - len;
                Some((index, ch, v))
            }
        }
    }

    #[inline]
    fn count(self) -> usize {
        self.iter.count()
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.iter.size_hint()
    }

    #[inline]
    fn last(mut self) -> Option<Self::Item> {
        // No need to go through the entire string.
        self.next_back()
    }
}

impl<'slice, 'trie, T, V> DoubleEndedIterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline]
    fn next_back(&mut self) -> Option<Self::Item> {
        self.iter.next_back().map(|(ch, v)| {
            let index = self.front_offset + self.as_slice().len();
            (index, ch, v)
        })
    }
}

impl<'slice, 'trie, T, V> FusedIterator for Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
}

impl<'slice, 'trie, T, V> Utf16CharIndicesWithTrie<'slice, 'trie, T, V>
where
    V: TrieValue,
    T: AbstractCodePointTrie<'trie, V>,
{
    #[inline(always)]
    /// Creates the iterator from a `u16` slice.
    pub fn new(code_units: &'slice [u16], trie: &'trie T) -> Self {
        Self {
            front_offset: 0,
            iter: Utf16CharsWithTrie::new(code_units, trie),
        }
    }

    /// Views the underlying data as a subslice of the original data.
    ///
    /// This has the same lifetime as the original slice, and so the
    /// iterator can continue to be used while this exists.
    #[must_use]
    #[inline]
    pub fn as_slice(&self) -> &'slice [u16] {
        self.iter.as_slice()
    }

    /// Returns the code unit position of the next character, or the length
    /// of the underlying string if there are no more characters.
    ///
    /// # Examples
    ///
    /// ```
    /// use utf16_iter::Utf16CharsEx;
    /// let mut chars = [0xD83Eu16, 0xDD73u16, 0x697Du16].char_indices();
    ///
    /// assert_eq!(chars.offset(), 0);
    /// assert_eq!(chars.next(), Some((0, '🥳')));
    ///
    /// assert_eq!(chars.offset(), 2);
    /// assert_eq!(chars.next(), Some((2, '楽')));
    ///
    /// assert_eq!(chars.offset(), 3);
    /// assert_eq!(chars.next(), None);
    /// ```
    #[inline]
    #[must_use]
    pub fn offset(&self) -> usize {
        self.front_offset
    }
}
