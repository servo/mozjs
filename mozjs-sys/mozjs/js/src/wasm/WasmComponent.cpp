/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "wasm/WasmComponent.h"

#ifdef ENABLE_WASM_COMPONENTS

#  include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#  include "threading/ExclusiveData.h"
#  include "util/Text.h"
#  include "vm/GlobalObject.h"
#  include "vm/MutexIDs.h"
#  include "wasm/WasmJS.h"

using namespace js;
using namespace js::wasm;

static constexpr mozilla::Span<const char> attributeConstructor =
    mozilla::MakeStringSpan("[constructor]");
static constexpr mozilla::Span<const char> attributeMethod =
    mozilla::MakeStringSpan("[method]");
static constexpr mozilla::Span<const char> attributeStatic =
    mozilla::MakeStringSpan("[static]");

// Component model names are encoded as UTF-8, and in fact an ASCII subset of
// UTF-8, so this is fine.
static char LowercaseNameChar(char c) {
  return ('A' <= c && c <= 'Z') ? c + ('a' - 'A') : c;
}

static mozilla::Span<const char> TrimAttribute(mozilla::Span<const char> name) {
  if (CharsStartsWith(name, attributeConstructor)) {
    return name.Subspan(attributeConstructor.Length());
  }
  if (CharsStartsWith(name, attributeMethod)) {
    return name.Subspan(attributeMethod.Length());
  }
  if (CharsStartsWith(name, attributeStatic)) {
    return name.Subspan(attributeStatic.Length());
  }
  return name;
}

static bool NameHasAttribute(mozilla::Span<const char> name) {
  // The name should already be well-formed from parse time.
  return name.Length() == 0 || name.data()[0] == '[';
}

// We hash only the base part of the name, e.g. "foo" for "[constructor]foo".
HashNumber StronglyUniqueNameHasher::hash(const Lookup& aLookup) {
  mozilla::Span<const char> trimmed = TrimAttribute(aLookup);

  HashNumber hash = 0;
  for (size_t i = 0; i < trimmed.Length(); i++) {
    char c = trimmed.data()[i];
    if (c == '.') {
      break;
    }
    hash = mozilla::AddToHash(hash, LowercaseNameChar(trimmed.data()[i]));
  }
  return hash;
}

bool StronglyUniqueNameHasher::match(const Key& aKey, const Lookup& aLookup) {
  mozilla::Span<const char> keyBytes = aKey.utf8Bytes();
  mozilla::Span<const char> newTrimmed = TrimAttribute(aLookup);
  mozilla::Span<const char> existingTrimmed = TrimAttribute(keyBytes);

  // Rule 1: If one name is l and the other name is [constructor]l (for the
  // same label l), they are strongly-unique.
  bool newIsConstructor = CharsStartsWith(aLookup, attributeConstructor);
  bool existingIsConstructor = CharsStartsWith(keyBytes, attributeConstructor);
  if (newIsConstructor != existingIsConstructor &&
      newTrimmed == existingTrimmed) {
    return false;
  }

  // Rule 2: If one name is l and the other name is [*]l.l (for the same label l
  // and any annotation * with a dotted l.l name), they are not strongly-unique.
  mozilla::Maybe<mozilla::Span<const char>> plain;
  mozilla::Maybe<mozilla::Span<const char>> dotted;
  if (!NameHasAttribute(aLookup)) {
    plain.emplace(aLookup);
  } else if (!NameHasAttribute(keyBytes)) {
    plain.emplace(keyBytes);
  }
  if (CharsStartsWith(aLookup, attributeMethod) ||
      CharsStartsWith(aLookup, attributeStatic)) {
    dotted.emplace(aLookup);
  } else if (CharsStartsWith(keyBytes, attributeMethod) ||
             CharsStartsWith(keyBytes, attributeStatic)) {
    dotted.emplace(keyBytes);
  }
  if (plain.isSome() && dotted.isSome()) {
    mozilla::Span<const char> dottedTrimmed = TrimAttribute(dotted.value());
    size_t indexOfDot = dottedTrimmed.IndexOf('.');
    MOZ_RELEASE_ASSERT(indexOfDot != mozilla::Span<const char>::npos);
    auto [before, after] = dottedTrimmed.SplitAt(indexOfDot);
    after = after.Subspan(1);  // The SplitAt method includes the dot.
    if (plain.value() == after && plain.value() == before) {
      return true;
    }
  }

  // Rule 3: Lowercase the names, trim attributes, and compare directly.
  if (newTrimmed.Length() != existingTrimmed.Length()) {
    return false;
  }
  for (size_t i = 0; i < newTrimmed.Length(); i++) {
    if (LowercaseNameChar(newTrimmed[i]) !=
        LowercaseNameChar(existingTrimmed[i])) {
      return false;
    }
  }
  return true;
}

bool StronglyUniqueNameSet::add(mozilla::Span<const char> name,
                                bool* duplicate) {
  *duplicate = false;

  auto p = data_.lookupForAdd(name);
  if (p) {
    *duplicate = true;
    return true;
  }

  CacheableName owned;
  if (!CacheableName::fromUTF8Bytes(name, &owned)) {
    return false;
  }
  return data_.add(p, std::move(owned));
}

bool ComponentExternDesc::matches(const ComponentExternDesc& sub,
                                  const ComponentExternDesc& super) {
  MOZ_ASSERT(ComponentSortValidForExternDesc(sub.sort()));
  MOZ_ASSERT(ComponentSortValidForExternDesc(super.sort()));
  MOZ_RELEASE_ASSERT(sub.isValid() && super.isValid());

  // Different sorts never match.
  if (sub.sort() != super.sort()) {
    return false;
  }

  switch (sub.sort()) {
    case ComponentSort::Func:
      return sub.asFunc() == super.asFunc();
    case ComponentSort::Type:
      return sub.asType() == super.asType();
    case ComponentSort::Component:
    case ComponentSort::Instance:
    case ComponentSort::CoreModule: {
      // TODO(wasm-cm)
      return false;
    } break;
    default:
      MOZ_CRASH("all valid sorts for externdesc should have been handled");
  }
}

bool ComponentType::record(ComponentRecordFieldVector&& fields,
                           ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(fields)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Record, def);
  return true;
}

bool ComponentType::variant(ComponentVariantCaseVector&& cases,
                            ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(cases)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Variant, def);
  return true;
}

bool ComponentType::list(ComponentType&& elemType, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(elemType)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::List, def);
  return true;
}

bool ComponentType::tuple(ComponentTypeVector&& items, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(items)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Tuple, def);
  return true;
}

bool ComponentType::flags(CacheableNameVector&& labels, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(labels)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Flags, def);
  return true;
}

bool ComponentType::enum_(CacheableNameVector&& cases, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(cases)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Enum, def);
  return true;
}

bool ComponentType::option(ComponentType&& inner, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Option, def);
  return true;
}

bool ComponentType::result(ComponentResultType&& inner, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Result, def);
  return true;
}

bool ComponentType::own(ComponentType&& inner, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Own, def);
  return true;
}

bool ComponentType::borrow(ComponentType&& inner, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Borrow, def);
  return true;
}

bool ComponentType::func(ComponentFuncType&& inner, ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Func, def);
  return true;
}

bool ComponentType::resource(ComponentResourceType&& inner,
                             ComponentType* type) {
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(inner)));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::Resource, def);
  return true;
}

bool ComponentType::subResource(ComponentType* type) {
  // We still need a unique heap allocation so that two (sub resource) types
  // will not be equal.
  ComponentTypeDef* def =
      js_new<ComponentTypeDef>(ComponentTypeSchema(mozilla::Nothing()));
  if (!def) {
    return false;
  }
  *type = ComponentType(ComponentTypeKind::SubResource, def);
  return true;
}

const ComponentRecordFieldVector& ComponentType::asRecord() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Record);
  return typeDef_->schema().as<ComponentRecordFieldVector>();
}

const ComponentVariantCaseVector& ComponentType::asVariant() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Variant);
  return typeDef_->schema().as<ComponentVariantCaseVector>();
}

ComponentType ComponentType::asList() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::List);
  return typeDef_->schema().as<ComponentType>();
}

const ComponentTypeVector& ComponentType::asTuple() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Tuple);
  return typeDef_->schema().as<ComponentTypeVector>();
}

const CacheableNameVector& ComponentType::asFlags() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Flags);
  return typeDef_->schema().as<CacheableNameVector>();
}

const CacheableNameVector& ComponentType::asEnum() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Enum);
  return typeDef_->schema().as<CacheableNameVector>();
}

ComponentType ComponentType::asOption() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Option);
  return typeDef_->schema().as<ComponentType>();
}

ComponentResultType ComponentType::asResult() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Result);
  return typeDef_->schema().as<ComponentResultType>();
}

ComponentType ComponentType::asOwn() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Own);
  return typeDef_->schema().as<ComponentType>();
}

ComponentType ComponentType::asBorrow() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Borrow);
  return typeDef_->schema().as<ComponentType>();
}

const ComponentFuncType& ComponentType::asFunc() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Func);
  return typeDef_->schema().as<ComponentFuncType>();
}

const ComponentResourceType& ComponentType::asResource() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Resource);
  return typeDef_->schema().as<ComponentResourceType>();
}

bool ComponentType::structurallyEqual(const ComponentType& a,
                                      const ComponentType& b) {
  return a.kind() == b.kind() &&
         ComponentTypeDef::structurallyEqual(*a.typeDef_, *b.typeDef_);
}

bool ComponentTypeDef::structurallyEqual(const ComponentTypeDef& a,
                                         const ComponentTypeDef& b) {
  return a.schema().match(
      [&](const mozilla::Nothing&) {
        return b.schema().is<mozilla::Nothing>();
      },
      [&](const ComponentType& aType) {
        if (!b.schema().is<ComponentType>()) {
          return false;
        }
        const ComponentType& bType = b.schema().as<ComponentType>();
        return aType == bType;
      },
      [&](const ComponentRecordFieldVector& aFields) {
        if (!b.schema().is<ComponentRecordFieldVector>()) {
          return false;
        }
        const ComponentRecordFieldVector& bFields =
            b.schema().as<ComponentRecordFieldVector>();

        if (aFields.length() != bFields.length()) {
          return false;
        }
        for (size_t i = 0; i < aFields.length(); i++) {
          if (aFields[i] != bFields[i]) {
            return false;
          }
        }
        return true;
      },
      [&](const ComponentVariantCaseVector& aCases) {
        if (!b.schema().is<ComponentVariantCaseVector>()) {
          return false;
        }
        const ComponentVariantCaseVector& bCases =
            b.schema().as<ComponentVariantCaseVector>();

        if (aCases.length() != bCases.length()) {
          return false;
        }
        for (size_t i = 0; i < aCases.length(); i++) {
          if (aCases[i] != bCases[i]) {
            return false;
          }
        }
        return true;
      },
      [&](const ComponentTypeVector& aTypes) {
        if (!b.schema().is<ComponentTypeVector>()) {
          return false;
        }
        const ComponentTypeVector& bTypes =
            b.schema().as<ComponentTypeVector>();

        if (aTypes.length() != bTypes.length()) {
          return false;
        }
        for (size_t i = 0; i < aTypes.length(); i++) {
          if (aTypes[i] != bTypes[i]) {
            return false;
          }
        }
        return true;
      },
      [&](const CacheableNameVector& aLabels) {
        if (!b.schema().is<CacheableNameVector>()) {
          return false;
        }
        const CacheableNameVector& bLabels =
            b.schema().as<CacheableNameVector>();

        if (aLabels.length() != bLabels.length()) {
          return false;
        }
        for (size_t i = 0; i < aLabels.length(); i++) {
          if (aLabels[i] != bLabels[i]) {
            return false;
          }
        }
        return true;
      },
      [&](const ComponentResultType& aResult) {
        if (!b.schema().is<ComponentResultType>()) {
          return false;
        }
        const ComponentResultType& bResult =
            b.schema().as<ComponentResultType>();
        return ComponentResultType::equals(aResult, bResult);
      },
      [&](const ComponentFuncType& aFunc) {
        if (!b.schema().is<ComponentFuncType>()) {
          return false;
        }
        const ComponentFuncType& bFunc = b.schema().as<ComponentFuncType>();
        return aFunc == bFunc;
      },
      [&](const ComponentResourceType& a) {
        // This method never considers resource types to be equal because this
        // is the wrong place to check for that kind of equality. Two
        // canonicalized ComponentTypes for resource types may be equal, because
        // they point at the same ComponentTypeDef (by pointer equality), but
        // this method has no concept of that.
        return false;
      });
}

[[nodiscard]] static HashNumber AddComponentTypeToHash(HashNumber hash,
                                                       ComponentType type) {
  hash = mozilla::AddToHash(hash, type.kind());
  hash = mozilla::AddToHash(hash, type.typeDef().get());
  return hash;
}

[[nodiscard]] static HashNumber AddMaybeComponentTypeToHash(
    HashNumber hash, mozilla::Maybe<ComponentType> type) {
  hash = mozilla::AddToHash(hash, type.isSome());
  if (type.isSome()) {
    hash = AddComponentTypeToHash(hash, *type);
  }
  return hash;
}

static HashNumber HashName(const CacheableName& name) {
  return mozilla::HashString(name.utf8Bytes().data(),
                             name.utf8Bytes().Length());
}

HashNumber ComponentTypeHasher::hash(const ComponentType& t) {
  HashNumber hash = 0;
  hash = mozilla::AddToHash(hash, t.kind());

  // Primitives and resource types should not appear here; this is caught by the
  // default case.
  switch (t.kind()) {
    case ComponentTypeKind::Record: {
      const ComponentRecordFieldVector& fields = t.asRecord();
      for (const ComponentRecordField& f : fields) {
        hash = mozilla::AddToHash(hash, HashName(f.name));
        hash = AddComponentTypeToHash(hash, f.type);
      }
    } break;
    case ComponentTypeKind::Variant: {
      const ComponentVariantCaseVector& cases = t.asVariant();
      for (const ComponentVariantCase& c : cases) {
        hash = mozilla::AddToHash(hash, HashName(c.name));
        hash = AddMaybeComponentTypeToHash(hash, c.type);
      }
    } break;
    case ComponentTypeKind::List: {
      hash = AddComponentTypeToHash(hash, t.asList());
    } break;
    case ComponentTypeKind::Tuple: {
      const ComponentTypeVector& types = t.asTuple();
      for (const ComponentType& t : types) {
        hash = AddComponentTypeToHash(hash, t);
      }
    } break;
    case ComponentTypeKind::Flags: {
      const CacheableNameVector& labels = t.asFlags();
      for (const CacheableName& label : labels) {
        hash = mozilla::AddToHash(hash, HashName(label));
      }
    } break;
    case ComponentTypeKind::Enum: {
      const CacheableNameVector& cases = t.asEnum();
      for (const CacheableName& c : cases) {
        hash = mozilla::AddToHash(hash, HashName(c));
      }
    } break;
    case ComponentTypeKind::Option: {
      hash = AddComponentTypeToHash(hash, t.asOption());
    } break;
    case ComponentTypeKind::Result: {
      const ComponentResultType& rt = t.asResult();
      hash = AddMaybeComponentTypeToHash(hash, rt.type);
      hash = AddMaybeComponentTypeToHash(hash, rt.errorType);
    } break;
    case ComponentTypeKind::Own: {
      hash = AddComponentTypeToHash(hash, t.asOwn());
    } break;
    case ComponentTypeKind::Borrow: {
      hash = AddComponentTypeToHash(hash, t.asBorrow());
    } break;
    case ComponentTypeKind::Func: {
      const ComponentFuncType& ft = t.asFunc();
      MOZ_ASSERT(ft.paramTypes.length() == ft.paramNames.length());
      for (size_t i = 0; i < ft.paramTypes.length(); i++) {
        hash = mozilla::AddToHash(hash, HashName(ft.paramNames[i]));
        hash = AddComponentTypeToHash(hash, ft.paramTypes[i]);
      }
      hash = AddMaybeComponentTypeToHash(hash, ft.resultType);
    } break;
    case ComponentTypeKind::Component:
    case ComponentTypeKind::Instance:
      // TODO(wasm-cm): Component and instance types not yet implemented
      MOZ_CRASH();
    default:
      MOZ_CRASH("should have been excluded from hashing");
  }

  return hash;
}
bool ComponentTypeHasher::match(const ComponentType& a,
                                const ComponentType& b) {
  // (eq i) bounds should be resolved to a unique type on type construction.
  MOZ_ASSERT(a.kind() != ComponentTypeKind::Eq);
  MOZ_ASSERT(b.kind() != ComponentTypeKind::Eq);

  // Primitives and resource types should be special-cased during
  // canonicalization and should therefore never end up here.
  MOZ_ASSERT(!ComponentTypeKindIsPrimitive(a.kind()) &&
             a.kind() != ComponentTypeKind::Resource &&
             a.kind() != ComponentTypeKind::SubResource);
  MOZ_ASSERT(!ComponentTypeKindIsPrimitive(b.kind()) &&
             b.kind() != ComponentTypeKind::Resource &&
             b.kind() != ComponentTypeKind::SubResource);

  return ComponentType::structurallyEqual(a, b);
}

bool ComponentCanonicalTypeSet::canonicalize(const ComponentType& type,
                                             ComponentType* canonicalized) {
  MOZ_RELEASE_ASSERT(type.isValid());

  // Primitives compare trivially and require no additional storage, therefore
  // they do not need to be explicitly stored.
  if (ComponentTypeKindIsPrimitive(type.kind())) {
    MOZ_RELEASE_ASSERT(!type.typeDef());
    *canonicalized = type;
    return true;
  }
  MOZ_RELEASE_ASSERT(type.typeDef());

  // Resource types retain their uniqueness by skipping canonicalization.
  if (type.kind() == ComponentTypeKind::Resource ||
      type.kind() == ComponentTypeKind::SubResource) {
    *canonicalized = type;
    return true;
  }

  // All other types are hashed and deduplicated structurally. As long as all
  // types are canonicalized as they are parsed, this means that pointer
  // equality of a type's ComponentTypeDef is equivalent to structural equality.
  auto addPtr = canonicalTypes_.lookupForAdd(type);
  if (addPtr) {
    *canonicalized = *addPtr;
    return true;
  }
  if (!canonicalTypes_.add(addPtr, type)) {
    return false;
  }
  *canonicalized = type;
  return true;
}

MOZ_RUNINIT static ExclusiveData<ComponentCanonicalTypeSet>
    sComponentCanonicalTypeSet(mutexid::WasmComponentCanonicalTypeSet);

bool wasm::CanonicalizeComponentType(const ComponentType& type,
                                     ComponentType* canonicalized) {
  ExclusiveData<ComponentCanonicalTypeSet>::Guard locked =
      sComponentCanonicalTypeSet.lock();
  return locked->canonicalize(type, canonicalized);
}

void wasm::PurgeComponentCanonicalTypes() {
  ExclusiveData<ComponentCanonicalTypeSet>::Guard locked =
      sComponentCanonicalTypeSet.lock();
  locked->canonicalTypes_.clearAndCompact();
}

mozilla::Maybe<FuncType> wasm::FlattenFuncType(
    const Component& c, const ComponentFuncType& funcType) {
  ValTypeVector params;
  ValTypeVector results;

  // TODO(wasm-cm): Handle (and test) the case where params or results exceed
  // the maximums set by the component model, at which point the ABI falls back
  // to passing values in memory. (Or maybe this will all change with lazy
  // lowering, who knows.)

  if (!FlattenTypes(c, funcType.paramTypes, &params)) {
    return mozilla::Nothing();
  }
  if (funcType.resultType.isSome()) {
    if (!FlattenType(c, funcType.resultType.ref(), &results)) {
      return mozilla::Nothing();
    }
  }

  return mozilla::Some(FuncType(std::move(params), std::move(results)));
}

bool wasm::FlattenTypes(const Component& c, const ComponentTypeVector& types,
                        ValTypeVector* result) {
  // Pre-reserve at least enough space for a bunch of primitives. We still may
  // exceed the capacity reserved here but at least we can avoid a little bit of
  // allocation. (Appends after this point are not to be considered infallible.)
  if (!result->reserve(types.length())) {
    return false;
  }

  for (const ComponentType& t : types) {
    if (!FlattenType(c, t, result)) {
      return false;
    }
  }

  return true;
}

static ValType JoinVariantValType(ValType a, ValType b) {
  MOZ_ASSERT(a.isNumber() && b.isNumber());
  if (a == b) {
    return a;
  } else if ((a == ValType::i32() && b == ValType::f32()) ||
             (a == ValType::f32() && b == ValType::i32())) {
    return ValType::i32();
  } else {
    return ValType::i64();
  }
}

bool wasm::FlattenType(const Component& c, const ComponentType& type,
                       ValTypeVector* result) {
  switch (type.kind()) {
    // Simple primitives
    case ComponentTypeKind::Bool:
    case ComponentTypeKind::U8:
    case ComponentTypeKind::U16:
    case ComponentTypeKind::U32:
    case ComponentTypeKind::S8:
    case ComponentTypeKind::S16:
    case ComponentTypeKind::S32:
    case ComponentTypeKind::Char:
    case ComponentTypeKind::Flags:
    case ComponentTypeKind::Enum:
    case ComponentTypeKind::Own:
    case ComponentTypeKind::Borrow: {
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::U64:
    case ComponentTypeKind::S64: {
      if (!result->append(ValType::i64())) {
        return false;
      }
    } break;
    case ComponentTypeKind::F32: {
      if (!result->append(ValType::f32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::F64: {
      if (!result->append(ValType::f64())) {
        return false;
      }
    } break;

    // Strings are always two i32's
    case ComponentTypeKind::String: {
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;

    // Compound types have dedicated logic. Note that our data storage for some
    // types disagrees with the categories in the canonical ABI explainer, e.g.
    // we represent tuples as a vector of value types, not a record.
    case ComponentTypeKind::List: {
      // This will have to change when support is added for fixed-length lists.
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::Record: {
      if (!FlattenRecord(c, type.asRecord(), result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Tuple: {
      if (!FlattenTypes(c, type.asTuple(), result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Variant: {
      // Flatten the discriminant
      if (!result->append(ValType::i32())) {
        return false;
      }

      // Flatten all the cases (overlapped, with joins)
      const ComponentVariantCaseVector& cases = type.asVariant();
      size_t startIndex = result->length();
      for (const ComponentVariantCase& case_ : cases) {
        if (!case_.type) {
          continue;
        }

        ValTypeVector caseFlattened;
        if (!FlattenType(c, *case_.type, &caseFlattened)) {
          return false;
        }
        for (size_t i = 0; i < caseFlattened.length(); i++) {
          size_t existingIndex = startIndex + i;
          if (existingIndex < result->length()) {
            // Join the new type with the existing one.
            (*result)[existingIndex] =
                JoinVariantValType((*result)[existingIndex], caseFlattened[i]);
          } else {
            // Append the new type to the overall list.
            if (!result->append(caseFlattened[i])) {
              return false;
            }
          }
        }
      }
    } break;
    case ComponentTypeKind::Option: {
      ComponentType inner = type.asOption();
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!FlattenType(c, inner, result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Result: {
      ComponentResultType inner = type.asResult();
      // Result types are encoded just like a variant with two cases, but each
      // case may or may not have a type.

      // Discriminant
      if (!result->append(ValType::i32())) {
        return false;
      }

      // Payload(s)
      size_t startIndex = result->length();
      if (inner.type.isSome()) {
        if (!FlattenType(c, *inner.type, result)) {
          return false;
        }
      }
      if (inner.errorType.isSome()) {
        ValTypeVector errorFlattened;
        if (!FlattenType(c, *inner.errorType, &errorFlattened)) {
          return false;
        }
        for (size_t i = 0; i < errorFlattened.length(); i++) {
          size_t existingIndex = startIndex + i;
          if (existingIndex < result->length()) {
            (*result)[existingIndex] =
                JoinVariantValType((*result)[existingIndex], errorFlattened[i]);
          } else {
            if (!result->append(errorFlattened[i])) {
              return false;
            }
          }
        }
      }
    } break;

    default:
      MOZ_CRASH("should have been rejected when the func type was validated");
  }

  return true;
}

bool wasm::FlattenRecord(const Component& c,
                         const ComponentRecordFieldVector& fields,
                         ValTypeVector* result) {
  for (const ComponentRecordField& field : fields) {
    if (!FlattenType(c, field.type, result)) {
      return false;
    }
  }

  return true;
}

/* virtual */
JSObject* Component::createObject(JSContext* cx) const {
  if (!GlobalObject::ensureConstructor(cx, cx->global(), JSProto_WebAssembly)) {
    return nullptr;
  }

  JS::RootedVector<JSString*> parameterStrings(cx);
  JS::RootedVector<Value> parameterArgs(cx);
  bool canCompileStrings = false;
  if (!cx->isRuntimeCodeGenEnabled(JS::RuntimeCode::WASM, nullptr,
                                   JS::CompilationType::Undefined,
                                   parameterStrings, nullptr, parameterArgs,
                                   NullHandleValue, &canCompileStrings)) {
    return nullptr;
  }
  if (!canCompileStrings) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_CSP_BLOCKED_WASM, "WebAssembly.Component");
    return nullptr;
  }

  RootedObject proto(cx, &cx->global()->getPrototype(JSProto_WasmComponent));
  return WasmComponentObject::create(cx, *this, proto);
}

bool Component::addImport(ComponentImport&& import) {
  ComponentSort sort = import.externDesc().sort();
  MOZ_ASSERT(ComponentSortValidForExternDesc(sort));

  // Add import to imports vector
  uint32_t importIndex = imports_.length();
  if (!imports_.append(std::move(import))) {
    return false;
  }

  // Add import to appropriate index space
  ComponentItem item = ComponentItem::import(importIndex);
  switch (sort) {
    case ComponentSort::Func: {
      if (!funcs_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Type: {
      if (!types_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Component: {
      if (!components_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Instance: {
      if (!instances_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::CoreModule: {
      if (!coreModules_.append(item)) {
        return false;
      }
    } break;
    default:
      MOZ_CRASH();
  }

  return true;
}

bool Component::addExport(ComponentExport&& exp) {
  ComponentSort sort = exp.externDesc().sort();
  MOZ_ASSERT(ComponentSortValidForExternDesc(sort));

  // Add export to exports vector
  uint32_t exportIndex = exports_.length();
  if (!exports_.append(std::move(exp))) {
    return false;
  }

  // Add export to appropriate index space
  ComponentItem item = ComponentItem::export_(exportIndex);
  switch (sort) {
    case ComponentSort::Func: {
      if (!funcs_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Type: {
      if (!types_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Component: {
      if (!components_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::Instance: {
      if (!instances_.append(item)) {
        return false;
      }
    } break;
    case ComponentSort::CoreModule: {
      if (!coreModules_.append(item)) {
        return false;
      }
    } break;
    default:
      MOZ_CRASH();
  }

  return true;
}

#endif  // ENABLE_WASM_COMPONENTS
