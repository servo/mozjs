/*
 * Copyright 2015 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmModuleTypes.h"

#include <bit>

#include "vm/JSAtomUtils.h"  // AtomizeUTF8Chars
#include "vm/MallocProvider.h"
#include "wasm/WasmUtility.h"

#include "vm/JSAtomUtils-inl.h"  // AtomToId

using namespace js;
using namespace js::wasm;

using mozilla::CheckedInt32;
using mozilla::MallocSizeOf;

//=========================================================================
// TagLayout

static CheckedInt32 RoundUpToAlignment(CheckedInt32 address, uint32_t align) {
  MOZ_ASSERT(std::has_single_bit(align));

  // Note: Be careful to order operators such that we first make the
  // value smaller and then larger, so that we don't get false
  // overflow errors due to (e.g.) adding `align` and then
  // subtracting `1` afterwards when merely adding `align-1` would
  // not have overflowed. Note that due to the nature of two's
  // complement representation, if `address` is already aligned,
  // then adding `align-1` cannot itself cause an overflow.

  return ((address + (align - 1)) / align) * align;
}

class TagLayout {
  mozilla::CheckedInt32 sizeSoFar = 0;
  uint32_t tagAlignment = 1;

 public:
  // The field adders return the offset of the the field.
  mozilla::CheckedInt32 addField(StorageType type) {
    uint32_t fieldSize = type.size();
    uint32_t fieldAlignment = type.alignmentInStruct();

    MOZ_ASSERT(fieldSize >= 1 && fieldSize <= 16);
    MOZ_ASSERT((fieldSize & (fieldSize - 1)) == 0);  // is a power of 2
    MOZ_ASSERT(fieldAlignment == fieldSize);         // is naturally aligned

    // Alignment of the tag is the max of the alignment of its fields.
    tagAlignment = std::max(tagAlignment, fieldAlignment);

    // Align the pointer.
    CheckedInt32 offset = RoundUpToAlignment(sizeSoFar, fieldAlignment);
    if (!offset.isValid()) {
      return offset;
    }

    // Allocate space.
    sizeSoFar = offset + fieldSize;
    if (!sizeSoFar.isValid()) {
      return sizeSoFar;
    }

    return offset;
  }

  // The close method rounds up the structure size to the appropriate
  // alignment and returns that size.
  mozilla::CheckedInt32 close() {
    CheckedInt32 size = RoundUpToAlignment(sizeSoFar, tagAlignment);
    // Make the overall size be an integral number of machine words.
    if (tagAlignment < sizeof(uintptr_t)) {
      size = RoundUpToAlignment(size, sizeof(uintptr_t));
    }
    return size;
  }
};

//=========================================================================

/* static */
CacheableName CacheableName::fromUTF8Chars(UniqueChars&& utf8Chars) {
  size_t length = strlen(utf8Chars.get());
  UTF8Bytes bytes;
  bytes.replaceRawBuffer(utf8Chars.release(), length, length + 1);
  return CacheableName(std::move(bytes));
}

/* static */
bool CacheableName::fromUTF8Chars(const char* utf8Chars, CacheableName* name) {
  size_t utf8CharsLen = strlen(utf8Chars);
  UTF8Bytes bytes;
  if (!bytes.resizeUninitialized(utf8CharsLen)) {
    return false;
  }
  memcpy(bytes.begin(), utf8Chars, utf8CharsLen);
  *name = CacheableName(std::move(bytes));
  return true;
}

/* static */
bool CacheableName::fromUTF8Bytes(mozilla::Span<const char> utf8Bytes,
                                  CacheableName* name) {
  UTF8Bytes bytes;
  if (!bytes.append(utf8Bytes.data(), utf8Bytes.Length())) {
    return false;
  }
  *name = CacheableName(std::move(bytes));
  return true;
}

MOZ_RUNINIT BranchHintVector BranchHintCollection::invalidVector_;

JSString* CacheableName::toJSString(JSContext* cx) const {
  return NewStringCopyUTF8N(cx, JS::UTF8Chars(begin(), length()));
}

JSAtom* CacheableName::toAtom(JSContext* cx) const {
  return AtomizeUTF8Chars(cx, begin(), length());
}

bool CacheableName::toPropertyKey(JSContext* cx,
                                  MutableHandleId propertyKey) const {
  JSAtom* atom = toAtom(cx);
  if (!atom) {
    return false;
  }
  propertyKey.set(AtomToId(atom));
  return true;
}

UniqueChars CacheableName::toQuotedString(JSContext* cx) const {
  RootedString atom(cx, toAtom(cx));
  if (!atom) {
    return nullptr;
  }
  return QuoteString(cx, atom.get());
}

size_t CacheableName::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return bytes_.sizeOfExcludingThis(mallocSizeOf);
}

size_t Import::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return module.sizeOfExcludingThis(mallocSizeOf) +
         field.sizeOfExcludingThis(mallocSizeOf);
}

Export::Export(CacheableName&& fieldName, uint32_t index, DefinitionKind kind)
    : fieldName_(std::move(fieldName)) {
  pod.kind_ = kind;
  pod.index_ = index;
}

uint32_t Export::funcIndex() const {
  MOZ_ASSERT(pod.kind_ == DefinitionKind::Function);
  return pod.index_;
}

uint32_t Export::memoryIndex() const {
  MOZ_ASSERT(pod.kind_ == DefinitionKind::Memory);
  return pod.index_;
}

uint32_t Export::globalIndex() const {
  MOZ_ASSERT(pod.kind_ == DefinitionKind::Global);
  return pod.index_;
}

uint32_t Export::tagIndex() const {
  MOZ_ASSERT(pod.kind_ == DefinitionKind::Tag);
  return pod.index_;
}

uint32_t Export::tableIndex() const {
  MOZ_ASSERT(pod.kind_ == DefinitionKind::Table);
  return pod.index_;
}

size_t Export::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return fieldName_.sizeOfExcludingThis(mallocSizeOf);
}

size_t GlobalDesc::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return initial_.sizeOfExcludingThis(mallocSizeOf);
}

bool TagType::initialize(const SharedTypeDef& funcType) {
  MOZ_ASSERT(funcType->isFuncType());
  type_ = funcType;

  const ValTypeVector& args = argTypes();
  // Compute the byte offsets for arguments when we layout an exception.
  if (!exceptionArgOffsets_.resize(args.length())) {
    return false;
  }

  TagLayout layout;
  for (size_t i = 0; i < args.length(); i++) {
    CheckedInt32 offset = layout.addField(StorageType(args[i].packed()));
    if (!offset.isValid()) {
      return false;
    }
    exceptionArgOffsets_[i] = offset.value();
  }

  // Find the total size of all the arguments.
  CheckedInt32 size = layout.close();
  if (!size.isValid()) {
    return false;
  }
  this->size_ = size.value();

  return true;
}

size_t TagType::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return exceptionArgOffsets_.sizeOfExcludingThis(mallocSizeOf);
}

size_t TagDesc::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return type->sizeOfExcludingThis(mallocSizeOf);
}

size_t ModuleElemSegment::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return SizeOfMaybeExcludingThis(offsetIfActive, mallocSizeOf) +
         elemIndices.sizeOfExcludingThis(mallocSizeOf);
}

size_t ModuleElemSegment::Expressions::sizeOfExcludingThis(
    MallocSizeOf mallocSizeOf) const {
  return exprBytes.sizeOfExcludingThis(mallocSizeOf);
}

size_t DataSegment::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return SizeOfMaybeExcludingThis(offsetIfActive, mallocSizeOf) +
         bytes.sizeOfExcludingThis(mallocSizeOf);
}

size_t CustomSection::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return name.sizeOfExcludingThis(mallocSizeOf) + sizeof(*payload) +
         payload->sizeOfExcludingThis(mallocSizeOf);
}

size_t NameSection::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const {
  return funcNames.sizeOfExcludingThis(mallocSizeOf);
}

const char* wasm::ToString(LimitsKind kind) {
  switch (kind) {
    case LimitsKind::Memory:
      return "Memory";
    case LimitsKind::Table:
      return "Table";
    default:
      MOZ_CRASH();
  }
}
