/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef wasm_component_h
#define wasm_component_h

#ifdef ENABLE_WASM_COMPONENTS

#  include "js/WasmComponent.h"

#  include "mozilla/HashTable.h"
#  include "mozilla/Maybe.h"
#  include "mozilla/RefPtr.h"
#  include "mozilla/Span.h"
#  include "mozilla/Variant.h"
#  include "mozilla/Vector.h"
#  include "wasm/WasmModule.h"

namespace js {
namespace wasm {

// A helper macro allowing component names to be printed with `%.*s`. Component
// names are always ASCII, so this is safe.
#  define ComponentName_Printf(n) \
    (int)(n).utf8Bytes().Length(), (n).utf8Bytes().data()

// A "sort", or "kind", of item in the component model, used for all cases where
// we must refer to a different item.
//
// This type is also used for the `externdesc` type, which describes what
// components (not core modules) can import and export, and whose cases are a
// subset of `sort`. Sorts that are valid for `externdesc` have the highest bit
// set. Additionally, sorts that can be exported by core modules (core:sort)
// have the second-highest bit set, and correspond to wasm::DefinitionKind.
enum class ComponentSort : uint8_t {
  Invalid = 0,

  Func = 0x80 | 0x01,
  Type = 0x80 | 0x03,
  Component = 0x80 | 0x04,
  Instance = 0x80 | 0x05,

  CoreFunction = 0x40 | int(DefinitionKind::Function),
  CoreTable = 0x40 | int(DefinitionKind::Table),
  CoreMemory = 0x40 | int(DefinitionKind::Memory),
  CoreGlobal = 0x40 | int(DefinitionKind::Global),
  CoreTag = 0x40 | int(DefinitionKind::Tag),

  CoreType = 0x10,
  CoreModule = 0x80 | 0x11,
  CoreInstance = 0x12,
};

// Checks if the given sort is valid for a component import or export (the
// component `externdesc` type).
inline bool ComponentSortValidForExternDesc(ComponentSort sort) {
  return (uint8_t(sort) & 0x80) != 0;
}

// Checks if the given sort is for a core item that can be imported or exported,
// i.e. a DefinitionKind imported into the component model. To extract the
// underlying DefinitionKind, use CoreSortFromComponentSort.
inline bool ComponentSortIsCoreSort(ComponentSort sort) {
  return (uint8_t(sort) & 0x40) != 0;
}

// Extracts the underlying DefinitionKind from a ComponentSort (if there is
// one).
inline DefinitionKind CoreSortFromComponentSort(ComponentSort sort) {
  MOZ_ASSERT(ComponentSortIsCoreSort(sort));
  return DefinitionKind(uint8_t(sort) & ~0xc0);
}

// Every kind of type that can be defined in the component model. Not all types
// are valid in all contexts.
enum class ComponentTypeKind : uint8_t {
  Invalid = 0,

  Bool = 0x7f,
  S8 = 0x7e,
  U8 = 0x7d,
  S16 = 0x7c,
  U16 = 0x7b,
  S32 = 0x7a,
  U32 = 0x79,
  S64 = 0x78,
  U64 = 0x77,
  F32 = 0x76,
  F64 = 0x75,
  Char = 0x74,
  String = 0x73,

  Record = 0x72,
  Variant = 0x71,
  List = 0x70,
  Tuple = 0x6f,
  Flags = 0x6e,
  Enum = 0x6d,
  Option = 0x6b,
  Result = 0x6a,
  Own = 0x69,
  Borrow = 0x68,

  Func = 0x40,  // async func types are not a separate kind
  Component = 0x41,
  Instance = 0x42,
  Resource = 0x3f,  // resource types with callbacks are not a separate kind

  // Type bounds
  Eq = 0x20,
  SubResource = 0x21,

  // Convenience for ComponentTypeKindIsPrimitive. "First" and "last" refer to
  // the actual byte value.
  FirstPrimitive = String,
  LastPrimitive = Bool,
};

// Checks if the given kind is for a primitive type (`primvaltype`), i.e. one
// that doesn't need to be defined and referenced.
inline bool ComponentTypeKindIsPrimitive(ComponentTypeKind kind) {
  return ComponentTypeKind::FirstPrimitive <= kind &&
         kind <= ComponentTypeKind::LastPrimitive;
}

// Checks if the given kind is for a value type (`valtype`), i.e. one that can
// be used for function parameters.
inline bool ComponentTypeKindIsValueType(ComponentTypeKind kind) {
  return ComponentTypeKindIsPrimitive(kind) ||
         (ComponentTypeKind::Borrow <= kind &&
          kind <= ComponentTypeKind::Record &&
          int(kind) != 0x6c  // the one weird gap in the binary
         );
}

// Forward declarations to satisfy the methods in ComponentType
class ComponentTypeDef;
class ComponentType;
struct ComponentRecordField;
struct ComponentVariantCase;
struct ComponentResultType;
struct ComponentFuncType;
class ComponentResourceType;
using ComponentTypeVector =
    mozilla::Vector<ComponentType, 0, SystemAllocPolicy>;
using ComponentRecordFieldVector =
    mozilla::Vector<ComponentRecordField, 0, SystemAllocPolicy>;
using ComponentVariantCaseVector =
    mozilla::Vector<ComponentVariantCase, 0, SystemAllocPolicy>;

// The type of an item within a component.
class ComponentType {
  // TODO(wasm-cm): See if we could do a fancy tagging scheme to store the kind
  // in the bits of the pointer. It's a bit funky because right now we use high
  // bits in the kind for various purposes and so we can't pack it down into 3
  // or 4 bits like you'd want.
  ComponentTypeKind kind_;

  RefPtr<ComponentTypeDef> typeDef_;

  explicit ComponentType(ComponentTypeKind kind)
      : kind_(kind), typeDef_(nullptr) {
    MOZ_ASSERT(ComponentTypeKindIsPrimitive(kind));
  }
  explicit ComponentType(ComponentTypeKind kind,
                         RefPtr<ComponentTypeDef> typeDef)
      : kind_(kind), typeDef_(std::move(typeDef)) {}

 public:
  ComponentType() : kind_(ComponentTypeKind::Invalid), typeDef_(nullptr) {}
  bool isValid() const { return kind_ != ComponentTypeKind::Invalid; }

  // "Constructors" for various kinds of types. The resulting types will NOT be
  // canonical until added to the process-wide ComponentCanonicalTypeSet.
  static ComponentType primitive(ComponentTypeKind kind) {
    MOZ_RELEASE_ASSERT(ComponentTypeKindIsPrimitive(kind));
    return ComponentType(kind);
  }
  static bool record(ComponentRecordFieldVector&& fields, ComponentType* type);
  static bool variant(ComponentVariantCaseVector&& cases, ComponentType* type);
  static bool list(ComponentType&& elemType, ComponentType* type);
  static bool tuple(ComponentTypeVector&& items, ComponentType* type);
  static bool flags(CacheableNameVector&& labels, ComponentType* type);
  static bool enum_(CacheableNameVector&& cases, ComponentType* type);
  static bool option(ComponentType&& inner, ComponentType* type);
  static bool result(ComponentResultType&& inner, ComponentType* type);
  static bool own(ComponentType&& inner, ComponentType* type);
  static bool borrow(ComponentType&& inner, ComponentType* type);
  static bool func(ComponentFuncType&& inner, ComponentType* type);
  static bool resource(ComponentResourceType&& inner, ComponentType* type);
  static bool subResource(ComponentType* type);

  ComponentTypeKind kind() const { return kind_; }
  RefPtr<ComponentTypeDef> typeDef() const { return typeDef_; }

  const ComponentRecordFieldVector& asRecord() const;
  const ComponentVariantCaseVector& asVariant() const;
  ComponentType asList() const;
  const ComponentTypeVector& asTuple() const;
  const CacheableNameVector& asFlags() const;
  const CacheableNameVector& asEnum() const;
  ComponentType asOption() const;
  ComponentResultType asResult() const;
  ComponentType asOwn() const;
  ComponentType asBorrow() const;
  const ComponentFuncType& asFunc() const;
  const ComponentResourceType& asResource() const;

  // Cheaply checks if two canonicalized component types are equal under the
  // rules of the component model. This is fully general and handles resource
  // types, but because it compares ComponentTypeDef pointers for equality, only
  // canonicalized types are supported.
  bool operator==(const ComponentType& other) const {
    return kind_ == other.kind_ && typeDef_ == other.typeDef_;
  }
  static bool maybeEquals(mozilla::Maybe<ComponentType> a,
                          mozilla::Maybe<ComponentType> b) {
    if (a.isNothing() && b.isNothing()) {
      return true;
    }
    if (a.isSome() != b.isSome()) {
      return false;
    }
    return *a == *b;
  }

  // Checks if two (non-canonical) component types are structurally equal. This
  // is different from the usual `==` operator, which assumes types have been
  // canonicalized. Resource types will always come back as unequal.
  //
  // In almost all cases, the `==` operator is what you want.
  static bool structurallyEqual(const ComponentType& a, const ComponentType& b);
};

static_assert(std::is_default_constructible_v<ComponentType>);
static_assert(std::is_copy_constructible_v<ComponentType>);

struct ComponentTypeHasher {
  using Key = ComponentType;
  using Lookup = ComponentType;

  static HashNumber hash(const Lookup& aLookup);
  static bool match(const Key& aKey, const Lookup& aLookup);
};

struct ComponentCanonicalTypeSet {
  mozilla::HashSet<ComponentType, ComponentTypeHasher, SystemAllocPolicy>
      canonicalTypes_;

  bool canonicalize(const ComponentType& type, ComponentType* canonicalized);
};

// Canonicalizes `type` against the process-wide canonical type set, returning
// the canonical representative through `*canonicalized`. Thread-safe.
[[nodiscard]] bool CanonicalizeComponentType(const ComponentType& type,
                                             ComponentType* canonicalized);

// Empties the process-wide canonical type set. Intended for shutdown / testing.
void PurgeComponentCanonicalTypes();

struct ComponentRecordField {
  CacheableName name;
  ComponentType type;

  ComponentRecordField(CacheableName&& name_, ComponentType type_)
      : name(std::move(name_)), type(type_) {}

  bool operator==(const ComponentRecordField& other) const {
    return name == other.name && type == other.type;
  }
};

struct ComponentVariantCase {
  CacheableName name;
  mozilla::Maybe<ComponentType> type;

  bool operator==(const ComponentVariantCase& other) const {
    return name == other.name && ComponentType::maybeEquals(type, other.type);
  }
};

struct ComponentResultType {
  mozilla::Maybe<ComponentType> type;
  mozilla::Maybe<ComponentType> errorType;

  static bool equals(const ComponentResultType& a,
                     const ComponentResultType& b) {
    return ComponentType::maybeEquals(a.type, b.type) &&
           ComponentType::maybeEquals(a.errorType, b.errorType);
  }
};

struct ComponentFuncType {
  ComponentTypeVector paramTypes;
  CacheableNameVector paramNames;
  mozilla::Maybe<ComponentType> resultType;

  bool operator==(const ComponentFuncType& other) const {
    MOZ_RELEASE_ASSERT(paramTypes.length() == paramNames.length());
    MOZ_RELEASE_ASSERT(other.paramTypes.length() == other.paramNames.length());
    if (paramTypes.length() != other.paramTypes.length()) {
      return false;
    }

    for (size_t i = 0; i < paramTypes.length(); i++) {
      if (paramTypes[i] != other.paramTypes[i] ||
          paramNames[i] != other.paramNames[i]) {
        return false;
      }
    }

    if (!ComponentType::maybeEquals(resultType, other.resultType)) {
      return false;
    }

    return true;
  }
};

class ComponentResourceType {
  // All resource types have (rep i32) for the time being.

  mozilla::Maybe<uint32_t> dtorIndex_;

 public:
  explicit ComponentResourceType(
      mozilla::Maybe<uint32_t> dtorIndex = mozilla::Nothing())
      : dtorIndex_(dtorIndex) {}

  mozilla::Maybe<uint32_t> dtorIndex() const { return dtorIndex_; }
};

using ComponentTypeSchema = mozilla::Variant<
    mozilla::Nothing, ComponentType, ComponentRecordFieldVector,
    ComponentVariantCaseVector, ComponentTypeVector, CacheableNameVector,
    ComponentResultType, ComponentFuncType, ComponentResourceType>;

class ComponentTypeDef : public AtomicRefCounted<ComponentTypeDef> {
  ComponentTypeSchema schema_;

 public:
  explicit ComponentTypeDef(ComponentTypeSchema&& schema)
      : schema_(std::move(schema)) {}

  const ComponentTypeSchema& schema() const { return schema_; }

  // Checks two typedefs for structural equality. Note that this is NOT the same
  // as comparing two types for equality, because a) not all types even have
  // ComponentTypeDefs, b) different type kinds may share the same kind of
  // backing storage (e.g. flags and enums), and c) because this method always
  // considers resource types to be unequal.
  static bool structurallyEqual(const ComponentTypeDef& a,
                                const ComponentTypeDef& b);
};

class Component;

[[nodiscard]] bool FlattenTypes(const Component& c,
                                const ComponentTypeVector& types,
                                ValTypeVector* result);
[[nodiscard]] bool FlattenType(const Component& c, const ComponentType& type,
                               ValTypeVector* result);
[[nodiscard]] bool FlattenRecord(const Component& c,
                                 const ComponentRecordFieldVector& fields,
                                 ValTypeVector* result);
mozilla::Maybe<FuncType> FlattenFuncType(const Component& c,
                                         const ComponentFuncType& funcType);

// A hash policy for StronglyUniqueNameSet that hashes items based on their
// trimmed, lowercased versions, but matches based on the full strongly-unique
// rules.
//
// The full strongly-unique rules are not hash-friendly; we have not yet figured
// out any way to "normalize" the name to a unique key that satisfies the
// strange carve-out rules for constructor and method names. But, we don't want
// to quadratically check each new name against every other name, so we take a
// disappointing halfway approach of hashing only the base part of the name, and
// then running the full strongly-unique logic in `match`. This results in more
// hash collisions and a less-inexpensive `match` method, but at least it keeps
// things from growing quadratically.
struct StronglyUniqueNameHasher {
  using Key = CacheableName;
  using Lookup = mozilla::Span<const char>;

  static HashNumber hash(const Lookup& aLookup);
  static bool match(const Key& aKey, const Lookup& aLookup);
};

// A class which can be used to check if a set of component model names is
// strongly-unique. The set owns its keys.
class StronglyUniqueNameSet {
  mozilla::HashSet<CacheableName, StronglyUniqueNameHasher, SystemAllocPolicy>
      data_;

 public:
  [[nodiscard]] bool add(mozilla::Span<const char> name, bool* duplicate);
};

struct ComponentCanonOpt {
  // TODO(wasm-cm)
};

using ComponentCanonOptVector =
    mozilla::Vector<ComponentCanonOpt, 0, SystemAllocPolicy>;

class ComponentFuncDesc {
  uint32_t typeIndex_;
  ComponentCanonOptVector canonOpts_;

 public:
  ComponentFuncDesc(uint32_t typeIndex, ComponentCanonOptVector&& canonOpts)
      : typeIndex_(typeIndex), canonOpts_(std::move(canonOpts)) {}

  // This returns the raw type index. To get the ComponentFuncType, call
  // Component::typeForFunc instead.
  uint32_t typeIndex() const { return typeIndex_; }
  const ComponentCanonOptVector& canonOpts() const { return canonOpts_; }
};

enum class ComponentAliasKind : uint8_t {
  CoreExport,
  Export,
  Outer,
};

// A generalized reference to an item in the component model. A ComponentItem
// may reference an import, an export, an item defined in the component itself,
// or an alias to an item defined elsewhere. This is the main type used for each
// index space in the component model, as imports, exports, aliases, and defined
// items can be interleaved in any order.
//
// The data is stored into two fields, one of which identifies the index space
// for the item (possibly in another component), and the other of which is the
// index in that index space.
//
// This first field, whatAndWhere_, stores all the information necessary to find
// the index space for the item. It is a packed field laid out like so:
//
//     00 00 00000000 00000000000000000000
//     │  │  │        └ instance index (ItemKind::Alias only)
//     │  │  └ alias sort (type ComponentSort, ItemKind::Alias only)
//     │  └ alias kind (type ComponentAliasKind, ItemKind::Alias only)
//     └ kind (type ItemKind)
//
// For all ItemKinds except ItemKind::Alias, this is basically a big 32-bit enum
// where only the top two bits are used. But for ItemKind::Alias we additionally
// store the ComponentAliasKind (core export alias, component export alias, or
// outer alias) and the ComponentSort (e.g. Func or Type). Finally there is the
// instance index, which is the index of the core instance, component instance,
// or outer component to fetch an item from.
//
// The second field, itemIndex_, is simply a uint32_t item index like you'd find
// anywhere else. Together, this means the common case for defined items,
// imports, and exports is just:
//
//     if (whatAndWhere_ == (ItemKind::Defined << ItemKindShift)) {
//         return items[itemIndex_];
//     }
//
class ComponentItem {
  uint32_t whatAndWhere_;
  uint32_t itemIndex_;

 public:
  static constexpr uint32_t ItemKindShift = 30;
  static constexpr uint32_t ItemKindMask = 0b11 << ItemKindShift;
  static constexpr uint32_t AliasKindShift = 28;
  static constexpr uint32_t AliasKindMask = 0b11 << AliasKindShift;
  static constexpr uint32_t AliasSortShift = 20;
  static constexpr uint32_t AliasSortMask = 0b11111111 << AliasSortShift;
  static constexpr uint32_t AliasInstanceMask = (1 << AliasSortShift) - 1;

  enum class ItemKind : uint8_t {
    Defined,
    Import,
    Export,
    Alias,
  };

  explicit ComponentItem(ItemKind kind, uint32_t itemIndex)
      : whatAndWhere_(uint32_t(kind) << ItemKindShift), itemIndex_(itemIndex) {
    MOZ_ASSERT(kind != ItemKind::Alias);
    MOZ_ASSERT(this->kind() == kind);
  }
  explicit ComponentItem(ComponentAliasKind aliasKind, ComponentSort sort,
                         uint32_t instanceIndex, uint32_t itemIndex)
      : whatAndWhere_(0), itemIndex_(itemIndex) {
    MOZ_ASSERT((instanceIndex & ~AliasInstanceMask) == 0);
    whatAndWhere_ |= uint32_t(ItemKind::Alias) << ItemKindShift;
    whatAndWhere_ |= uint32_t(aliasKind) << AliasKindShift;
    whatAndWhere_ |= uint32_t(sort) << AliasSortShift;
    whatAndWhere_ |= instanceIndex;

    MOZ_ASSERT(kind() == ItemKind::Alias);
    MOZ_ASSERT(this->aliasKind() == aliasKind);
    MOZ_ASSERT(aliasSort() == sort);
    MOZ_ASSERT(aliasInstanceIndex() == instanceIndex);
  }

 public:
  static ComponentItem defined(uint32_t itemIndex) {
    return ComponentItem(ItemKind::Defined, itemIndex);
  }
  static ComponentItem import(uint32_t itemIndex) {
    return ComponentItem(ItemKind::Import, itemIndex);
  }
  static ComponentItem export_(uint32_t itemIndex) {
    return ComponentItem(ItemKind::Export, itemIndex);
  }
  static ComponentItem alias(ComponentAliasKind aliasKind, ComponentSort sort,
                             uint32_t instanceIndex, uint32_t itemIndex) {
    return ComponentItem(aliasKind, sort, instanceIndex, itemIndex);
  }

  ItemKind kind() const {
    return ItemKind((whatAndWhere_ & ItemKindMask) >> ItemKindShift);
  }
  uint32_t itemIndex() const { return itemIndex_; }

  ComponentAliasKind aliasKind() const {
    MOZ_RELEASE_ASSERT(kind() == ItemKind::Alias);
    return ComponentAliasKind((whatAndWhere_ & AliasKindMask) >>
                              AliasKindShift);
  }
  ComponentSort aliasSort() const {
    MOZ_RELEASE_ASSERT(kind() == ItemKind::Alias);
    return ComponentSort((whatAndWhere_ & AliasSortMask) >> AliasSortShift);
  }
  uint32_t aliasInstanceIndex() const {
    MOZ_RELEASE_ASSERT(kind() == ItemKind::Alias);
    return whatAndWhere_ & AliasInstanceMask;
  }
};

// TODO(wasm-cm): Add static asserts for MaxComponents and
// MaxComponentNestingDepth or whatever, eventually
static_assert(MaxComponentCoreInstances <= ComponentItem::AliasInstanceMask);

struct CoreInstanceInstantiateArg {
  CacheableName name;
  uint32_t instanceIndex;
};

using CoreInstanceInstantiateArgVector =
    mozilla::Vector<CoreInstanceInstantiateArg, 0, SystemAllocPolicy>;

// Instructions for instantiating a core instance from a core module,
// corresponding to this text production:
//
//     (core instance (instantiate <modidx>) (with ...)*)`
//
struct CoreInstanceDescFromModule {
  // The core module to instantiate.
  uint32_t moduleIndex;

  // The instance's "with" declarations. In the binary format there is no inline
  // export form, only a form that uses the exports of another core instance.
  CoreInstanceInstantiateArgVector args;
};

// Instructions for instantiating a core instance by re-exporting core items
// already present in the component's index spaces. Corresponds to this text:
//
//     (core instance (export ...)*)
//
// This form of core instantiation semantically creates a new anonymous module
// which imports the given definitions and re-exports them. Alternatively, you
// can consider it a mere renaming of the items exported by other modules, but
// creating an anonymous module simplifies our implementation. Note that the
// module does not live in the component's core module index space.
//
// TODO(wasm-cm): Fill this out and figure out how to satisfy the module's
// imports.
struct CoreInstanceDescFromInlineExports {
  SharedModule mod;
};

// Instructions for instantiating a core instance.
using CoreInstanceDesc = mozilla::Variant<CoreInstanceDescFromModule,
                                          CoreInstanceDescFromInlineExports>;

// Describes an import or export from a wasm component.
class ComponentExternDesc {
  ComponentSort sort_;
  ComponentType type_;

  // TODO(wasm-cm): This is a total hack, but since we currently don't have a
  // notion of core module types, we actually just store the index of the
  // relevant core module within the component. This obviously will not work as
  // soon as we do anything with multiple components.
  uint32_t coreModuleIndex_;

  explicit ComponentExternDesc(ComponentSort sort, ComponentType&& type)
      : sort_(sort), type_(std::move(type)) {
    MOZ_ASSERT(ComponentSortValidForExternDesc(sort));
  }
  explicit ComponentExternDesc(uint32_t coreModuleIndex)
      : sort_(ComponentSort::CoreModule), coreModuleIndex_(coreModuleIndex) {}

 public:
  ComponentExternDesc() = default;

  static ComponentExternDesc func(ComponentType&& funcType) {
    MOZ_ASSERT(funcType.kind() == ComponentTypeKind::Func);
    return ComponentExternDesc(ComponentSort::Func, std::move(funcType));
  }
  static ComponentExternDesc type(ComponentType&& type) {
    return ComponentExternDesc(ComponentSort::Type, std::move(type));
  }
  static ComponentExternDesc coreModule(uint32_t coreModuleIndex) {
    return ComponentExternDesc(coreModuleIndex);
  }

  bool isValid() const { return sort_ != ComponentSort::Invalid; }
  ComponentSort sort() const { return sort_; }
  ComponentType asFunc() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::Func);
    return type_;
  }
  ComponentType asType() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::Type);
    return type_;
  }
  uint32_t asCoreModule() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::CoreModule);
    // TODO(wasm-cm): This should obviously return a proper core module type,
    // when we actually support that.
    return coreModuleIndex_;
  }

  static bool matches(const ComponentExternDesc& sub,
                      const ComponentExternDesc& super);
};

static_assert(std::is_default_constructible_v<ComponentExternDesc>);

class ComponentImport {
  CacheableName name_;
  ComponentExternDesc externDesc_;

 public:
  explicit ComponentImport(CacheableName&& name,
                           const ComponentExternDesc& externDesc)
      : name_(std::move(name)), externDesc_(externDesc) {}

  const CacheableName& name() const { return name_; }
  const ComponentExternDesc& externDesc() const { return externDesc_; }
};

class ComponentExport {
  CacheableName name_;
  ComponentExternDesc externDesc_;

 public:
  explicit ComponentExport(CacheableName&& name, ComponentExternDesc externDesc)
      : name_(std::move(name)), externDesc_(externDesc) {}

  const CacheableName& name() const { return name_; }
  const ComponentExternDesc& externDesc() const { return externDesc_; }
};

// TODO(wasm-cm): This type is enormous, but a lot of the storage is due to
// containers like HashMap and Vector that aren't actually required once the
// component is built and validated. It would probably be smart to split this
// into ComponentBuilder and Component classes so that the final version can be
// smaller. (After all, we will have a lot of components in practice!)
class Component : public JS::WasmComponent {
 public:
  using CoreModuleVector = mozilla::Vector<SharedModule, 0, SystemAllocPolicy>;
  using CoreInstanceVector =
      mozilla::Vector<CoreInstanceDesc, 0, SystemAllocPolicy>;
  using TypeVector = mozilla::Vector<ComponentType, 0, SystemAllocPolicy>;
  using FuncVector = mozilla::Vector<ComponentFuncDesc, 0, SystemAllocPolicy>;
  using ImportVector = mozilla::Vector<ComponentImport, 0, SystemAllocPolicy>;
  using ExportVector = mozilla::Vector<ComponentExport, 0, SystemAllocPolicy>;
  using ItemVector = mozilla::Vector<ComponentItem, 0, SystemAllocPolicy>;

 private:
  CoreModuleVector definedCoreModules_;
  CoreInstanceVector definedCoreInstances_;
  TypeVector definedTypes_;
  FuncVector definedFuncs_;
  ImportVector imports_;
  ExportVector exports_;

  ItemVector funcs_;
  ItemVector types_;
  ItemVector components_;
  ItemVector instances_;
  ItemVector coreFuncs_;
  ItemVector coreTables_;
  ItemVector coreMemories_;
  ItemVector coreGlobals_;
  ItemVector coreTags_;
  ItemVector coreTypes_;
  ItemVector coreModules_;
  ItemVector coreInstances_;

  template <typename T>
  bool addDefinedItem(
      T&& item, mozilla::Vector<T, 0, SystemAllocPolicy>& definedItemsVector,
      ItemVector& indexSpaceVector) {
    uint32_t index = definedItemsVector.length();
    if (!definedItemsVector.append(std::forward<T>(item))) {
      return false;
    }
    return indexSpaceVector.append(ComponentItem::defined(index));
  }

 public:
  Component() = default;

  // --------------------------------------------------------------------------
  // Accessors and adders for each index space

  const ImportVector& imports() const { return imports_; }
  [[nodiscard]] bool addImport(ComponentImport&& import);

  const ExportVector& exports() const { return exports_; }
  [[nodiscard]] bool addExport(ComponentExport&& exp);

  const ItemVector& funcs() const { return funcs_; }
  [[nodiscard]] bool addFunc(ComponentFuncDesc&& func) {
    return addDefinedItem(std::move(func), definedFuncs_, funcs_);
  }

  const ItemVector& types() const { return types_; }
  [[nodiscard]] bool addType(ComponentType&& type) {
    MOZ_RELEASE_ASSERT(type.isValid());
    return addDefinedItem(std::move(type), definedTypes_, types_);
  }

  // TODO(wasm-cm): Functions for components
  // TODO(wasm-cm): Functions for component instances

  const ItemVector& coreFuncs() const { return coreFuncs_; }
  [[nodiscard]] bool addCoreFunc(ComponentItem&& funcItem) {
    return coreFuncs_.append(std::move(funcItem));
  }

  const ItemVector& coreTables() const { return coreTables_; }
  [[nodiscard]] bool addCoreTable(ComponentItem&& tableItem) {
    return coreTables_.append(std::move(tableItem));
  }

  const ItemVector& coreMemories() const { return coreMemories_; }
  [[nodiscard]] bool addCoreMemory(ComponentItem&& memoryItem) {
    return coreMemories_.append(std::move(memoryItem));
  }

  const ItemVector& coreGlobals() const { return coreGlobals_; }
  [[nodiscard]] bool addCoreGlobal(ComponentItem&& globalItem) {
    return coreGlobals_.append(std::move(globalItem));
  }

  const ItemVector& coreTags() const { return coreTags_; }
  bool addCoreTag(ComponentItem&& tagItem) {
    return coreTags_.append(std::move(tagItem));
  }

  const ItemVector& coreModules() const { return coreModules_; }
  [[nodiscard]] bool addCoreModule(SharedModule module) {
    return addDefinedItem(std::move(module), definedCoreModules_, coreModules_);
  }

  const ItemVector& coreInstances() const { return coreInstances_; }
  [[nodiscard]] bool addCoreInstance(CoreInstanceDesc&& instance) {
    return addDefinedItem(std::move(instance), definedCoreInstances_,
                          coreInstances_);
  }

  // --------------------------------------------------------------------------
  // Utilities for accessing type information

  // Gets a type from the component's type index space.
  ComponentType getType(uint32_t typeIndex) const {
    ComponentItem item = types_[typeIndex];
    switch (item.kind()) {
      case ComponentItem::ItemKind::Defined:
        return definedTypes_[item.itemIndex()];
      case ComponentItem::ItemKind::Import:
        return imports_[item.itemIndex()].externDesc().asType();
      case ComponentItem::ItemKind::Export:
        return exports_[item.itemIndex()].externDesc().asType();
      case ComponentItem::ItemKind::Alias:
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  // Gets the type of a component func (not a core func). It is always safe to
  // call `.asFunc()` on the result.
  ComponentType getTypeForFunc(uint32_t funcIndex) const {
    ComponentItem item = funcs_[funcIndex];
    switch (item.kind()) {
      case ComponentItem::ItemKind::Defined:
        return getType(definedFuncs_[item.itemIndex()].typeIndex());
      case ComponentItem::ItemKind::Import:
        return imports_[item.itemIndex()].externDesc().asFunc();
      case ComponentItem::ItemKind::Export:
        return exports_[item.itemIndex()].externDesc().asFunc();
      case ComponentItem::ItemKind::Alias:
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  // Gets the type of a core func (not a component func).
  const FuncType& getCoreFuncTypeForCoreFunc(uint32_t coreFuncIndex) const {
    ComponentItem item = coreFuncs_[coreFuncIndex];
    switch (item.kind()) {
      case ComponentItem::ItemKind::Defined: {
        // TODO(wasm-cm): Fix this when (canon lower) is supported.
        MOZ_CRASH("should be impossible for now");
      } break;
      case ComponentItem::ItemKind::Import:
      case ComponentItem::ItemKind::Export:
        // Core funcs cannot be imported or exported
        MOZ_CRASH();
      case ComponentItem::ItemKind::Alias: {
        MOZ_ASSERT(item.aliasKind() == ComponentAliasKind::CoreExport);
        SharedModule mod =
            getCoreModuleForCoreInstance(item.aliasInstanceIndex());
        uint32_t ft = mod->codeMeta().funcs[item.itemIndex()].typeIndex;
        return mod->codeMeta().types->type(ft).funcType();
      } break;
      default:
        MOZ_CRASH();
    }
  }

  SharedModule getCoreModule(uint32_t modIndex) const {
    ComponentItem item = coreModules_[modIndex];
    switch (item.kind()) {
      case ComponentItem::ItemKind::Defined:
        return definedCoreModules_[item.itemIndex()];
      case ComponentItem::ItemKind::Import:
        // TODO(wasm-cm): Fix when core module types are supported
        MOZ_CRASH("should be impossible for now");
      case ComponentItem::ItemKind::Export: {
        const ComponentExport& exp = exports_[item.itemIndex()];
        MOZ_ASSERT(exp.externDesc().sort() == ComponentSort::CoreModule);
        return definedCoreModules_[exp.externDesc().asCoreModule()];
      } break;
      case ComponentItem::ItemKind::Alias:
        // TODO(wasm-cm): Fix when nested components are supported
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  SharedModule getCoreModuleForCoreInstance(uint32_t instanceIndex) const {
    ComponentItem item = coreInstances_[instanceIndex];
    switch (item.kind()) {
      case ComponentItem::ItemKind::Defined: {
        const CoreInstanceDesc& instance =
            definedCoreInstances_[item.itemIndex()];
        if (instance.is<CoreInstanceDescFromModule>()) {
          return getCoreModule(
              instance.as<CoreInstanceDescFromModule>().moduleIndex);
        }
        return instance.as<CoreInstanceDescFromInlineExports>().mod;
      } break;
      case ComponentItem::ItemKind::Import:
      case ComponentItem::ItemKind::Export:
        // Core instances cannot be imported or exported
        MOZ_CRASH();
      case ComponentItem::ItemKind::Alias:
        // TODO(wasm-cm): Fix once nested components are supported
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  size_t gcMallocBytesExcludingCode() const {
    // TODO(wasm-cm): Right now, this only sums up the sizes of the inner
    // modules, but this is not an accurate picture of a component's memory
    // footprint.
    size_t total = 0;
    for (const SharedModule& module : definedCoreModules_) {
      total += module->gcMallocBytesExcludingCode();
    }
    return total;
  }

  size_t tier1CodeMemoryUsed() const {
    // TODO(wasm-cm): As above, this only sums up the memory for core modules,
    // and does not account for other potential code memory.
    size_t total = 0;
    for (const SharedModule& module : definedCoreModules_) {
      total += module->tier1CodeMemoryUsed();
    }
    return total;
  }

 private:
  // JS API and JS::WasmComponent implementation:
  JSObject* createObject(JSContext* cx) const override;
};

using MutableComponent = RefPtr<Component>;
using SharedComponent = RefPtr<const Component>;

}  // namespace wasm
}  // namespace js

#endif  // ENABLE_WASM_COMPONENTS

#endif  // wasm_component_h
