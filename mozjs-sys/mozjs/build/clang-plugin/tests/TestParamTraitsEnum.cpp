#include <cstdint>

typedef enum {
  BadFirst,
  BadSecond,
  BadThird
} BadEnum;

typedef enum {
  NestedFirst,
  NestedSecond
} NestedBadEnum;

typedef enum {
  GoodFirst,
  GoodSecond,
  GoodLast
} GoodEnum;

enum RawEnum {
  RawFirst,
  RawLast
};

enum class ClassEnum {
  ClassFirst,
  ClassLast
};

enum class TypedClassEnum : uint32_t {
  TypedFirst,
  TypedLast
};

enum class nsresult : uint32_t {
  NS_OK = 0
};

template <class P> struct ParamTraits;

// Simplified EnumSerializer etc. from IPCMessageUtils.h
template <typename E, typename EnumValidator>
struct EnumSerializer {
};

template <typename E,
          E MinLegal,
          E HighBound>
class ContiguousEnumValidator
{};

// Make sure the class derived from EnumSerializer doesn't error
template <typename E,
          E MinLegal,
          E HighBound>
struct ContiguousEnumSerializer
  : EnumSerializer<E,
                   ContiguousEnumValidator<E, MinLegal, HighBound>>
{};

// Typical ParamTraits implementation that should be avoided
template<>
struct ParamTraits<ClassEnum> // expected-error {{Custom ParamTraits implementation for an enum type}} expected-note {{Please use a helper class for example ContiguousEnumSerializer}}
{
  // Make sure the matcher doesn't need a typedef.
};

template<>
struct ParamTraits<TypedClassEnum> // expected-error {{Custom ParamTraits implementation for an enum type}} expected-note {{Please use a helper class for example ContiguousEnumSerializer}}
{
};

template<>
struct ParamTraits<enum RawEnum> // expected-error {{Custom ParamTraits implementation for an enum type}} expected-note {{Please use a helper class for example ContiguousEnumSerializer}}
{
};

// Make sure forward declarations are not flagged
template <> struct ParamTraits<BadEnum>;

struct SomeClass {
  enum FooBar {};
  enum class FooBarClass {};

  friend struct ParamTraits<FooBar>;
  friend struct ParamTraits<FooBarClass>;
};

template<>
struct ParamTraits<BadEnum> // expected-error {{Custom ParamTraits implementation for an enum type}} expected-note {{Please use a helper class for example ContiguousEnumSerializer}}
{
};

// Make sure the analysis catches nested typedefs
typedef NestedBadEnum NestedDefLevel1;
typedef NestedDefLevel1 NestedDefLevel2;

template<>
struct ParamTraits<NestedDefLevel2> // expected-error {{Custom ParamTraits implementation for an enum type}} expected-note {{Please use a helper class for example ContiguousEnumSerializer}}
{
};

// Make sure a non enum typedef is not accidentally flagged
typedef int IntTypedef;

template<>
struct ParamTraits<IntTypedef>
{
};

// Make sure ParamTraits using helper classes are not flagged
template<>
struct ParamTraits<GoodEnum>
: public ContiguousEnumSerializer<GoodEnum,
                                  GoodEnum::GoodFirst,
                                  GoodEnum::GoodLast>
{};

// nsresult has special handling via ParamTraitsMozilla and should be allowed
template<>
struct ParamTraits<nsresult>
{};
