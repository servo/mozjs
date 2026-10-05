/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_StringFlags_h
#define vm_StringFlags_h

#include <stdint.h>

#include "jstypes.h"

#include "gc/Cell.h"
#include "js/shadow/String.h"  // JS::shadow::String

namespace js {

enum class CharEncoding : bool { Latin1 = true, TwoByte = false };

template <typename CharT>
constexpr CharEncoding CharEncodingFromType() {
  static_assert(std::is_same_v<CharT, JS::Latin1Char> ||
                std::is_same_v<CharT, char16_t>);
  if constexpr (std::is_same_v<CharT, JS::Latin1Char>) {
    return CharEncoding::Latin1;
  }

  return CharEncoding::TwoByte;
}

constexpr CharEncoding CharEncodingFromIsLatin1(bool isLatin1) {
  if (isLatin1) {
    return CharEncoding::Latin1;
  }

  return CharEncoding::TwoByte;
}

/*
 * JSString Flag Encoding
 *
 * If LATIN1_CHARS_BIT is set, the string's characters are stored as Latin1
 * instead of TwoByte. This flag can also be set for ropes, if both the left and
 * right nodes are Latin1. Flattening will result in a Latin1 string in this
 * case. When we flatten a TwoByte rope, we turn child ropes (including Latin1
 * ropes) into TwoByte dependent strings. If one of these strings is also part
 * of another Latin1 rope tree, we can have a Latin1 rope with a TwoByte
 * descendent.
 *
 * The other flags store the string's type. Instead of using a dense index to
 * represent the most-derived type, string types are encoded to allow single-op
 * tests for hot queries (isRope, isDependent, isAtom) which, in view of
 * subtyping, would require slower (isX() || isY() || isZ()).
 *
 * The string type encoding can be summarized as follows. The "instance
 * encoding" entry for a type specifies the flag bits used to create a string
 * instance of that type. Abstract types have no instances and thus have no such
 * entry. The "subtype predicate" entry for a type specifies the predicate used
 * to query whether a JSString instance is subtype (reflexively) of that type.
 *
 *   String         Instance        Subtype
 *   type           encoding        predicate
 *   -----------------------------------------
 *   Rope           0000000 000     xxxxx0x xxx
 *   Linear         0000010 000     xxxxx1x xxx
 *   Dependent      0000110 000     xxxx1xx xxx
 *   AtomRef        1000110 000     1xxxxxx xxx
 *   External       0100010 000     x100010 xxx
 *   Extensible     0010010 000     x010010 xxx
 *   Inline         0001010 000     xxx1xxx xxx
 *   FatInline      0011010 000     xx11xxx xxx
 *   JSAtom         -               xxxxxx1 xxx
 *   NormalAtom     0000011 000     xxx0xx1 xxx
 *   PermanentAtom  0100011 000     x1xxxx1 xxx
 *   ThinInlineAtom 0001011 000     xx01xx1 xxx
 *   FatInlineAtom  0011011 000     xx11xx1 xxx
 *                                  ||||||| |||
 *                                  ||||||| ||\- [0] reserved (FORWARD_BIT)
 *                                  ||||||| |\-- [1] reserved
 *                                  ||||||| \--- [2] reserved
 *                                  ||||||\----- [3] IsAtom
 *                                  |||||\------ [4] IsLinear
 *                                  ||||\------- [5] IsDependent
 *                                  |||\-------- [6] IsInline
 *                                  ||\--------- [7] FatInlineAtom/Extensible
 *                                  |\---------- [8] External/Permanent
 *                                  \----------- [9] AtomRef
 *
 * Bits 0..2 are reserved for use by the GC (see
 * gc::CellFlagBitsReservedForGC). In particular, bit 0 is currently used for
 * FORWARD_BIT for forwarded nursery cells. The other 2 bits are currently
 * unused.
 *
 * Note that the first 4 flag bits 3..6 (from right to left in the previous
 * table) have the following meaning and can be used for some hot queries:
 *
 *   Bit 3: IsAtom (Atom, PermanentAtom)
 *   Bit 4: IsLinear
 *   Bit 5: IsDependent
 *   Bit 6: IsInline (Inline, FatInline, ThinInlineAtom, FatInlineAtom)
 *
 * If INDEX_VALUE_BIT is set, bits 16 and up will also hold an integer index.
 */
class StringFlags {
 public:
  // The low bits of flag word are reserved by GC.
  static_assert(js::gc::CellFlagBitsReservedForGC <= 3,
                "JSString::flags must reserve enough bits for Cell");

  static constexpr uint32_t ATOM_BIT = js::Bit(3);
  static constexpr uint32_t LINEAR_BIT = js::Bit(4);
  static constexpr uint32_t DEPENDENT_BIT = js::Bit(5);
  static constexpr uint32_t INLINE_CHARS_BIT = js::Bit(6);

  // Indicates a dependent string pointing to an atom.
  static constexpr uint32_t ATOM_REF_BIT = js::Bit(9);

  static constexpr uint32_t LINEAR_IS_EXTENSIBLE_BIT = js::Bit(7);
  static constexpr uint32_t INLINE_IS_FAT_BIT = js::Bit(7);

  static constexpr uint32_t LINEAR_IS_EXTERNAL_BIT = js::Bit(8);
  static constexpr uint32_t ATOM_IS_PERMANENT_BIT = js::Bit(8);

  static constexpr uint32_t EXTENSIBLE_FLAGS =
      LINEAR_BIT | LINEAR_IS_EXTENSIBLE_BIT;
  static constexpr uint32_t EXTERNAL_FLAGS =
      LINEAR_BIT | LINEAR_IS_EXTERNAL_BIT;

  static constexpr uint32_t FAT_INLINE_MASK =
      INLINE_CHARS_BIT | INLINE_IS_FAT_BIT;

  // Initial flags for various types of strings.
  static constexpr uint32_t INIT_THIN_INLINE_FLAGS =
      LINEAR_BIT | INLINE_CHARS_BIT;
  static constexpr uint32_t INIT_FAT_INLINE_FLAGS =
      LINEAR_BIT | FAT_INLINE_MASK;
  static constexpr uint32_t INIT_ROPE_FLAGS = 0;
  static constexpr uint32_t INIT_LINEAR_FLAGS = LINEAR_BIT;
  static constexpr uint32_t INIT_DEPENDENT_FLAGS = LINEAR_BIT | DEPENDENT_BIT;
  static constexpr uint32_t INIT_ATOM_REF_FLAGS =
      INIT_DEPENDENT_FLAGS | ATOM_REF_BIT;

  static constexpr uint32_t TYPE_FLAGS_MASK = js::BitMask(10) - js::BitMask(3);
  static_assert((TYPE_FLAGS_MASK & js::gc::HeaderWord::RESERVED_MASK) == 0,
                "GC reserved bits must not be used for Strings");

  // Whether this atom's characters store an uint32 index value less than or
  // equal to MAX_ARRAY_INDEX. This bit means something different if the
  // string is not an atom (see ATOM_REF_BIT)
  static constexpr uint32_t ATOM_IS_INDEX_BIT = js::Bit(9);

  // Linear strings:
  // - Content and representation are Latin-1 characters.
  // - Unmodifiable after construction.
  //
  // Ropes:
  // - Content are Latin-1 characters.
  // - Flag may be cleared when the rope is changed into a dependent string.
  static constexpr uint32_t LATIN1_CHARS_BIT = js::Bit(10);

  // Linear strings only.
  static constexpr uint32_t INDEX_VALUE_BIT = js::Bit(11);
  static constexpr uint32_t INDEX_VALUE_SHIFT = 16;

  // Whether this is a non-inline linear string with a refcounted
  // mozilla::StringBuffer.
  static constexpr uint32_t HAS_STRING_BUFFER_BIT = js::Bit(12);

  // NON_DEDUP_BIT is used in string deduplication during tenuring. This bit is
  // shared with both FLATTEN_FINISH_NODE and ATOM_IS_PERMANENT_BIT, since it
  // only applies to linear non-atoms.
  static constexpr uint32_t NON_DEDUP_BIT = js::Bit(15);

  // If IN_STRING_TO_ATOM_CACHE is set, this string had an entry in the
  // StringToAtomCache at some point. Note that GC can purge the cache without
  // clearing this bit.
  static constexpr uint32_t IN_STRING_TO_ATOM_CACHE = js::Bit(13);

  // Flags used during rope flattening that indicate what action to perform when
  // returning to the rope's parent rope.
  static constexpr uint32_t FLATTEN_VISIT_RIGHT = js::Bit(14);
  static constexpr uint32_t FLATTEN_FINISH_NODE = js::Bit(15);
  static constexpr uint32_t FLATTEN_MASK =
      FLATTEN_VISIT_RIGHT | FLATTEN_FINISH_NODE;

  // Indicates that this string is depended on by another string. A rope should
  // never be depended on, and this should never be set during flattening, so
  // we can reuse the FLATTEN_VISIT_RIGHT bit.
  static constexpr uint32_t DEPENDED_ON_BIT = FLATTEN_VISIT_RIGHT;

  static constexpr uint32_t PINNED_ATOM_BIT = js::Bit(15);
  static constexpr uint32_t PERMANENT_ATOM_MASK =
      ATOM_BIT | PINNED_ATOM_BIT | ATOM_IS_PERMANENT_BIT;

  // When doing a placement new or simple flags update to reinitialize a
  // JSString with a different representation subtype, keep these bits. There
  // are different bitsets here for which string type we're coming from.
  static constexpr uint32_t PRESERVE_LINEAR_NONATOM_BITS_ON_REPLACE =
      DEPENDED_ON_BIT | IN_STRING_TO_ATOM_CACHE | INDEX_VALUE_BIT |
      ~uint32_t(0) << INDEX_VALUE_SHIFT;
  static constexpr uint32_t PRESERVE_ROPE_BITS_ON_REPLACE =
      IN_STRING_TO_ATOM_CACHE;

  static_assert(ATOM_BIT == JS::shadow::String::ATOM_BIT,
                "shadow::String::ATOM_BIT must match js::StringFlags");
  static_assert(LINEAR_BIT == JS::shadow::String::LINEAR_BIT,
                "shadow::String::LINEAR_BIT must match js::StringFlags");
  static_assert(INLINE_CHARS_BIT == JS::shadow::String::INLINE_CHARS_BIT,
                "shadow::String::INLINE_CHARS_BIT must match "
                "js::StringFlags");
  static_assert(LATIN1_CHARS_BIT == JS::shadow::String::LATIN1_CHARS_BIT,
                "shadow::String::LATIN1_CHARS_BIT must match "
                "js::StringFlags");
  static_assert(TYPE_FLAGS_MASK == JS::shadow::String::TYPE_FLAGS_MASK,
                "shadow::String::TYPE_FLAGS_MASK must match "
                "js::StringFlags");
  static_assert(EXTERNAL_FLAGS == JS::shadow::String::EXTERNAL_FLAGS,
                "shadow::String::EXTERNAL_FLAGS must match "
                "js::StringFlags");

  static bool hasLatin1Chars(uint32_t flags) {
    return flags & LATIN1_CHARS_BIT;
  }
  static bool hasTwoByteChars(uint32_t flags) {
    return !(flags & LATIN1_CHARS_BIT);
  }
  static bool hasIndexValue(uint32_t flags) { return flags & INDEX_VALUE_BIT; }
  static uint32_t indexValue(uint32_t flags) {
    return flags >> INDEX_VALUE_SHIFT;
  }
  static bool hasStringBuffer(uint32_t flags) {
    return flags & HAS_STRING_BUFFER_BIT;
  }
  static bool isDependedOn(uint32_t flags) { return flags & DEPENDED_ON_BIT; }
  static bool isBeingFlattened(uint32_t flags) { return flags & FLATTEN_MASK; }
  static bool isRope(uint32_t flags) { return !(flags & LINEAR_BIT); }
  static bool isLinear(uint32_t flags) { return flags & LINEAR_BIT; }
  static bool isDependent(uint32_t flags) { return flags & DEPENDENT_BIT; }
  static bool isAtomRef(uint32_t flags) {
    return (flags & ATOM_REF_BIT) && !(flags & ATOM_BIT);
  }
  static bool isExtensible(uint32_t flags) {
    return (flags & TYPE_FLAGS_MASK) == EXTENSIBLE_FLAGS;
  }
  static bool isInline(uint32_t flags) { return flags & INLINE_CHARS_BIT; }
  static bool isFatInline(uint32_t flags) {
    return (flags & FAT_INLINE_MASK) == FAT_INLINE_MASK;
  }
  static bool isExternal(uint32_t flags) {
    return (flags & TYPE_FLAGS_MASK) == EXTERNAL_FLAGS;
  }
  static bool isAtom(uint32_t flags) { return flags & ATOM_BIT; }
  static bool isPermanentAtom(uint32_t flags) {
    return (flags & PERMANENT_ATOM_MASK) == PERMANENT_ATOM_MASK;
  }
  static bool inStringToAtomCache(uint32_t flags) {
    return flags & IN_STRING_TO_ATOM_CACHE;
  }
  static bool isIndex(uint32_t flags) { return flags & ATOM_IS_INDEX_BIT; }
  static bool isPinned(uint32_t flags) { return flags & PINNED_ATOM_BIT; }

  static constexpr uint32_t ropeFlags(CharEncoding encoding) {
    return INIT_ROPE_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t dependentStringFlags(CharEncoding encoding) {
    return INIT_DEPENDENT_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t normalAtomFlags(CharEncoding encoding,
                                            bool hasBuffer) {
    return linearStringFlags(encoding, hasBuffer) | StringFlags::ATOM_BIT;
  }

  static constexpr uint32_t thinInlineAtomFlags(CharEncoding encoding) {
    return thinInlineStringFlags(encoding) | StringFlags::ATOM_BIT;
  }

  static constexpr uint32_t fatInlineAtomFlags(CharEncoding encoding) {
    return fatInlineStringFlags(encoding) | StringFlags::ATOM_BIT;
  }

  static constexpr uint32_t atomRefFlags(CharEncoding encoding) {
    return StringFlags::INIT_ATOM_REF_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t linearStringFlags(CharEncoding encoding,
                                              bool hasBuffer) {
    return INIT_LINEAR_FLAGS | charEncodingFlags(encoding) |
           hasBufferFlags(hasBuffer);
  }

  static constexpr uint32_t extensibleStringFlags(CharEncoding encoding,
                                                  bool hasBuffer) {
    return EXTENSIBLE_FLAGS | charEncodingFlags(encoding) |
           hasBufferFlags(hasBuffer);
  }

  static constexpr uint32_t thinInlineStringFlags(CharEncoding encoding) {
    return INIT_THIN_INLINE_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t fatInlineStringFlags(CharEncoding encoding) {
    return INIT_FAT_INLINE_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t externalStringFlags(CharEncoding encoding) {
    return EXTERNAL_FLAGS | charEncodingFlags(encoding);
  }

  static constexpr uint32_t charEncodingFlags(CharEncoding encoding) {
    return encoding == CharEncoding::Latin1 ? LATIN1_CHARS_BIT : 0;
  }

  static constexpr uint32_t hasBufferFlags(bool hasBuffer) {
    return hasBuffer ? HAS_STRING_BUFFER_BIT : 0;
  }
};

}  // namespace js

#endif  // vm_StringFlags_h
