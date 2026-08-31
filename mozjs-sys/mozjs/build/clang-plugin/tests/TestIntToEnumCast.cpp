enum UnscopedEnum { kFirst, kSecond, kThird };

enum class ScopedEnum { A, B, C };

enum class TypedEnum : unsigned { X = 0, Y = 1 };

// Minimal mock of the IPC base class.
namespace mozilla {
namespace ipc {
class IProtocol {
public:
  virtual ~IProtocol() = default;
};
} // namespace ipc
} // namespace mozilla

// Simulates an IPC actor class (inherits from IProtocol).
class MockCDMParent : public mozilla::ipc::IProtocol {
public:
  void RecvDecryptFailed(unsigned aStatus) {
    ScopedEnum e = static_cast<ScopedEnum>(aStatus); // expected-error {{static_cast from builtin type 'unsigned int' to enum type 'ScopedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
  }

  void RecvOnRejectPromise(unsigned aException) {
    UnscopedEnum e = static_cast<UnscopedEnum>(aException); // expected-error {{static_cast from builtin type 'unsigned int' to enum type 'UnscopedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
  }

  void RecvDecoderInit(unsigned aStatus) {
    TypedEnum e = static_cast<TypedEnum>(aStatus); // expected-error {{static_cast from builtin type 'unsigned int' to enum type 'TypedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
  }

  void RecvCStyleCast(unsigned aStatus) {
    ScopedEnum e = (ScopedEnum)aStatus; // expected-error {{C-style cast from builtin type 'unsigned int' to enum type 'ScopedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
  }

  void RecvFunctionalCast(unsigned aStatus) {
    UnscopedEnum e = UnscopedEnum(aStatus); // expected-error {{functional cast from builtin type 'unsigned int' to enum type 'UnscopedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
  }

  // Casts inside a lambda body (e.g. a promise callback) must also be caught.
  void RecvInLambda(unsigned aStatus) {
    auto callback = [](unsigned aValue) {
      ScopedEnum e = static_cast<ScopedEnum>(aValue); // expected-error {{static_cast from builtin type 'unsigned int' to enum type 'ScopedEnum' in an IPC actor method}} expected-note {{consider using the enum type directly}}
      return e;
    };
    callback(aStatus);
  }

};

// Simulates a non-IPC class — no warnings expected.
class RegularClass {
public:
  void doSomething(unsigned aValue) {
    ScopedEnum e1 = static_cast<ScopedEnum>(aValue);
    ScopedEnum e2 = (ScopedEnum)aValue;
    UnscopedEnum e3 = UnscopedEnum(aValue);
  }
};

// Free function — no warnings expected.
void freeFunction(unsigned aValue) {
  ScopedEnum e1 = static_cast<ScopedEnum>(aValue);
  ScopedEnum e2 = (ScopedEnum)aValue;
  UnscopedEnum e3 = UnscopedEnum(aValue);
}
