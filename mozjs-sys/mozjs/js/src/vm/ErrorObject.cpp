/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/ErrorObject-inl.h"

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "jspubtd.h"
#include "NamespaceImports.h"

#include "gc/AllocKind.h"
#include "gc/GCContext.h"
#include "js/CallArgs.h"
#include "js/CallNonGenericMethod.h"
#include "js/CharacterEncoding.h"  // JS::ConstUTF8CharsZ
#include "js/Class.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/Conversions.h"
#include "js/ErrorReport.h"
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/friend/StackLimits.h"    // js::AutoCheckRecursionLimit
#include "js/PropertyAndElement.h"
#include "js/PropertySpec.h"
#include "js/RootingAPI.h"
#include "js/SavedFrameAPI.h"
#include "js/Stack.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "js/Value.h"
#include "js/Wrapper.h"
#include "util/StringBuilder.h"
#include "vm/ErrorReporting.h"
#include "vm/GlobalObject.h"
#include "vm/Iteration.h"
#include "vm/JSAtomUtils.h"  // ClassName
#include "vm/JSFunction.h"
#include "vm/JSObject.h"
#include "vm/NativeObject.h"
#include "vm/ObjectOperations.h"
#include "vm/SavedStacks.h"
#include "vm/SelfHosting.h"
#include "vm/Shape.h"
#include "vm/Stack.h"
#include "vm/StringType.h"
#include "vm/ToSource.h"  // js::ValueToSource

#include "vm/Compartment-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/ObjectOperations-inl.h"
#include "vm/Realm-inl.h"
#include "vm/SavedStacks-inl.h"
#include "vm/Shape-inl.h"

using namespace js;

#define IMPLEMENT_ERROR_PROTO_CLASS(name)                        \
  {#name ".prototype", JSCLASS_HAS_CACHED_PROTO(JSProto_##name), \
   JS_NULL_CLASS_OPS,                                            \
   &ErrorObject::classSpecs[JSProto_##name - JSProto_Error]}

const JSClass ErrorObject::protoClasses[JSEXN_ERROR_LIMIT] = {
    IMPLEMENT_ERROR_PROTO_CLASS(Error),

    IMPLEMENT_ERROR_PROTO_CLASS(InternalError),
    IMPLEMENT_ERROR_PROTO_CLASS(AggregateError),
    IMPLEMENT_ERROR_PROTO_CLASS(EvalError),
    IMPLEMENT_ERROR_PROTO_CLASS(RangeError),
    IMPLEMENT_ERROR_PROTO_CLASS(ReferenceError),
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    IMPLEMENT_ERROR_PROTO_CLASS(SuppressedError),
#endif
    IMPLEMENT_ERROR_PROTO_CLASS(SyntaxError),
    IMPLEMENT_ERROR_PROTO_CLASS(TypeError),
    IMPLEMENT_ERROR_PROTO_CLASS(URIError),

    IMPLEMENT_ERROR_PROTO_CLASS(DebuggeeWouldRun),
    IMPLEMENT_ERROR_PROTO_CLASS(CompileError),
    IMPLEMENT_ERROR_PROTO_CLASS(LinkError),
    IMPLEMENT_ERROR_PROTO_CLASS(RuntimeError),
#ifdef ENABLE_WASM_JSPI
    IMPLEMENT_ERROR_PROTO_CLASS(SuspendError),
#endif
};

static bool exn_toSource(JSContext* cx, unsigned argc, Value* vp);

static const JSFunctionSpec error_methods[] = {
    JS_FN("toSource", exn_toSource, 0, 0),
    JS_SELF_HOSTED_FN("toString", "ErrorToString", 0, 0),
    JS_FS_END,
};

static bool exn_isError(JSContext* cx, unsigned argc, Value* vp);

static bool exn_captureStackTrace(JSContext* cx, unsigned argc, Value* vp);

static const JSFunctionSpec error_static_methods[] = {
    JS_FN("isError", exn_isError, 1, 0),
    JS_FN("captureStackTrace", exn_captureStackTrace, 2, 0),
    JS_FS_END,
};

static const JSPropertySpec error_static_properties[] = {
    JS_INT32_PS("stackTraceLimit", int32_t(MAX_REPORTED_STACK_DEPTH),
                JSPROP_ENUMERATE),
    JS_PS_END,
};

// Error.prototype and NativeError.prototype have own .message and .name
// properties.
#define COMMON_ERROR_PROPERTIES(name) \
  JS_STRING_PS("message", "", 0), JS_STRING_PS("name", #name, 0)

static const JSPropertySpec error_properties[] = {
    COMMON_ERROR_PROPERTIES(Error),
    // Only Error.prototype has .stack!
    JS_PSGS("stack", ErrorObject::getStack, ErrorObject::setStack, 0),
    JS_PS_END,
};

#define IMPLEMENT_NATIVE_ERROR_PROPERTIES(name)       \
  static const JSPropertySpec name##_properties[] = { \
      COMMON_ERROR_PROPERTIES(name), JS_PS_END};

IMPLEMENT_NATIVE_ERROR_PROPERTIES(InternalError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(AggregateError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(EvalError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(RangeError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(ReferenceError)
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
IMPLEMENT_NATIVE_ERROR_PROPERTIES(SuppressedError)
#endif
IMPLEMENT_NATIVE_ERROR_PROPERTIES(SyntaxError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(TypeError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(URIError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(DebuggeeWouldRun)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(CompileError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(LinkError)
IMPLEMENT_NATIVE_ERROR_PROPERTIES(RuntimeError)
#ifdef ENABLE_WASM_JSPI
IMPLEMENT_NATIVE_ERROR_PROPERTIES(SuspendError)
#endif

#define IMPLEMENT_NATIVE_ERROR_SPEC(name) \
  {ErrorObject::createConstructor,        \
   ErrorObject::createProto,              \
   nullptr,                               \
   nullptr,                               \
   nullptr,                               \
   name##_properties,                     \
   nullptr,                               \
   JSProto_Error}

#define IMPLEMENT_NONGLOBAL_ERROR_SPEC(name) \
  {ErrorObject::createConstructor,           \
   ErrorObject::createProto,                 \
   nullptr,                                  \
   nullptr,                                  \
   nullptr,                                  \
   name##_properties,                        \
   nullptr,                                  \
   JSProto_Error | ClassSpec::DontDefineConstructor}

const ClassSpec ErrorObject::classSpecs[JSEXN_ERROR_LIMIT] = {
    {ErrorObject::createConstructor, ErrorObject::createProto,
     error_static_methods, error_static_properties, error_methods,
     error_properties},

    IMPLEMENT_NATIVE_ERROR_SPEC(InternalError),
    IMPLEMENT_NATIVE_ERROR_SPEC(AggregateError),
    IMPLEMENT_NATIVE_ERROR_SPEC(EvalError),
    IMPLEMENT_NATIVE_ERROR_SPEC(RangeError),
    IMPLEMENT_NATIVE_ERROR_SPEC(ReferenceError),
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    IMPLEMENT_NATIVE_ERROR_SPEC(SuppressedError),
#endif
    IMPLEMENT_NATIVE_ERROR_SPEC(SyntaxError),
    IMPLEMENT_NATIVE_ERROR_SPEC(TypeError),
    IMPLEMENT_NATIVE_ERROR_SPEC(URIError),

    IMPLEMENT_NONGLOBAL_ERROR_SPEC(DebuggeeWouldRun),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(CompileError),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(LinkError),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(RuntimeError),
#ifdef ENABLE_WASM_JSPI
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(SuspendError),
#endif
};

#define IMPLEMENT_ERROR_CLASS_CORE(name, reserved_slots) \
  {#name,                                                \
   JSCLASS_HAS_CACHED_PROTO(JSProto_##name) |            \
       JSCLASS_HAS_RESERVED_SLOTS(reserved_slots) |      \
       JSCLASS_BACKGROUND_FINALIZE,                      \
   &ErrorObjectClassOps,                                 \
   &ErrorObject::classSpecs[JSProto_##name - JSProto_Error]}

#define IMPLEMENT_ERROR_CLASS(name) \
  IMPLEMENT_ERROR_CLASS_CORE(name, ErrorObject::RESERVED_SLOTS)

// Only used for classes that could be a Wasm trap. Classes that use this
// macro should be kept in sync with the exception types that mightBeWasmTrap()
// will return true for.
#define IMPLEMENT_ERROR_CLASS_MAYBE_WASM_TRAP(name) \
  IMPLEMENT_ERROR_CLASS_CORE(name, ErrorObject::RESERVED_SLOTS_MAYBE_WASM_TRAP)

static void exn_finalize(JS::GCContext* gcx, JSObject* obj);

static const JSClassOps ErrorObjectClassOps = {
    .finalize = exn_finalize,
};

const JSClass ErrorObject::classes[JSEXN_ERROR_LIMIT] = {
    IMPLEMENT_ERROR_CLASS(Error),
    IMPLEMENT_ERROR_CLASS_MAYBE_WASM_TRAP(InternalError),
    IMPLEMENT_ERROR_CLASS(AggregateError),
    IMPLEMENT_ERROR_CLASS(EvalError),
    IMPLEMENT_ERROR_CLASS(RangeError),
    IMPLEMENT_ERROR_CLASS(ReferenceError),
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    IMPLEMENT_ERROR_CLASS(SuppressedError),
#endif
    IMPLEMENT_ERROR_CLASS(SyntaxError),
    IMPLEMENT_ERROR_CLASS(TypeError),
    IMPLEMENT_ERROR_CLASS(URIError),
    // These Error subclasses are not accessible via the global object:
    IMPLEMENT_ERROR_CLASS(DebuggeeWouldRun),
    IMPLEMENT_ERROR_CLASS(CompileError),
    IMPLEMENT_ERROR_CLASS(LinkError),
    IMPLEMENT_ERROR_CLASS_MAYBE_WASM_TRAP(RuntimeError),
#ifdef ENABLE_WASM_JSPI
    IMPLEMENT_ERROR_CLASS(SuspendError),
#endif
};

static void exn_finalize(JS::GCContext* gcx, JSObject* obj) {
  if (JSErrorReport* report = obj->as<ErrorObject>().getErrorReport()) {
    // Bug 1560019: This allocation is not currently tracked.
    gcx->deleteUntracked(report);
  }
}

static ErrorObject* CreateErrorObject(JSContext* cx, const CallArgs& args,
                                      unsigned messageArg, JSExnType exnType,
                                      HandleObject proto) {
  // Compute the error message, if any.
  RootedString message(cx, nullptr);
  if (args.hasDefined(messageArg)) {
    message = ToString<CanGC>(cx, args[messageArg]);
    if (!message) {
      return nullptr;
    }
  }

  // Don't interpret the two parameters following the message parameter as the
  // non-standard fileName and lineNumber arguments when we have an options
  // object argument and the exception type is not SuppressedError.
  bool hasOptions =
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
      args.get(messageArg + 1).isObject() && exnType != JSEXN_SUPPRESSEDERR;
#else
      args.get(messageArg + 1).isObject();
#endif

  Rooted<mozilla::Maybe<Value>> cause(cx, mozilla::Nothing());
  if (hasOptions) {
    RootedObject options(cx, &args[messageArg + 1].toObject());

    bool hasCause = false;
    if (!HasProperty(cx, options, cx->names().cause, &hasCause)) {
      return nullptr;
    }

    if (hasCause) {
      RootedValue causeValue(cx);
      if (!GetProperty(cx, options, options, cx->names().cause, &causeValue)) {
        return nullptr;
      }
      cause = mozilla::Some(causeValue.get());
    }
  }

  // Find the scripted caller, but only ones we're allowed to know about.
  NonBuiltinFrameIter iter(cx, cx->realm()->principals());

  RootedString fileName(cx);
  uint32_t sourceId = 0;
  if (!hasOptions && args.length() > messageArg + 1) {
    fileName = ToString<CanGC>(cx, args[messageArg + 1]);
  } else {
    fileName = cx->runtime()->emptyString;
    if (!iter.done()) {
      if (const char* cfilename = iter.filename()) {
        fileName = JS_NewStringCopyUTF8Z(
            cx, JS::ConstUTF8CharsZ(cfilename, strlen(cfilename)));
      }
      if (iter.hasScript()) {
        sourceId = iter.script()->scriptSource()->id();
      }
    }
  }
  if (!fileName) {
    return nullptr;
  }

  uint32_t lineNumber;
  JS::ColumnNumberOneOrigin columnNumber;
  if (!hasOptions && args.length() > messageArg + 2) {
    if (!ToUint32(cx, args[messageArg + 2], &lineNumber)) {
      return nullptr;
    }
  } else {
    JS::TaggedColumnNumberOneOrigin tmp;
    lineNumber = iter.done() ? 0 : iter.computeLine(&tmp);
    columnNumber = JS::ColumnNumberOneOrigin(tmp.oneOriginValue());
  }

  mozilla::Maybe<uint32_t> limit = GetStackTraceLimit(cx);
  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack, limit.valueOr(0))) {
    return nullptr;
  }

  Rooted<ErrorObject*> errObject(
      cx,
      ErrorObject::create(cx, exnType, stack, fileName, sourceId, lineNumber,
                          columnNumber, nullptr, message, cause, proto));
  if (!errObject) {
    return nullptr;
  }
  if (limit.isNothing() && !DefineDataProperty(cx, errObject, cx->names().stack,
                                               JS::UndefinedHandleValue)) {
    return nullptr;
  }
  return errObject;
}

static bool Error(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // ECMA ed. 3, 15.11.1 requires Error, etc., to construct even when
  // called as functions, without operator new.  But as we do not give
  // each constructor a distinct JSClass, we must get the exception type
  // ourselves.
  JSExnType exnType =
      JSExnType(args.callee().as<JSFunction>().getExtendedSlot(0).toInt32());

  MOZ_ASSERT(exnType != JSEXN_AGGREGATEERR,
             "AggregateError has its own constructor function");

#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
  MOZ_ASSERT(exnType != JSEXN_SUPPRESSEDERR,
             "SuppressedError has its own constuctor function");
#endif

  JSProtoKey protoKey =
      JSCLASS_CACHED_PROTO_KEY(&ErrorObject::classes[exnType]);

  // ES6 19.5.1.1 mandates the .prototype lookup happens before the toString
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey, &proto)) {
    return false;
  }

  auto* obj = CreateErrorObject(cx, args, 0, exnType, proto);
  if (!obj) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

// AggregateError ( errors, message )
static bool AggregateError(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  mozilla::DebugOnly<JSExnType> exnType =
      JSExnType(args.callee().as<JSFunction>().getExtendedSlot(0).toInt32());

  MOZ_ASSERT(exnType == JSEXN_AGGREGATEERR);

  // Steps 1-2. (9.1.13 OrdinaryCreateFromConstructor, steps 1-2).
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_AggregateError,
                                          &proto)) {
    return false;
  }

  // TypeError anyway, but this gives a better error message.
  if (!args.requireAtLeast(cx, "AggregateError", 1)) {
    return false;
  }

  // 9.1.13 OrdinaryCreateFromConstructor, step 3.
  // Step 3.
  Rooted<ErrorObject*> obj(
      cx, CreateErrorObject(cx, args, 1, JSEXN_AGGREGATEERR, proto));
  if (!obj) {
    return false;
  }

  // Step 4.

  Rooted<ArrayObject*> errorsList(cx, IterableToArray(cx, args.get(0)));
  if (!errorsList) {
    return false;
  }

  // Step 5.
  RootedValue errorsVal(cx, JS::ObjectValue(*errorsList));
  if (!NativeDefineDataProperty(cx, obj, cx->names().errors, errorsVal, 0)) {
    return false;
  }

  // Step 6.
  args.rval().setObject(*obj);
  return true;
}

#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
// Explicit Resource Management Proposal
// SuppressedError ( error, suppressed, message )
// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-suppressederror
static bool SuppressedError(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  mozilla::DebugOnly<JSExnType> exnType =
      JSExnType(args.callee().as<JSFunction>().getExtendedSlot(0).toInt32());

  MOZ_ASSERT(exnType == JSEXN_SUPPRESSEDERR);

  // Step 1. If NewTarget is undefined, let newTarget be the active function
  // object; else let newTarget be NewTarget.
  // Step 2. Let O be ? OrdinaryCreateFromConstructor(newTarget,
  // "%SuppressedError.prototype%", « [[ErrorData]] »).
  JS::Rooted<JSObject*> proto(cx);

  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_SuppressedError,
                                          &proto)) {
    return false;
  }

  // Step 3. If message is not undefined, then
  // Step 3.a. Let messageString be ? ToString(message).
  // Step 3.b. Perform CreateNonEnumerableDataPropertyOrThrow(O, "message",
  // messageString).
  JS::Rooted<ErrorObject*> obj(
      cx, CreateErrorObject(cx, args, 2, JSEXN_SUPPRESSEDERR, proto));

  if (!obj) {
    return false;
  }

  // Step 4. Perform CreateNonEnumerableDataPropertyOrThrow(O, "error", error).
  JS::Rooted<JS::Value> errorVal(cx, args.get(0));
  if (!NativeDefineDataProperty(cx, obj, cx->names().error, errorVal, 0)) {
    return false;
  }

  // Step 5. Perform CreateNonEnumerableDataPropertyOrThrow(O, "suppressed",
  // suppressed).
  JS::Rooted<JS::Value> suppressedVal(cx, args.get(1));
  if (!NativeDefineDataProperty(cx, obj, cx->names().suppressed, suppressedVal,
                                0)) {
    return false;
  }

  // Step 6. Return O.
  args.rval().setObject(*obj);
  return true;
}
#endif

/* static */
JSObject* ErrorObject::createProto(JSContext* cx, JSProtoKey key) {
  JSExnType type = ExnTypeFromProtoKey(key);

  if (type == JSEXN_ERR) {
    return GlobalObject::createBlankPrototype(
        cx, cx->global(), &ErrorObject::protoClasses[JSEXN_ERR]);
  }

  RootedObject protoProto(
      cx, GlobalObject::getOrCreateErrorPrototype(cx, cx->global()));
  if (!protoProto) {
    return nullptr;
  }

  return GlobalObject::createBlankPrototypeInheriting(
      cx, &ErrorObject::protoClasses[type], protoProto);
}

/* static */
JSObject* ErrorObject::createConstructor(JSContext* cx, JSProtoKey key) {
  JSExnType type = ExnTypeFromProtoKey(key);
  RootedObject ctor(cx);

  if (type == JSEXN_ERR) {
    ctor = GenericCreateConstructor<Error, 1, gc::AllocKind::FUNCTION_EXTENDED>(
        cx, key);
  } else {
    RootedFunction proto(
        cx, GlobalObject::getOrCreateErrorConstructor(cx, cx->global()));
    if (!proto) {
      return nullptr;
    }

    Native native;
    unsigned nargs;
    if (type == JSEXN_AGGREGATEERR) {
      native = AggregateError;
      nargs = 2;
    }
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    else if (type == JSEXN_SUPPRESSEDERR) {
      native = SuppressedError;
      nargs = 3;
    }
#endif
    else {
      native = Error;
      nargs = 1;
    }

    ctor =
        NewFunctionWithProto(cx, native, nargs, FunctionFlags::NATIVE_CTOR,
                             nullptr, ClassName(key, cx), proto,
                             gc::AllocKind::FUNCTION_EXTENDED, TenuredObject);
  }

  if (!ctor) {
    return nullptr;
  }

  ctor->as<JSFunction>().setExtendedSlot(0, Int32Value(type));
  return ctor;
}

/* static */
SharedShape* js::ErrorObject::assignInitialShape(JSContext* cx,
                                                 Handle<ErrorObject*> obj) {
  MOZ_ASSERT(obj->empty());

  constexpr PropertyFlags propFlags = {PropertyFlag::Configurable,
                                       PropertyFlag::Writable};

  if (!NativeObject::addPropertyInReservedSlot(cx, obj, cx->names().fileName,
                                               FILENAME_SLOT, propFlags)) {
    return nullptr;
  }

  if (!NativeObject::addPropertyInReservedSlot(cx, obj, cx->names().lineNumber,
                                               LINENUMBER_SLOT, propFlags)) {
    return nullptr;
  }

  if (!NativeObject::addPropertyInReservedSlot(
          cx, obj, cx->names().columnNumber, COLUMNNUMBER_SLOT, propFlags)) {
    return nullptr;
  }

  return obj->sharedShape();
}

/* static */
bool js::ErrorObject::init(JSContext* cx, Handle<ErrorObject*> obj,
                           JSExnType type, UniquePtr<JSErrorReport> errorReport,
                           HandleString fileName, HandleObject stack,
                           uint32_t sourceId, uint32_t lineNumber,
                           JS::ColumnNumberOneOrigin columnNumber,
                           HandleString message,
                           Handle<mozilla::Maybe<JS::Value>> cause) {
  MOZ_ASSERT(JSEXN_ERR <= type && type < JSEXN_ERROR_LIMIT);
  AssertObjectIsSavedFrameOrWrapper(cx, stack);
  cx->check(obj, stack);

  // Null out early in case of error, for exn_finalize's sake.
  obj->initReservedSlot(ERROR_REPORT_SLOT, PrivateValue(nullptr));

  if (!SharedShape::ensureInitialCustomShape<ErrorObject>(cx, obj)) {
    return false;
  }

  // The .message property isn't part of the initial shape because it's
  // present in some error objects -- |Error.prototype|, |new Error("f")|,
  // |new Error("")| -- but not in others -- |new Error(undefined)|,
  // |new Error()|.
  if (message) {
    constexpr PropertyFlags propFlags = {PropertyFlag::Configurable,
                                         PropertyFlag::Writable};
    if (!NativeObject::addPropertyInReservedSlot(cx, obj, cx->names().message,
                                                 MESSAGE_SLOT, propFlags)) {
      return false;
    }
  }

  // Similar to the .message property, .cause is present only in some error
  // objects -- |new Error("f", {cause: cause})| -- but not in other --
  // |Error.prototype|, |new Error()|, |new Error("f")|.
  if (cause.isSome()) {
    constexpr PropertyFlags propFlags = {PropertyFlag::Configurable,
                                         PropertyFlag::Writable};
    if (!NativeObject::addPropertyInReservedSlot(cx, obj, cx->names().cause,
                                                 CAUSE_SLOT, propFlags)) {
      return false;
    }
  }

  MOZ_ASSERT(obj->lookupPure(NameToId(cx->names().fileName))->slot() ==
             FILENAME_SLOT);
  MOZ_ASSERT(obj->lookupPure(NameToId(cx->names().lineNumber))->slot() ==
             LINENUMBER_SLOT);
  MOZ_ASSERT(obj->lookupPure(NameToId(cx->names().columnNumber))->slot() ==
             COLUMNNUMBER_SLOT);
  MOZ_ASSERT_IF(
      message,
      obj->lookupPure(NameToId(cx->names().message))->slot() == MESSAGE_SLOT);
  MOZ_ASSERT_IF(
      cause.isSome(),
      obj->lookupPure(NameToId(cx->names().cause))->slot() == CAUSE_SLOT);

  JSErrorReport* report = errorReport.release();
  obj->initReservedSlot(STACK_SLOT, ObjectOrNullValue(stack));
  obj->setReservedSlot(ERROR_REPORT_SLOT, PrivateValue(report));
  obj->initReservedSlot(FILENAME_SLOT, StringValue(fileName));
  obj->initReservedSlot(LINENUMBER_SLOT, Int32Value(lineNumber));
  obj->initReservedSlot(COLUMNNUMBER_SLOT,
                        Int32Value(columnNumber.oneOriginValue()));
  if (message) {
    obj->initReservedSlot(MESSAGE_SLOT, StringValue(message));
  }
  if (cause.isSome()) {
    obj->initReservedSlot(CAUSE_SLOT, *cause.get());
  } else {
    obj->initReservedSlot(CAUSE_SLOT, MagicValue(JS_ERROR_WITHOUT_CAUSE));
  }
  obj->initReservedSlot(SOURCEID_SLOT, Int32Value(sourceId));
  if (obj->mightBeWasmTrap()) {
    MOZ_ASSERT(JSCLASS_RESERVED_SLOTS(obj->getClass()) > WASM_TRAP_SLOT);
    obj->initReservedSlot(WASM_TRAP_SLOT, BooleanValue(false));
  }

  return true;
}

/* static */
ErrorObject* js::ErrorObject::create(JSContext* cx, JSExnType errorType,
                                     HandleObject stack, HandleString fileName,
                                     uint32_t sourceId, uint32_t lineNumber,
                                     JS::ColumnNumberOneOrigin columnNumber,
                                     UniquePtr<JSErrorReport> report,
                                     HandleString message,
                                     Handle<mozilla::Maybe<JS::Value>> cause,
                                     HandleObject protoArg /* = nullptr */) {
  AssertObjectIsSavedFrameOrWrapper(cx, stack);

  RootedObject proto(cx, protoArg);
  if (!proto) {
    proto = GlobalObject::getOrCreateCustomErrorPrototype(cx, cx->global(),
                                                          errorType);
    if (!proto) {
      return nullptr;
    }
  }

  Rooted<ErrorObject*> errObject(cx);
  {
    const JSClass* clasp = ErrorObject::classForType(errorType);
    JSObject* obj = NewObjectWithGivenProto(cx, clasp, proto);
    if (!obj) {
      return nullptr;
    }
    errObject = &obj->as<ErrorObject>();
  }

  if (!ErrorObject::init(cx, errObject, errorType, std::move(report), fileName,
                         stack, sourceId, lineNumber, columnNumber, message,
                         cause)) {
    return nullptr;
  }

  return errObject;
}

JSErrorReport* js::ErrorObject::getOrCreateErrorReport(JSContext* cx) {
  if (JSErrorReport* r = getErrorReport()) {
    return r;
  }

  // We build an error report on the stack and then use CopyErrorReport to do
  // the nitty-gritty malloc stuff.
  JSErrorReport report;

  // Type.
  JSExnType type_ = type();
  report.exnType = type_;

  // Filename.
  RootedString filename(cx, fileName(cx));
  UniqueChars filenameStr = JS_EncodeStringToUTF8(cx, filename);
  if (!filenameStr) {
    return nullptr;
  }
  report.filename = JS::ConstUTF8CharsZ(filenameStr.get());

  // Coordinates.
  report.sourceId = sourceId();
  report.lineno = lineNumber();
  report.column = columnNumber();

  // Message. Note that |new Error()| will result in an undefined |message|
  // slot, so we need to explicitly substitute the empty string in that case.
  RootedString message(cx, getMessage());
  if (!message) {
    message = cx->runtime()->emptyString;
  }

  UniqueChars utf8 = StringToNewUTF8CharsZ(cx, *message);
  if (!utf8) {
    return nullptr;
  }
  report.initOwnedMessage(utf8.release());

  // Cache and return.
  UniquePtr<JSErrorReport> copy = CopyErrorReport(cx, &report);
  if (!copy) {
    return nullptr;
  }
  setReservedSlot(ERROR_REPORT_SLOT, PrivateValue(copy.get()));
  return copy.release();
}

static bool FindErrorInstanceOrPrototype(JSContext* cx, HandleObject obj,
                                         MutableHandleObject result) {
  // Walk up the prototype chain until we find an error object instance or
  // prototype object. This allows code like:
  //  Object.create(Error.prototype).stack
  // or
  //   function NYI() { }
  //   NYI.prototype = new Error;
  //   (new NYI).stack
  // to continue returning stacks that are useless, but at least don't throw.

  RootedObject curr(cx, obj);
  RootedObject target(cx);
  do {
    target = CheckedUnwrapStatic(curr);
    if (!target) {
      ReportAccessDenied(cx);
      return false;
    }
    if (IsErrorProtoKey(StandardProtoKeyOrNull(target))) {
      result.set(target);
      return true;
    }

    if (!GetPrototype(cx, curr, &curr)) {
      return false;
    }
  } while (curr);

  // We walked the whole prototype chain and did not find an Error
  // object.
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                            JSMSG_INCOMPATIBLE_PROTO, "Error", "(get stack)",
                            obj->getClass()->name);
  return false;
}

static MOZ_ALWAYS_INLINE bool IsObject(HandleValue v) { return v.isObject(); }

/* static */
bool js::ErrorObject::getStack(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  // We accept any object here, because of poor-man's subclassing of Error.
  return CallNonGenericMethod<IsObject, getStack_impl>(cx, args);
}

/* static */
bool js::ErrorObject::getStack_impl(JSContext* cx, const CallArgs& args) {
  RootedObject thisObj(cx, &args.thisv().toObject());

  RootedObject obj(cx);
  if (!FindErrorInstanceOrPrototype(cx, thisObj, &obj)) {
    return false;
  }

  if (!obj->is<ErrorObject>()) {
    args.rval().setString(cx->runtime()->emptyString);
    return true;
  }

  // Do frame filtering based on the ErrorObject's principals. This ensures we
  // don't see chrome frames when chrome code accesses .stack over Xrays.
  JSPrincipals* principals = obj->as<ErrorObject>().realm()->principals();

  RootedObject savedFrameObj(cx, obj->as<ErrorObject>().stack());
  RootedString stackString(cx);
  if (!BuildStackString(cx, principals, savedFrameObj, &stackString)) {
    return false;
  }

  if (cx->runtime()->stackFormat() == js::StackFormat::V8) {
    // When emulating V8 stack frames, we also need to prepend the
    // stringified Error to the stack string.
    Handle<PropertyName*> name = cx->names().ErrorToStringWithTrailingNewline;
    FixedInvokeArgs<0> args2(cx);
    RootedValue rval(cx);
    if (!CallSelfHostedFunction(cx, name, args.thisv(), args2, &rval)) {
      return false;
    }

    if (!rval.isString()) {
      args.rval().setString(cx->runtime()->emptyString);
      return true;
    }

    RootedString stringified(cx, rval.toString());
    stackString = ConcatStrings<CanGC>(cx, stringified, stackString);
  }

  args.rval().setString(stackString);
  return true;
}

/* static */
bool js::ErrorObject::setStack(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  // We accept any object here, because of poor-man's subclassing of Error.
  return CallNonGenericMethod<IsObject, setStack_impl>(cx, args);
}

/* static */
bool js::ErrorObject::setStack_impl(JSContext* cx, const CallArgs& args) {
  RootedObject thisObj(cx, &args.thisv().toObject());

  if (!args.requireAtLeast(cx, "(set stack)", 1)) {
    return false;
  }

  return DefineDataProperty(cx, thisObj, cx->names().stack, args[0]);
}

void js::ErrorObject::setFromWasmTrap() {
  MOZ_ASSERT(mightBeWasmTrap());
  MOZ_ASSERT(JSCLASS_RESERVED_SLOTS(getClass()) > WASM_TRAP_SLOT);
  setReservedSlot(WASM_TRAP_SLOT, BooleanValue(true));
}

JSString* js::ErrorToSource(JSContext* cx, HandleObject obj) {
  AutoCycleDetector detector(cx, obj);
  if (!detector.init()) {
    return nullptr;
  }
  if (detector.foundCycle()) {
    return NewStringCopyZ<CanGC>(cx, "{}");
  }

  RootedValue nameVal(cx);
  RootedString name(cx);
  if (!GetProperty(cx, obj, obj, cx->names().name, &nameVal) ||
      !(name = ToString<CanGC>(cx, nameVal))) {
    return nullptr;
  }

  RootedValue messageVal(cx);
  RootedString message(cx);
  if (!GetProperty(cx, obj, obj, cx->names().message, &messageVal) ||
      !(message = ValueToSource(cx, messageVal))) {
    return nullptr;
  }

  RootedValue filenameVal(cx);
  RootedString filename(cx);
  if (!GetProperty(cx, obj, obj, cx->names().fileName, &filenameVal) ||
      !(filename = ValueToSource(cx, filenameVal))) {
    return nullptr;
  }

  RootedValue errorsVal(cx);
  RootedString errors(cx);
  bool isAggregateError = obj->is<ErrorObject>() &&
                          obj->as<ErrorObject>().type() == JSEXN_AGGREGATEERR;
  if (isAggregateError) {
    if (!GetProperty(cx, obj, obj, cx->names().errors, &errorsVal) ||
        !(errors = ValueToSource(cx, errorsVal))) {
      return nullptr;
    }
  }

  RootedValue linenoVal(cx);
  uint32_t lineno;
  if (!GetProperty(cx, obj, obj, cx->names().lineNumber, &linenoVal) ||
      !ToUint32(cx, linenoVal, &lineno)) {
    return nullptr;
  }

  JSStringBuilder sb(cx);
  if (!sb.append("(new ") || !sb.append(name) || !sb.append("(")) {
    return nullptr;
  }

  if (isAggregateError) {
    if (!sb.append(errors) || !sb.append(", ")) {
      return nullptr;
    }
  }

  if (!sb.append(message)) {
    return nullptr;
  }

  if (!filename->empty()) {
    if (!sb.append(", ") || !sb.append(filename)) {
      return nullptr;
    }
  }
  if (lineno != 0) {
    /* We have a line, but no filename, add empty string */
    if (filename->empty() && !sb.append(", \"\"")) {
      return nullptr;
    }

    JSString* linenumber = ToString<CanGC>(cx, linenoVal);
    if (!linenumber) {
      return nullptr;
    }
    if (!sb.append(", ") || !sb.append(linenumber)) {
      return nullptr;
    }
  }

  if (!sb.append("))")) {
    return nullptr;
  }

  return sb.finishString();
}

/*
 * Return a string that may eval to something similar to the original object.
 */
static bool exn_toSource(JSContext* cx, unsigned argc, Value* vp) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedObject obj(cx, ToObject(cx, args.thisv()));
  if (!obj) {
    return false;
  }

  JSString* str = ErrorToSource(cx, obj);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

/**
 * Error.isError Proposal
 * Error.isError ( arg )
 * https://tc39.es/proposal-is-error/#sec-error.iserror
 * IsError ( argument )
 * https://tc39.es/proposal-is-error/#sec-iserror
 */
static bool exn_isError(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Error.isError ( arg )
  // Step 1. Return IsError(arg).

  // IsError ( argument )
  // Step 1. If argument is not an Object, return false.
  if (!args.get(0).isObject()) {
    args.rval().setBoolean(false);
    return true;
  }

  JSObject* unwrappedObject = CheckedUnwrapStatic(&args.get(0).toObject());
  if (!unwrappedObject) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_OBJECT_ACCESS_DENIED);
    return false;
  }

  if (JS_IsDeadWrapper(unwrappedObject)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_DEAD_OBJECT);
    return false;
  }

  // Step 2. If argument has an [[ErrorData]] internal slot, return true.
  if (unwrappedObject->is<ErrorObject>()) {
    args.rval().setBoolean(true);
    return true;
  }
  if (unwrappedObject->getClass()->isDOMClass()) {
    args.rval().setBoolean(cx->runtime()->DOMcallbacks->instanceClassIsError(
        unwrappedObject->getClass()));
    return true;
  }

  // Step 3. Return false
  args.rval().setBoolean(false);
  return true;
}

// The below is the "documentation" from https://v8.dev/docs/stack-trace-api
//
// Setting it to 0 disables stack trace collection. Any finite integer value
// can be used as the maximum number of frames to collect. Setting it to
// Infinity means that all frames get collected. This variable only affects
// the current context; it has to be set explicitly for each context that
// needs a different value.
//
// Undocumented, but setting it to `undefined` will cause the `stack`
// property to also be `undefined`. In this case, we return Nothing.
mozilla::Maybe<uint32_t> js::GetStackTraceLimit(JSContext* cx) {
  if (!JS::Prefs::experimental_error_stack_trace_limit()) {
    return mozilla::Some(uint32_t(MAX_REPORTED_STACK_DEPTH));
  }
  JSObject* errorCtor = cx->global()->maybeGetConstructor(JSProto_Error);
  if (!errorCtor) {
    return mozilla::Some(uint32_t(MAX_REPORTED_STACK_DEPTH));
  }
  Value limitVal;
  if (!GetPropertyPure(cx, errorCtor, NameToId(cx->names().stackTraceLimit),
                       &limitVal)) {
    return mozilla::Some(uint32_t(MAX_REPORTED_STACK_DEPTH));
  }
  if (limitVal.isUndefined()) {
    return mozilla::Nothing();
  }
  if (!limitVal.isNumber()) {
    return mozilla::Some(uint32_t(0));
  }
  double d = limitVal.toNumber();
  if (std::isnan(d) || d < 0) {
    return mozilla::Some(uint32_t(0));
  }
  return mozilla::Some(uint32_t(std::min(d, double(MAX_REPORTED_STACK_DEPTH))));
}

// The below is the "documentation" from https://v8.dev/docs/stack-trace-api
//
//  ## Stack trace collection for custom exceptions
//
//  The stack trace mechanism used for built-in errors is implemented using a
//  general stack trace collection API that is also available to user scripts.
//  The function
//
//   Error.captureStackTrace(error, constructorOpt)
//
//  adds a stack property to the given error object that yields the stack trace
//  at the time captureStackTrace was called. Stack traces collected through
//  Error.captureStackTrace are immediately collected, formatted, and attached
//  to the given error object.
//
//  The optional constructorOpt parameter allows you to pass in a function
//  value. When collecting the stack trace all frames above the topmost call to
//  this function, including that call, are left out of the stack trace. This
//  can be useful to hide implementation details that won’t be useful to the
//  user. The usual way of defining a custom error that captures a stack trace
//  would be:
//
//   function MyError() {
//     Error.captureStackTrace(this, MyError);
//     // Any other initialization goes here.
//   }
//
//  Passing in MyError as a second argument means that the constructor call to
//  MyError won’t show up in the stack trace.

static bool exn_captureStackTrace(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  const char* callerName = "Error.captureStackTrace";

  if (!args.requireAtLeast(cx, callerName, 1)) {
    return false;
  }

  Rooted<JSObject*> obj(cx,
                        RequireObjectArg(cx, "`target`", callerName, args[0]));
  if (!obj) {
    return false;
  }

  Rooted<JSObject*> caller(cx, nullptr);
  if (args.length() > 1 && args[1].isObject() &&
      args[1].toObject().isCallable()) {
    caller = CheckedUnwrapStatic(&args[1].toObject());
    if (!caller) {
      ReportAccessDenied(cx);
      return false;
    }
  }

  mozilla::Maybe<uint32_t> limit = GetStackTraceLimit(cx);
  RootedValue stackVal(cx, UndefinedValue());
  if (limit.isSome()) {
    RootedObject stack(cx);
    if (*limit > 0) {
      if (!CaptureCurrentStack(
              cx, &stack, JS::StackCapture(JS::MaxFrames(*limit)), caller)) {
        return false;
      }
    }

    RootedString stackString(cx);

    // Do frame filtering based on the current realm, to filter out any
    // chrome frames which could exist on the stack.
    JSPrincipals* principals = cx->realm()->principals();
    if (!BuildStackString(cx, principals, stack, &stackString)) {
      return false;
    }
    stackVal.setString(stackString);
  }

  // V8 installs a non-enumerable, configurable getter-setter on the object.
  // JSC installs a non-enumerable, configurable, writable value on the
  // object. We are following JSC here, not V8.
  if (!DefineDataProperty(cx, obj, cx->names().stack, stackVal, 0)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

size_t ExtraMallocSize(JSErrorReport* report) {
  if (report->linebuf()) {
    /*
     * Count with null terminator and alignment.
     * See CopyExtraData for the details about alignment.
     */
    return (report->linebufLength() + 1) * sizeof(char16_t) + 1;
  }

  return 0;
}

size_t ExtraMallocSize(JSErrorNotes::Note* note) { return 0; }

bool CopyExtraData(JSContext* cx, uint8_t** cursor, JSErrorReport* copy,
                   JSErrorReport* report) {
  if (report->linebuf()) {
    /*
     * Make sure cursor is properly aligned for char16_t for platforms
     * which need it and it's at the end of the buffer on exit.
     */
    size_t alignment_backlog = 0;
    if (size_t(*cursor) % 2) {
      (*cursor)++;
    } else {
      alignment_backlog = 1;
    }

    size_t linebufSize = (report->linebufLength() + 1) * sizeof(char16_t);
    const char16_t* linebufCopy = (const char16_t*)(*cursor);
    js_memcpy(*cursor, report->linebuf(), linebufSize);
    *cursor += linebufSize + alignment_backlog;
    copy->initBorrowedLinebuf(linebufCopy, report->linebufLength(),
                              report->tokenOffset());
  }

  /* Copy non-pointer members. */
  copy->isMuted = report->isMuted;
  copy->exnType = report->exnType;
  copy->isWarning_ = report->isWarning_;

  /* Deep copy notes. */
  if (report->notes) {
    auto copiedNotes = report->notes->copy(cx);
    if (!copiedNotes) {
      return false;
    }
    copy->notes = std::move(copiedNotes);
  } else {
    copy->notes.reset(nullptr);
  }

  return true;
}

bool CopyExtraData(JSContext* cx, uint8_t** cursor, JSErrorNotes::Note* copy,
                   JSErrorNotes::Note* report) {
  return true;
}

template <typename T>
static UniquePtr<T> CopyErrorHelper(JSContext* cx, T* report) {
  /*
   * We use a single malloc block to make a deep copy of JSErrorReport or
   * JSErrorNotes::Note, except JSErrorNotes linked from JSErrorReport with
   * the following layout:
   *   JSErrorReport or JSErrorNotes::Note
   *   char array with characters for message_
   *   char array with characters for filename
   *   char16_t array with characters for linebuf (only for JSErrorReport)
   * Such layout together with the properties enforced by the following
   * asserts does not need any extra alignment padding.
   */
  static_assert(sizeof(T) % sizeof(const char*) == 0);
  static_assert(sizeof(const char*) % sizeof(char16_t) == 0);

  size_t filenameSize =
      report->filename ? strlen(report->filename.c_str()) + 1 : 0;
  size_t messageSize = 0;
  if (report->message()) {
    messageSize = strlen(report->message().c_str()) + 1;
  }

  /*
   * The mallocSize can not overflow since it represents the sum of the
   * sizes of already allocated objects.
   */
  size_t mallocSize =
      sizeof(T) + messageSize + filenameSize + ExtraMallocSize(report);
  uint8_t* cursor = cx->pod_calloc<uint8_t>(mallocSize);
  if (!cursor) {
    return nullptr;
  }

  UniquePtr<T> copy(new (cursor) T());
  cursor += sizeof(T);

  if (report->message()) {
    copy->initBorrowedMessage((const char*)cursor);
    js_memcpy(cursor, report->message().c_str(), messageSize);
    cursor += messageSize;
  }

  if (report->filename) {
    copy->filename = JS::ConstUTF8CharsZ((const char*)cursor);
    js_memcpy(cursor, report->filename.c_str(), filenameSize);
    cursor += filenameSize;
  }

  if (!CopyExtraData(cx, &cursor, copy.get(), report)) {
    return nullptr;
  }

  MOZ_ASSERT(cursor == (uint8_t*)copy.get() + mallocSize);

  // errorMessageName should be static.
  copy->errorMessageName = report->errorMessageName;

  /* Copy non-pointer members. */
  copy->sourceId = report->sourceId;
  copy->lineno = report->lineno;
  copy->column = report->column;
  copy->errorNumber = report->errorNumber;

  return copy;
}

UniquePtr<JSErrorNotes::Note> js::CopyErrorNote(JSContext* cx,
                                                JSErrorNotes::Note* note) {
  return CopyErrorHelper(cx, note);
}

UniquePtr<JSErrorReport> js::CopyErrorReport(JSContext* cx,
                                             JSErrorReport* report) {
  return CopyErrorHelper(cx, report);
}

struct SuppressErrorsGuard {
  JSContext* cx;
  JS::WarningReporter prevReporter;
  JS::AutoSaveExceptionState prevState;

  explicit SuppressErrorsGuard(JSContext* cx)
      : cx(cx),
        prevReporter(JS::SetWarningReporter(cx, nullptr)),
        prevState(cx) {}

  ~SuppressErrorsGuard() { JS::SetWarningReporter(cx, prevReporter); }
};

bool js::CaptureStack(JSContext* cx, MutableHandleObject stack,
                      uint32_t limit) {
  if (limit == 0) {
    return true;
  }
  return CaptureCurrentStack(cx, stack, JS::StackCapture(JS::MaxFrames(limit)));
}

JSString* js::ComputeStackString(JSContext* cx) {
  SuppressErrorsGuard seg(cx);

  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack, MAX_REPORTED_STACK_DEPTH)) {
    return nullptr;
  }

  RootedString str(cx);
  if (!BuildStackString(cx, cx->realm()->principals(), stack, &str)) {
    return nullptr;
  }

  return str.get();
}

bool js::ErrorFromException(JSContext* cx, HandleObject objArg,
                            JS::BorrowedErrorReport& errorReport) {
  // It's ok to UncheckedUnwrap here, since all we do is get the
  // JSErrorReport, and consumers are careful with the information they get
  // from that anyway.  Anyone doing things that would expose anything in the
  // JSErrorReport to page script either does a security check on the
  // JSErrorReport's principal or also tries to do toString on our object and
  // will fail if they can't unwrap it.
  RootedObject obj(cx, UncheckedUnwrap(objArg));
  if (!obj->is<ErrorObject>()) {
    return false;
  }

  JSErrorReport* report = obj->as<ErrorObject>().getOrCreateErrorReport(cx);
  if (!report) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    cx->recoverFromOutOfMemory();
    return false;
  }

  // Note: it's important to use the unwrapped object here. CCWs can be cut when
  // nuking wrappers so they're not guaranteed to keep the target object and its
  // JSErrorReport alive.
  errorReport.init(obj, report);
  return true;
}

JS_PUBLIC_API JSObject* JS::ExceptionStackOrNull(HandleObject objArg) {
  ErrorObject* errorObject = objArg->maybeUnwrapIf<ErrorObject>();
  if (errorObject) {
    return errorObject->stack();
  }

  WasmExceptionObject* wasmObject =
      objArg->maybeUnwrapIf<WasmExceptionObject>();
  if (wasmObject) {
    return wasmObject->stack();
  }

  return nullptr;
}

JS_PUBLIC_API JSLinearString* js::GetErrorTypeName(JSContext* cx,
                                                   int16_t exnType) {
  /*
   * JSEXN_INTERNALERR returns null to prevent that "InternalError: "
   * is prepended before "uncaught exception: "
   */
  if (exnType < 0 || exnType >= JSEXN_LIMIT || exnType == JSEXN_INTERNALERR ||
      exnType == JSEXN_WARN || exnType == JSEXN_NOTE) {
    return nullptr;
  }
  JSProtoKey key = GetExceptionProtoKey(JSExnType(exnType));
  return ClassName(key, cx);
}

bool js::ErrorToException(JSContext* cx, JSErrorReport* reportp,
                          JSErrorCallback callback, void* userRef) {
  MOZ_ASSERT(!reportp->isWarning());

  // Find the exception index associated with this error.
  JSErrNum errorNumber = static_cast<JSErrNum>(reportp->errorNumber);
  if (!callback) {
    callback = GetErrorMessage;
  }
  const JSErrorFormatString* errorString = callback(userRef, errorNumber);
  JSExnType exnType =
      errorString ? static_cast<JSExnType>(errorString->exnType) : JSEXN_ERR;
  MOZ_ASSERT(exnType < JSEXN_ERROR_LIMIT);

  // Prevent infinite recursion.
  if (cx->generatingError) {
    return false;
  }

  cx->generatingError = true;
  auto restore = mozilla::MakeScopeExit([cx] { cx->generatingError = false; });

  // Create an exception object.
  RootedString messageStr(cx, reportp->newMessageString(cx));
  if (!messageStr) {
    return false;
  }

  Rooted<JSString*> fileName(cx);
  if (const char* filename = reportp->filename.c_str()) {
    fileName =
        JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(filename, strlen(filename)));
    if (!fileName) {
      return false;
    }
  } else {
    fileName = cx->emptyString();
  }

  uint32_t sourceId = reportp->sourceId;
  uint32_t lineNumber = reportp->lineno;
  JS::ColumnNumberOneOrigin columnNumber = reportp->column;

  // Error reports don't provide a |cause|, so we default to |Nothing| here.
  auto cause = JS::NothingHandleValue;

  mozilla::Maybe<uint32_t> limit = GetStackTraceLimit(cx);
  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack, limit.valueOr(0))) {
    return false;
  }

  UniquePtr<JSErrorReport> report = CopyErrorReport(cx, reportp);
  if (!report) {
    return false;
  }

  Rooted<ErrorObject*> errObject(
      cx,
      ErrorObject::create(cx, exnType, stack, fileName, sourceId, lineNumber,
                          columnNumber, std::move(report), messageStr, cause));
  if (!errObject) {
    return false;
  }
  if (limit.isNothing()) {
    if (!DefineDataProperty(cx, errObject, cx->names().stack,
                            JS::UndefinedHandleValue)) {
      return false;
    }
  }

  // Throw it.
  RootedValue errValue(cx, ObjectValue(*errObject));
  Rooted<SavedFrame*> nstack(cx);
  if (stack) {
    nstack = &stack->as<SavedFrame>();
  }
  cx->setPendingException(errValue, nstack);
  return true;
}

using SniffingBehavior = JS::ErrorReportBuilder::SniffingBehavior;

static bool IsDuckTypedErrorObject(JSContext* cx, HandleObject exnObject,
                                   const char** filename_strp) {
  /*
   * This function is called from ErrorReport::init and so should not generate
   * any new exceptions.
   */
  AutoClearPendingException acpe(cx);

  bool found;
  if (!JS_HasProperty(cx, exnObject, "message", &found) || !found) {
    return false;
  }

  // First try "filename".
  const char* filename_str = *filename_strp;
  if (!JS_HasProperty(cx, exnObject, filename_str, &found)) {
    return false;
  }
  if (!found) {
    // If that doesn't work, try "fileName".
    filename_str = "fileName";
    if (!JS_HasProperty(cx, exnObject, filename_str, &found) || !found) {
      return false;
    }
  }

  if (!JS_HasProperty(cx, exnObject, "lineNumber", &found) || !found) {
    return false;
  }

  *filename_strp = filename_str;
  return true;
}

static bool GetPropertyNoException(JSContext* cx, HandleObject obj,
                                   SniffingBehavior behavior,
                                   Handle<PropertyName*> name,
                                   MutableHandleValue vp) {
  // This function has no side-effects so always use it.
  if (GetPropertyPure(cx, obj, NameToId(name), vp.address())) {
    return true;
  }

  if (behavior == SniffingBehavior::WithSideEffects) {
    AutoClearPendingException acpe(cx);
    return GetProperty(cx, obj, obj, name, vp);
  }

  return false;
}

// Create a new error message similar to what Error.prototype.toString would
// produce when called on an object with those property values for name and
// message.
static JSString* FormatErrorMessage(JSContext* cx, HandleString name,
                                    HandleString message) {
  if (name && message) {
    AutoClearPendingException acpe(cx);
    JSStringBuilder sb(cx);

    // Prefix the message with the error type, if it exists.
    if (!sb.append(name) || !sb.append(": ") || !sb.append(message)) {
      return nullptr;
    }

    return sb.finishString();
  }

  return name ? name : message;
}

static JSString* ErrorReportToString(JSContext* cx, HandleObject exn,
                                     JSErrorReport* reportp,
                                     SniffingBehavior behavior) {
  // The error object might have custom `name` overwriting the exnType in the
  // error report. Try getting that property and use the exnType as a fallback.
  RootedString name(cx);
  RootedValue nameV(cx);
  if (GetPropertyNoException(cx, exn, behavior, cx->names().name, &nameV) &&
      nameV.isString()) {
    name = nameV.toString();
  }

  // We do NOT want to use GetErrorTypeName() here because it will not do the
  // "right thing" for JSEXN_INTERNALERR.  That is, the caller of this API
  // expects that "InternalError: " will be prepended but GetErrorTypeName
  // goes out of its way to avoid this.
  if (!name) {
    JSExnType type = static_cast<JSExnType>(reportp->exnType);
    if (type != JSEXN_WARN && type != JSEXN_NOTE) {
      name = ClassName(GetExceptionProtoKey(type), cx);
    }
  }

  RootedString message(cx);
  RootedValue messageV(cx);
  if (GetPropertyNoException(cx, exn, behavior, cx->names().message,
                             &messageV) &&
      messageV.isString()) {
    message = messageV.toString();
  }

  if (!message) {
    message = reportp->newMessageString(cx);
    if (!message) {
      return nullptr;
    }
  }

  return FormatErrorMessage(cx, name, message);
}

JS::ErrorReportBuilder::ErrorReportBuilder(JSContext* cx)
    : reportp(nullptr), borrowedReport(cx) {}

JS::ErrorReportBuilder::~ErrorReportBuilder() = default;

// (DOM)Exception objects are kind of like error objects, and they actually
// have an Error.prototype, but they aren't really JS error objects.
// They also don't have their own JSErrorReport*.
// To improve the error reporting for DOMExceptions and make them look more
// like JS errors, we create a fake JSErrorReport for them.
JSString* JS::ErrorReportBuilder::maybeCreateReportFromDOMException(
    JS::HandleObject obj, JSContext* cx) {
  if (!obj->getClass()->isDOMClass()) {
    return nullptr;
  }

  bool isException;
  Rooted<JSString*> fileNameStr(cx), messageStr(cx);
  uint32_t lineno, column;
  if (!cx->runtime()->DOMcallbacks->extractExceptionInfo(
          cx, obj, &isException, &fileNameStr, &lineno, &column, &messageStr)) {
    cx->clearPendingException();
    return nullptr;
  }

  if (!isException) {
    return nullptr;
  }

  filename = JS_EncodeStringToUTF8(cx, fileNameStr);
  if (!filename) {
    cx->clearPendingException();
    return nullptr;
  }

  JS::UniqueChars messageUtf8 = JS_EncodeStringToUTF8(cx, messageStr);
  if (!messageUtf8) {
    cx->clearPendingException();
    return nullptr;
  }

  reportp = &ownedReport;
  new (reportp) JSErrorReport();
  ownedReport.filename = JS::ConstUTF8CharsZ(filename.get());
  ownedReport.lineno = lineno;
  ownedReport.exnType = JSEXN_INTERNALERR;
  ownedReport.column = JS::ColumnNumberOneOrigin(column);
  // Note that using |messageStr| for |message_| here is kind of wrong,
  // because |messageStr| is of the format
  // |ErrorName: ErrorMessage|, and |message_| is supposed to
  // correspond to |ErrorMessage|. But this is what we've
  // historically done for duck-typed error objects.
  //
  // If only this stuff could get specced one day...
  ownedReport.initOwnedMessage(messageUtf8.release());

  return messageStr;
}

bool JS::ErrorReportBuilder::init(JSContext* cx,
                                  const JS::ExceptionStack& exnStack,
                                  SniffingBehavior sniffingBehavior) {
  MOZ_ASSERT(!cx->isExceptionPending());
  MOZ_ASSERT(!reportp);

  Rooted<JSObject*> exnObject(cx);

  if (exnStack.exception().isObject()) {
    // Because ToString below could error and an exception object could become
    // unrooted, we must root our exception object, if any.
    exnObject = &exnStack.exception().toObject();

    if (ErrorFromException(cx, exnObject, borrowedReport)) {
      reportp = borrowedReport.get();
      if (reportp->isMuted) {
        sniffingBehavior = SniffingBehavior::NoSideEffects;
      }
    } else {
      reportp = nullptr;
    }
  }

  // Be careful not to invoke ToString if we've already successfully extracted
  // an error report, since the exception might be wrapped in a security
  // wrapper, and ToString-ing it might throw.
  RootedString str(cx);
  if (reportp) {
    str = ErrorReportToString(cx, exnObject, reportp, sniffingBehavior);
  } else if (exnObject &&
             (str = maybeCreateReportFromDOMException(exnObject, cx))) {
    MOZ_ASSERT(reportp, "Should have initialized report");
  } else if (exnStack.exception().isSymbol()) {
    RootedValue strVal(cx);
    if (js::SymbolDescriptiveString(cx, exnStack.exception().toSymbol(),
                                    &strVal)) {
      str = strVal.toString();
    } else {
      str = nullptr;
    }
  } else if (exnObject && sniffingBehavior == NoSideEffects) {
    str = cx->names().Object;
  } else {
    str = js::ToString<CanGC>(cx, exnStack.exception());
  }

  if (!str) {
    cx->clearPendingException();
  }

  // If ErrorFromException didn't get us a JSErrorReport, then the object
  // was not an ErrorObject, security-wrapped or otherwise. However, it might
  // still quack like one. Give duck-typing a chance.  We start by looking for
  // "filename" (all lowercase), since that's where DOMExceptions store their
  // filename.  Then we check "fileName", which is where Errors store it.  We
  // have to do it in that order, because DOMExceptions have Error.prototype
  // on their proto chain, and hence also have a "fileName" property, but its
  // value is "".
  //
  // WARNING: This is disabled by default and planned to be removed completely.
  const char* filename_str = "filename";
  if (JS::Prefs::ducktyped_errors() && !reportp && exnObject &&
      sniffingBehavior == WithSideEffects &&
      IsDuckTypedErrorObject(cx, exnObject, &filename_str)) {
    // Temporary value for pulling properties off of duck-typed objects.
    RootedValue val(cx);

    RootedString name(cx);
    if (JS_GetProperty(cx, exnObject, "name", &val) && val.isString()) {
      name = val.toString();
    } else {
      cx->clearPendingException();
    }

    RootedString msg(cx);
    if (JS_GetProperty(cx, exnObject, "message", &val) && val.isString()) {
      msg = val.toString();
    } else {
      cx->clearPendingException();
    }

    // If we have the right fields, override the ToString we performed on
    // the exception object above with something built out of its quacks
    // (i.e. as much of |NameQuack: MessageQuack| as we can make).
    str = FormatErrorMessage(cx, name, msg);

    {
      AutoClearPendingException acpe(cx);
      if (JS_GetProperty(cx, exnObject, filename_str, &val)) {
        RootedString tmp(cx, js::ToString<CanGC>(cx, val));
        if (tmp) {
          filename = JS_EncodeStringToUTF8(cx, tmp);
        }
      }
    }
    if (!filename) {
      filename = DuplicateString("");
      if (!filename) {
        ReportOutOfMemory(cx);
        return false;
      }
    }

    uint32_t lineno;
    if (!JS_GetProperty(cx, exnObject, "lineNumber", &val) ||
        !ToUint32(cx, val, &lineno)) {
      cx->clearPendingException();
      lineno = 0;
    }

    uint32_t column;
    if (!JS_GetProperty(cx, exnObject, "columnNumber", &val) ||
        !ToUint32(cx, val, &column)) {
      cx->clearPendingException();
      column = 0;
    }

    reportp = &ownedReport;
    new (reportp) JSErrorReport();
    ownedReport.filename = JS::ConstUTF8CharsZ(filename.get());
    ownedReport.lineno = lineno;
    ownedReport.exnType = JSEXN_INTERNALERR;
    ownedReport.column = JS::ColumnNumberOneOrigin(column);

    if (str) {
      // Note that using |str| for |message_| here is kind of wrong,
      // because |str| is supposed to be of the format
      // |ErrorName: ErrorMessage|, and |message_| is supposed to
      // correspond to |ErrorMessage|. But this is what we've
      // historically done for duck-typed error objects.
      //
      // If only this stuff could get specced one day...
      if (auto utf8 = JS_EncodeStringToUTF8(cx, str)) {
        ownedReport.initOwnedMessage(utf8.release());
      } else {
        cx->clearPendingException();
        str = nullptr;
      }
    }
  }

  const char* utf8Message = nullptr;
  if (str) {
    toStringResultBytesStorage = JS_EncodeStringToUTF8(cx, str);
    utf8Message = toStringResultBytesStorage.get();
    if (!utf8Message) {
      cx->clearPendingException();
    }
  }
  if (!utf8Message) {
    utf8Message = "unknown (can't convert to string)";
  }

  if (!reportp) {
    // This is basically an inlined version of
    //
    //   JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
    //                            JSMSG_UNCAUGHT_EXCEPTION, utf8Message);
    //
    // but without the reporting bits.  Instead it just puts all
    // the stuff we care about in our ownedReport and message_.
    if (!populateUncaughtExceptionReportUTF8(cx, exnStack.stack(),
                                             utf8Message)) {
      // Just give up.  We're out of memory or something; not much we can
      // do here.
      return false;
    }
  } else {
    toStringResult_ = JS::ConstUTF8CharsZ(utf8Message, strlen(utf8Message));
  }

  return true;
}

bool JS::ErrorReportBuilder::populateUncaughtExceptionReportUTF8(
    JSContext* cx, HandleObject stack, ...) {
  va_list ap;
  va_start(ap, stack);
  bool ok = populateUncaughtExceptionReportUTF8VA(cx, stack, ap);
  va_end(ap);
  return ok;
}

bool JS::ErrorReportBuilder::populateUncaughtExceptionReportUTF8VA(
    JSContext* cx, HandleObject stack, va_list ap) {
  new (&ownedReport) JSErrorReport();
  ownedReport.isWarning_ = false;
  ownedReport.errorNumber = JSMSG_UNCAUGHT_EXCEPTION;

  bool skippedAsync;
  Rooted<SavedFrame*> frame(
      cx, UnwrapSavedFrame(cx, cx->realm()->principals(), stack,
                           JS::SavedFrameSelfHosted::Exclude, skippedAsync));
  if (frame) {
    filename = StringToNewUTF8CharsZ(cx, *frame->getSource());
    if (!filename) {
      return false;
    }

    // |ownedReport.filename| inherits the lifetime of |ErrorReport::filename|.
    ownedReport.filename = JS::ConstUTF8CharsZ(filename.get());
    ownedReport.sourceId = frame->getSourceId();
    ownedReport.lineno = frame->getLine();
    ownedReport.column =
        JS::ColumnNumberOneOrigin(frame->getColumn().oneOriginValue());
    ownedReport.isMuted = frame->getMutedErrors();
  } else {
    // XXXbz this assumes the stack we have right now is still
    // related to our exception object.
    NonBuiltinFrameIter iter(cx, cx->realm()->principals());
    if (!iter.done()) {
      ownedReport.filename = JS::ConstUTF8CharsZ(iter.filename());
      JS::TaggedColumnNumberOneOrigin column;
      ownedReport.sourceId =
          iter.hasScript() ? iter.script()->scriptSource()->id() : 0;
      ownedReport.lineno = iter.computeLine(&column);
      ownedReport.column = JS::ColumnNumberOneOrigin(column.oneOriginValue());
      ownedReport.isMuted = iter.mutedErrors();
    }
  }

  AutoReportFrontendContext fc(cx);
  if (!ExpandErrorArgumentsVA(&fc, GetErrorMessage, nullptr,
                              JSMSG_UNCAUGHT_EXCEPTION, ArgumentsAreUTF8,
                              &ownedReport, ap)) {
    return false;
  }

  toStringResult_ = ownedReport.message();
  reportp = &ownedReport;
  return true;
}

JSObject* js::CopyErrorObject(JSContext* cx, Handle<ErrorObject*> err) {
  UniquePtr<JSErrorReport> copyReport;
  if (JSErrorReport* errorReport = err->getErrorReport()) {
    copyReport = CopyErrorReport(cx, errorReport);
    if (!copyReport) {
      return nullptr;
    }
  }

  RootedString message(cx, err->getMessage());
  if (message && !cx->compartment()->wrap(cx, &message)) {
    return nullptr;
  }
  RootedString fileName(cx, err->fileName(cx));
  if (!cx->compartment()->wrap(cx, &fileName)) {
    return nullptr;
  }
  RootedObject stack(cx, err->stack());
  if (!cx->compartment()->wrap(cx, &stack)) {
    return nullptr;
  }
  if (stack && JS_IsDeadWrapper(stack)) {
    // ErrorObject::create expects |stack| to be either nullptr or a (possibly
    // wrapped) SavedFrame instance.
    stack = nullptr;
  }
  Rooted<mozilla::Maybe<Value>> cause(cx, mozilla::Nothing());
  if (auto maybeCause = err->getCause()) {
    RootedValue errorCause(cx, maybeCause.value());
    if (!cx->compartment()->wrap(cx, &errorCause)) {
      return nullptr;
    }
    cause = mozilla::Some(errorCause.get());
  }
  uint32_t sourceId = err->sourceId();
  uint32_t lineNumber = err->lineNumber();
  JS::ColumnNumberOneOrigin columnNumber = err->columnNumber();
  JSExnType errorType = err->type();

  // Create the Error object.
  Rooted<ErrorObject*> copy(
      cx,
      ErrorObject::create(cx, errorType, stack, fileName, sourceId, lineNumber,
                          columnNumber, std::move(copyReport), message, cause));
  if (!copy) {
    return nullptr;
  }

  // Preserve the Wasm trap flag so that a copied trap remains uncatchable by
  // Wasm exception handling (catch_all).
  if (err->mightBeWasmTrap() && err->fromWasmTrap()) {
    copy->setFromWasmTrap();
  }

  return copy;
}

JS_PUBLIC_API bool JS::CreateError(JSContext* cx, JSExnType type,
                                   HandleObject stack, HandleString fileName,
                                   uint32_t lineNumber,
                                   JS::ColumnNumberOneOrigin columnNumber,
                                   JSErrorReport* report, HandleString message,
                                   Handle<mozilla::Maybe<Value>> cause,
                                   MutableHandleValue rval) {
  cx->check(stack, fileName, message);
  AssertObjectIsSavedFrameOrWrapper(cx, stack);

  js::UniquePtr<JSErrorReport> rep;
  if (report) {
    rep = CopyErrorReport(cx, report);
    if (!rep) {
      return false;
    }
  }

  JSObject* obj =
      js::ErrorObject::create(cx, type, stack, fileName, 0, lineNumber,
                              columnNumber, std::move(rep), message, cause);
  if (!obj) {
    return false;
  }

  rval.setObject(*obj);
  return true;
}

const char* js::ValueToSourceForError(JSContext* cx, HandleValue val,
                                      UniqueChars& bytes) {
  if (val.isUndefined()) {
    return "undefined";
  }

  if (val.isNull()) {
    return "null";
  }

  AutoClearPendingException acpe(cx);

  // This function must always return a non-null string. If the conversion to
  // string fails due to OOM, we return this string instead.
  static constexpr char ErrorConvertingToStringMsg[] =
      "<<error converting value to string>>";

  RootedString str(cx, JS_ValueToSource(cx, val));
  if (!str) {
    return ErrorConvertingToStringMsg;
  }

  JSStringBuilder sb(cx);
  if (val.isObject()) {
    RootedObject valObj(cx, &val.toObject());
    ESClass cls;
    if (!JS::GetBuiltinClass(cx, valObj, &cls)) {
      return "<<error determining class of value>>";
    }
    const char* s;
    if (cls == ESClass::Array) {
      s = "the array ";
    } else if (cls == ESClass::ArrayBuffer) {
      s = "the array buffer ";
    } else if (JS_IsArrayBufferViewObject(valObj)) {
      s = "the typed array ";
    } else {
      s = "the object ";
    }
    if (!sb.append(s, strlen(s))) {
      return ErrorConvertingToStringMsg;
    }
  } else if (val.isNumber()) {
    if (!sb.append("the number ")) {
      return ErrorConvertingToStringMsg;
    }
  } else if (val.isString()) {
    if (!sb.append("the string ")) {
      return ErrorConvertingToStringMsg;
    }
  } else if (val.isBigInt()) {
    if (!sb.append("the BigInt ")) {
      return ErrorConvertingToStringMsg;
    }
  } else {
    MOZ_ASSERT(val.isBoolean() || val.isSymbol());
    bytes = StringToNewUTF8CharsZ(cx, *str);
    if (!bytes) {
      return ErrorConvertingToStringMsg;
    }
    return bytes.get();
  }
  if (!sb.append(str)) {
    return ErrorConvertingToStringMsg;
  }
  str = sb.finishString();
  if (!str) {
    return ErrorConvertingToStringMsg;
  }
  bytes = StringToNewUTF8CharsZ(cx, *str);
  if (!bytes) {
    return ErrorConvertingToStringMsg;
  }
  return bytes.get();
}

bool js::GetInternalError(JSContext* cx, unsigned errorNumber,
                          MutableHandleValue error) {
  FixedInvokeArgs<1> args(cx);
  args[0].set(Int32Value(errorNumber));
  return CallSelfHostedFunction(cx, cx->names().GetInternalError,
                                NullHandleValue, args, error);
}

bool js::GetTypeError(JSContext* cx, unsigned errorNumber,
                      MutableHandleValue error) {
  FixedInvokeArgs<1> args(cx);
  args[0].set(Int32Value(errorNumber));
  return CallSelfHostedFunction(cx, cx->names().GetTypeError, NullHandleValue,
                                args, error);
}

bool js::GetAggregateError(JSContext* cx, unsigned errorNumber,
                           MutableHandleValue error) {
  FixedInvokeArgs<1> args(cx);
  args[0].set(Int32Value(errorNumber));
  return CallSelfHostedFunction(cx, cx->names().GetAggregateError,
                                NullHandleValue, args, error);
}

JS_PUBLIC_API mozilla::Maybe<Value> JS::GetExceptionCause(JSObject* exc) {
  if (!exc->is<ErrorObject>()) {
    return mozilla::Nothing();
  }
  auto& error = exc->as<ErrorObject>();
  return error.getCause();
}
