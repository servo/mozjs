/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Interfaces by which the embedding can interact with the Debugger API.

#ifndef js_Debug_h
#define js_Debug_h

#include "mozilla/Assertions.h"
#include "mozilla/BaseProfilerUtils.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Vector.h"

#include "jstypes.h"

#include "js/GCAPI.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/Value.h"

namespace js {
class Debugger;
}  // namespace js

/* Defined in vm/Debugger.cpp. */
extern JS_PUBLIC_API bool JS_DefineDebuggerObject(JSContext* cx,
                                                  JS::HandleObject obj);

extern JS_PUBLIC_API const char* JS_GetLastOOMStackTrace(JSContext* cx);

// If the JS execution tracer is running, this will generate a
// ENTRY_KIND_LABEL_ENTER entry with the specified label.
// The consumer of the trace can then, for instance, correlate all code running
// after this entry and before the corresponding ENTRY_KIND_LABEL_LEAVE with the
// provided label.
// If the tracer is not running, this does nothing.
extern JS_PUBLIC_API void JS_TracerEnterLabelLatin1(JSContext* cx,
                                                    const char* label);
extern JS_PUBLIC_API void JS_TracerEnterLabelTwoByte(JSContext* cx,
                                                     const char16_t* label);

extern JS_PUBLIC_API bool JS_TracerIsTracing(JSContext* cx);

// If the JS execution tracer is running, this will generate a
// ENTRY_KIND_LABEL_LEAVE entry with the specified label.
// It is up to the consumer to decide what to do with a ENTRY_KIND_LABEL_LEAVE
// entry is encountered without a corresponding ENTRY_KIND_LABEL_ENTER.
// If the tracer is not running, this does nothing.
extern JS_PUBLIC_API void JS_TracerLeaveLabelLatin1(JSContext* cx,
                                                    const char* label);
extern JS_PUBLIC_API void JS_TracerLeaveLabelTwoByte(JSContext* cx,
                                                     const char16_t* label);

#ifdef MOZ_EXECUTION_TRACING

// This will begin execution tracing for the JSContext, i.e., this will begin
// recording every entrance into / exit from a function for the given context.
// The trace can be read via JS_TracerSnapshotTrace, and populates the
// ExecutionTrace struct defined below.
//
// This throws if the code coverage is active for any realm in the context.
extern JS_PUBLIC_API bool JS_TracerBeginTracing(JSContext* cx);

// This ends execution tracing for the JSContext, discards the tracing
// buffers, and clears some caches used for tracing. JS_TracerSnapshotTrace
// should be called *before* JS_TracerEndTracing if you want to read the trace
// data for this JSContext.
extern JS_PUBLIC_API bool JS_TracerEndTracing(JSContext* cx);

namespace JS {

// Encoding values used for strings recorded via the tracer.
enum class TracerStringEncoding {
  Latin1,
  TwoByte,
  UTF8,
};

// Value Summary
//
// Value summaries are intended as a best effort, minimal representation of
// values, for the purpose of understanding/debugging an application from a
// recorded trace. At present, we record value summaries for the first
// MAX_ARGUMENTS_TO_RECORD arguments of every function call we record when
// tracing is enabled via JS_TracerBeginTracing above. Value summaries are
// surfaced as a contiguous buffer which is intended to be read as needed
// by looking up values as needed via the index in the `values` field of
// FunctionEnter events in the recorded trace. There is a reader in the
// Firefox Profiler frontend which unpacks the binary representation into
// more easily understandable objects.
//
// Value Summary Types
//
// (NOTE: All values listed below use little-endian byte ordering)
//
// - List<T> - A list of at most MAX_COLLECTION_VALUES items and structured as
//   follows:
//      length:   uint32_t
//      values:   T[min(length, MAX_COLLECTION_VALUES)]
//
// - NestedList<T> - If this is a field of ValueSummary which is not itself
//   nested inside another ValueSummary, this will be the same as a List<T>.
//   However, if it *is* nested, it will contain only the length:
//      length:     uint32_t
//      if not inside another ValueSummary ->
//        values:   T[min(length, MAX_COLLECTION_VALUES)]
//
// - SmallString - a string limited to a length of SMALL_STRING_LENGTH_LIMIT,
//   with the following structure:
//      encodingAndLength:  uint16_t (encoding << 14 | length)
//      payload:            CharT[length]
//   The encoding is one of the values in TracerStringEncoding, and CharT is
//   a char for Latin1 and UTF8, and a char16_t for TwoByte. It should be
//   noted that the original string length before truncation to
//   SMALL_STRING_LENGTH_LIMIT is not written, so it is not possible to
//   distinguish between cases where a string had a true length of
//   SMALL_STRING_LENGTH_LIMIT vs cases where a string was truncated.
//
// - Pair<T,U> - A pairing of a T followed immediately by a U
//      first: T
//      second: U
//
// Value Summary Structure
//
// (NOTE: Here and below, see Value Summary Types for more on what
// the type annotations mean.)
//
//    typeAndFlags:   uint8_t (type << 4 | flags)
//    payload:        see below
//
// The value payload's structure depends on the type and the flags:
//
//    JS::ValueType::Undefined ->       nothing
//    JS::ValueType::Null ->            nothing
//    JS::ValueType::Magic ->           nothing
//      NOTE: JS::ValueType::Magic is only used for dense element holes.
//    JS::ValueType::Boolean ->         nothing
//      NOTE: For a JS::ValueType::Boolean, `flags` will hold `1` for `true`,
//      and `0` for `false`.
//    JS::ValueType::PrivateGCThing ->  unused
//    JS::ValueType::BigInt ->          SmallString
//    JS::ValueType::BigInt ->          SmallString
//
//    JS::ValueType::Int32:
//      if flags != NUMBER_IS_OUT_OF_LINE_MAGIC -> nothing (see MIN_INLINE_INT)
//      else ->                                    int32_t
//
//    JS::ValueType::Double:
//      if flags != NUMBER_IS_OUT_OF_LINE_MAGIC -> nothing (value is +0)
//      else ->                                    double
//
//    JS::ValueType::Symbol:
//      if flags != SYMBOL_NO_DESCRIPTION ->       nothing
//      else ->                                    SmallString
//
//    JS::ValueType::Object:
//      See ObjectSummary
struct ValueSummary {
  enum Flags : uint8_t {
    // If this is set, the object has an array of dense elements right
    // after the shape summary id, which are implicitly keyed as the
    // indices within the array.
    GENERIC_OBJECT_HAS_DENSE_ELEMENTS = 1,

    // If a symbol does not have a description, this is set.
    SYMBOL_NO_DESCRIPTION = 1,

    // If the type is numeric and the flags are equal to this, the value is
    // stored immediately after the header. Otherwise, the value is stored
    // directly in the flags. (See MIN_INLINE_INT)
    NUMBER_IS_OUT_OF_LINE_MAGIC = 0xf,
  };

  // This value is written to the start of the value summaries buffer (see
  // TracedJSContext::valueBuffer), and should be bumped every time the format
  // is changed.
  //
  // Keep in mind to update
  // js/src/jit-test/tests/debug/ExecutionTracer-traced-values.js
  // VALUE_SUMMARY_VERSION value.
  static const uint32_t VERSION = 2;

  // If the type is an int and flags != Flags::NUMBER_IS_OUT_OF_LINE_MAGIC,
  // the value is MIN_INLINE_INT + flags.
  static const int32_t MIN_INLINE_INT = -1;
  static const int32_t MAX_INLINE_INT = 13;

  // Limit on the length of strings in traced value summaries.
  static const size_t SMALL_STRING_LENGTH_LIMIT = 512;

  // The max number of entries to record for general collection objects, such
  // as arrays, sets, and maps. Additionally limits the number of indexed
  // properties recorded for objects. This also limits the number of parameter
  // names to record for Function objects.
  static const size_t MAX_COLLECTION_VALUES = 16;

  // The actual JS Value type.
  JS::ValueType type : 4;

  // See the Flags enum.
  uint8_t flags : 4;

  // A variable length payload may trail the type and flags. See the comment
  // above this class.
};

// An ObjectSummary has the following structure:
//
//    kind:     uint8_t
//    payload:  see below
//
// a structure determined by that kind and by the flags on the ValueSummary
// The structure is as follows:
//
//    Kind::NotImplemented ->
//      shapeSummaryId:     uint32_t (summary will only contain class name)
//        NOTE - above, and where noted below, `shapeSummaryId` is included for
//        the class name, but no property values corresponding to the
//        shapeSummary's property names are present in `values`.
//    Kind::ArrayLike ->
//      shapeSummaryId:     uint32_t (summary will only contain class name)
//      values:             NestedList<ValueSummary>
//        NOTE - at present, ArrayObjects as well as SetObjects are serialized
//        using the ArrayLike structure.
//    Kind::MapLike ->
//      shapeSummaryId:     uint32_t (summary will only contain class name)
//      values:             NestedList<Pair<SmallString, ValueSummary>>
//        NOTE - similar to ArrayLike, the property values noted by the shape
//        are not present here.
//    Kind::Function ->
//      functionName:       SmallString
//      parameterNames:
//        values:           List<SmallString>
//        NOTE - destructuring parameters become an empty string
//    Kind::WrappedPrimitiveObject ->
//      wrappedValue:       ValueSummary
//      object:             same as GenericObject (shapeSummaryId, props, etc.)
//    Kind::GenericObject ->
//      shapeSummaryId:     uint32_t
//      props:              NestedList<PropertySummary> (see below)
//      if flags & GENERIC_OBJECT_HAS_DENSE_ELEMENTS ->
//        denseElements:    NestedList<Pair<SmallString, ValueSummary>>
//    Kind::External ->
//      shapeSummaryId:     uint32_t (summary will only contain class name)
//      externalSize:       uint32_t
//      payload:            (defined by embeddings)
//      The structure for Kind::External entries is defined by embeddings.
//      Embedders can use the JS_SetCustomObjectSummaryCallback, which will
//      define a callback for the tracer to call when tracing objects whose
//      classes have the JSCLASS_IS_DOMJSCLASS flag. From within this callback
//      the embedder should use the JS_TracerSummaryWriter interface to write
//      the data however they see fit. SpiderMonkey will then populate the
//      externalSize field with the amount written.
//      NOTE: it is the embedders' responsibility to manage the versioning of
//      their format.
//    Kind::Error ->
//      shapeSummaryId:     uint32_t (summary will only contain class name)
//      name:               SmallString
//      message:            SmallString
//      stack:              SmallString
//      filename:           SmallString
//      lineNumber:         uint32_t
//      columnNumber        uint32_t
//
// WrappedPrimitiveObjects and GenericObjects make use of a PropertySummary
// type, defined here:
//
// - PropertySummary - A union of either a ValueSummary or the value
//   GETTER_SETTER_MAGIC followed by two value summaries. I.e.:
//      if the current byte in the stream is GETTER_SETTER_MAGIC ->
//        magic:  uint8_t  (GETTER_SETTER_MAGIC)
//        getter: ValueSummary
//        setter: ValueSummary
//      else ->
//        value:  ValueSummary
struct ObjectSummary {
  // This is a special value for ValueSummary::typeAndFlags. It should be noted
  // that this only works as long as 0xf is not a valid JS::ValueType.
  static const uint8_t GETTER_SETTER_MAGIC = 0x0f;

  enum class Kind : uint8_t {
    NotImplemented,
    ArrayLike,
    MapLike,
    Function,
    WrappedPrimitiveObject,
    GenericObject,
    ProxyObject,
    External,
    Error,
  };

  Kind kind;

  // A variable length payload may trail the kind. See the comment above this
  // class.
};

// This is populated by JS_TracerSnapshotTrace and just represent a minimal
// structure for natively representing an execution trace across a range of
// JSContexts (see below). The core of the trace is an array of events, each of
// which is a tagged union with data corresponding to that event. Events can
// also point into various tables, and store all of their string data in a
// contiguous UTF-8 stringBuffer (each string is null-terminated within the
// buffer.)
struct ExecutionTrace {
  enum class EventKind : uint8_t {
    FunctionEnter = 0,
    FunctionLeave = 1,
    LabelEnter = 2,
    LabelLeave = 3,

    // NOTE: the `Error` event has no TracedEvent payload, and will always
    // represent the end of the trace when encountered.
    Error = 4,
  };

  enum class ImplementationType : uint8_t {
    Interpreter = 0,
    Baseline = 1,
    Ion = 2,
    Wasm = 3,
  };

  // See the comment above the `values` field of TracedEvent::functionEvent
  // for an explanation of how these constants apply.
  static const uint32_t MAX_ARGUMENTS_TO_RECORD = 4;
  static const int32_t ZERO_ARGUMENTS_MAGIC = -2;
  static const int32_t EXPIRED_VALUES_MAGIC = -1;
  static const int32_t FUNCTION_LEAVE_VALUES = -1;

  struct TracedEvent {
    EventKind kind;
    union {
      // For FunctionEnter / FunctionLeave
      struct {
        ImplementationType implementation;

        // 1-origin line number of the function
        uint32_t lineNumber;

        // 1-origin column of the function
        uint32_t column;

        // Keys into the thread's scriptUrls HashMap. This key can be missing
        // from the HashMap, although ideally that situation is rare (it is
        // more likely in long running traces with *many* unique functions
        // and/or scripts)
        uint32_t scriptId;

        // ID to the realm that the frame was in. It's used for finding which
        // frame comes from which window/page.
        uint64_t realmID;

        // Keys into the thread's atoms HashMap. This key can be missing from
        // the HashMap as well (see comment above scriptId)
        uint32_t functionNameId;

        // If this value is negative,
        //    ZERO_ARGUMENTS_MAGIC indicates the function call had no arguments
        //    EXPIRED_VALUES_MAGIC indicates the argument values have been
        //      overwritten in the ring buffer.
        //    FUNCTION_LEAVE_VALUES is simply a placeholder value for if this
        //      functionEvent is a FunctionLeave
        //      (TODO: we leave this here because we want to record return
        //      values here, but this is not implemented yet.)
        //
        // If this value is non-negative, this is an index into the
        // TracedJSContext::valueBuffer.
        // At the specified index, if kind == EventKind::FunctionEnter, the
        // call's arguments will be listed in the following format:
        //      argc:     uint32_t
        //      values:   ValueSummary[min(argc, MAX_ARGUMENTS_TO_RECORD)]
        //
        // Additionally, immediately preceding argc will be a uint16_t header
        // containing the size of all of the serialized arguments plus
        // the size of argc and the size of the uint16_t header itself.
        int32_t values;
      } functionEvent;

      // For LabelEnter / LabelLeave
      struct {
        size_t label;  // Indexes directly into the trace's stringBuffer
      } labelEvent;
    };
    // Milliseconds since process creation
    double time;
  };

  // Represents the shape of a traced native object. Essentially this lets us
  // deduplicate the property key array to one location and only store the
  // dense array of property values for each object instance.
  struct ShapeSummary {
    // An identifier for the shape summary, which is referenced by object
    // summaries recorded in the TracedJSContext::valueBuffer.
    uint32_t id;

    // This is the total number of properties for the shape excluding any
    // dense elements on the object.
    uint32_t numProperties;

    // An index into the stringBuffer containing an array, beginning with the
    // class name followed by the array of properties, which will have a length
    // of min(numProperties, MAX_COLLECTION_VALUES). The property keys are for
    // best effort end user comprehension, so for simplicity's sake we just
    // represent all keys as strings, with symbols becoming
    // "Symbol(<description>)". Note that this can result in duplicate keys in
    // the array, when the keys are not actually duplicated on the underlying
    // objects.
    size_t stringBufferOffset;

    // Consider the following example object:
    //
    // {
    //    "0": 0,
    //    "1": 0,
    //    "2": 0,
    //    [Symbol.for("prop1")]: 0,
    //    "prop2": 0,
    //    ...
    //    "prop19": 0,
    //    "prop20": 0,
    // }
    //
    // This will result in a ShapeSummary with numProperties of 20, since "0",
    // "1", and "2" are dense elements, and an array at `stringBufferOffset`
    // looking something like:
    //
    // [
    //    "Object", // The class name
    //    "Symbol(prop1)",
    //    "prop2",
    //    ...
    //    "prop15",
    //    "prop16", // The sequence ends at MAX_COLLECTION_VALUES (16)
    // ]
  };

  struct TracedJSContext {
    mozilla::baseprofiler::BaseProfilerThreadId id;

    // Maps ids to indices into the trace's stringBuffer
    mozilla::HashMap<uint32_t, size_t> scriptUrls;

    // Similar to scriptUrls
    mozilla::HashMap<uint32_t, size_t> atoms;

    // Holds any traced values, in the format defined above (See the
    // ValueSummary type). The first 4 bytes of this buffer will contain
    // the VERSION constant defined above.
    mozilla::Vector<uint8_t> valueBuffer;

    // Holds shape information for objects traced in the valueBuffer
    mozilla::Vector<ShapeSummary> shapeSummaries;

    mozilla::Vector<TracedEvent> events;
  };

  mozilla::Vector<char> stringBuffer;

  // This will be populated with an entry for each context which had tracing
  // enabled via JS_TracerBeginTracing.
  mozilla::Vector<TracedJSContext> contexts;
};
}  // namespace JS

// Captures the trace for all JSContexts in the process which are currently
// tracing.
extern JS_PUBLIC_API bool JS_TracerSnapshotTrace(JS::ExecutionTrace& trace);

// Given that embeddings may want to add support for serializing their own
// types, we expose here a means of registering a callback for serializing
// them. The JS_TracerSummaryWriter exposes a means of writing common types
// to the tracer's value ring buffer, and JS_SetCustomObjectSummaryCallback
// sets a callback on the JSContext
struct JS_TracerSummaryWriterImpl;

struct JS_PUBLIC_API JS_TracerSummaryWriter {
  JS_TracerSummaryWriterImpl* impl;

  void writeUint8(uint8_t val);
  void writeUint16(uint16_t val);
  void writeUint32(uint32_t val);
  void writeUint64(uint64_t val);

  void writeInt8(int8_t val);
  void writeInt16(int16_t val);
  void writeInt32(int32_t val);
  void writeInt64(int64_t val);

  void writeUTF8String(const char* val);
  void writeTwoByteString(const char16_t* val);

  bool writeValue(JSContext* cx, JS::Handle<JS::Value> val);
};

// - `obj` is the object intended to be summarized.
// - `nested` is true if this object is a nested property of another
//   JS::ValueSummary being written.
// - `writer` is an interface which should be used to write the serialized
//   summary.
using CustomObjectSummaryCallback = bool (*)(JSContext*,
                                             JS::Handle<JSObject*> obj,
                                             bool nested,
                                             JS_TracerSummaryWriter* writer);

extern JS_PUBLIC_API void JS_SetCustomObjectSummaryCallback(
    JSContext* cx, CustomObjectSummaryCallback callback);

#endif /* MOZ_EXECUTION_TRACING */

namespace JS {
namespace dbg {

// [SMDOC] Debugger builder API
//
// Helping embedding code build objects for Debugger
// -------------------------------------------------
//
// Some Debugger API features lean on the embedding application to construct
// their result values. For example, Debugger.Frame.prototype.scriptEntryReason
// calls hooks provided by the embedding to construct values explaining why it
// invoked JavaScript; if F is a frame called from a mouse click event handler,
// F.scriptEntryReason would return an object of the form:
//
//   { eventType: "mousedown", event: <object> }
//
// where <object> is a Debugger.Object whose referent is the event being
// dispatched.
//
// However, Debugger implements a trust boundary. Debuggee code may be
// considered untrusted; debugger code needs to be protected from debuggee
// getters, setters, proxies, Object.watch watchpoints, and any other feature
// that might accidentally cause debugger code to set the debuggee running. The
// Debugger API tries to make it easy to write safe debugger code by only
// offering access to debuggee objects via Debugger.Object instances, which
// ensure that only those operations whose explicit purpose is to invoke
// debuggee code do so. But this protective membrane is only helpful if we
// interpose Debugger.Object instances in all the necessary spots.
//
// SpiderMonkey's compartment system also implements a trust boundary. The
// debuggee and debugger are always in different compartments. Inter-compartment
// work requires carefully tracking which compartment each JSObject or JS::Value
// belongs to, and ensuring that is is correctly wrapped for each operation.
//
// It seems precarious to expect the embedding's hooks to implement these trust
// boundaries. Instead, the JS::dbg::Builder API segregates the code which
// constructs trusted objects from that which deals with untrusted objects.
// Trusted objects have an entirely different C++ type, so code that improperly
// mixes trusted and untrusted objects is caught at compile time.
//
// In the structure shown above, there are two trusted objects, and one
// untrusted object:
//
// - The overall object, with the 'eventType' and 'event' properties, is a
//   trusted object. We're going to return it to D.F.p.scriptEntryReason's
//   caller, which will handle it directly.
//
// - The Debugger.Object instance appearing as the value of the 'event' property
//   is a trusted object. It belongs to the same Debugger instance as the
//   Debugger.Frame instance whose scriptEntryReason accessor was called, and
//   presents a safe reflection-oriented API for inspecting its referent, which
//   is:
//
// - The actual event object, an untrusted object, and the referent of the
//   Debugger.Object above. (Content can do things like replacing accessors on
//   Event.prototype.)
//
// Using JS::dbg::Builder, all objects and values the embedding deals with
// directly are considered untrusted, and are assumed to be debuggee values. The
// only way to construct trusted objects is to use Builder's own methods, which
// return a separate Object type. The only way to set a property on a trusted
// object is through that Object type. The actual trusted object is never
// exposed to the embedding.
//
// So, for example, the embedding might use code like the following to construct
// the object shown above, given a Builder passed to it by Debugger:
//
//    bool
//    MyScriptEntryReason::explain(JSContext* cx,
//                                 Builder& builder,
//                                 Builder::Object& result)
//    {
//        JSObject* eventObject = ... obtain debuggee event object somehow ...;
//        if (!eventObject) {
//            return false;
//        }
//        result = builder.newObject(cx);
//        return result &&
//               result.defineProperty(cx, "eventType",
//                                     SafelyFetchType(eventObject)) &&
//               result.defineProperty(cx, "event", eventObject);
//    }
//
//
// Object::defineProperty also accepts an Object as the value to store on the
// property. By its type, we know that the value is trusted, so we set it
// directly as the property's value, without interposing a Debugger.Object
// wrapper. This allows the embedding to builted nested structures of trusted
// objects.
//
// The Builder and Builder::Object methods take care of doing whatever
// compartment switching and wrapping are necessary to construct the trusted
// values in the Debugger's compartment.
//
// The Object type is self-rooting. Construction, assignment, and destruction
// all properly root the referent object.

class BuilderOrigin;

class Builder {
  // The Debugger instance whose client we are building a value for. We build
  // objects in this object's compartment.
  PersistentRootedObject debuggerObject;

  // debuggerObject's Debugger structure, for convenience.
  js::Debugger* debugger;

  // Check that |thing| is in the same compartment as our debuggerObject. Used
  // for assertions when constructing BuiltThings. We can overload this as we
  // add more instantiations of BuiltThing.
#ifdef DEBUG
  void assertBuilt(JSObject* obj);
#else
  void assertBuilt(JSObject* obj) {}
#endif

 protected:
  // A reference to a trusted object or value. At the moment, we only use it
  // with JSObject*.
  template <typename T>
  class BuiltThing {
    friend class BuilderOrigin;

   protected:
    // The Builder to which this trusted thing belongs.
    Builder& owner;

    // A rooted reference to our value.
    PersistentRooted<T> value;

    BuiltThing(JSContext* cx, Builder& owner_,
               T value_ = SafelyInitialized<T>::create())
        : owner(owner_), value(cx, value_) {
      owner.assertBuilt(value_);
    }

    // Forward some things from our owner, for convenience.
    js::Debugger* debugger() const { return owner.debugger; }
    JSObject* debuggerObject() const { return owner.debuggerObject; }

   public:
    BuiltThing(const BuiltThing& rhs) : owner(rhs.owner), value(rhs.value) {}
    BuiltThing& operator=(const BuiltThing& rhs) {
      MOZ_ASSERT(&owner == &rhs.owner);
      owner.assertBuilt(rhs.value);
      value = rhs.value;
      return *this;
    }

    explicit operator bool() const {
      // If we ever instantiate BuiltThing<Value>, this might not suffice.
      return value;
    }

   private:
    BuiltThing() = delete;
  };

 public:
  // A reference to a trusted object, possibly null. Instances of Object are
  // always properly rooted. They can be copied and assigned, as if they were
  // pointers.
  class Object : private BuiltThing<JSObject*> {
    friend class Builder;        // for construction
    friend class BuilderOrigin;  // for unwrapping

    typedef BuiltThing<JSObject*> Base;

    // This is private, because only Builders can create Objects that
    // actually point to something (hence the 'friend' declaration).
    Object(JSContext* cx, Builder& owner_, HandleObject obj)
        : Base(cx, owner_, obj.get()) {}

    bool definePropertyToTrusted(JSContext* cx, const char* name,
                                 JS::MutableHandleValue value);

   public:
    Object(JSContext* cx, Builder& owner_) : Base(cx, owner_, nullptr) {}
    Object(const Object& rhs) = default;

    // Our automatically-generated assignment operator can see our base
    // class's assignment operator, so we don't need to write one out here.

    // Set the property named |name| on this object to |value|.
    //
    // If |value| is a string or primitive, re-wrap it for the debugger's
    // compartment.
    //
    // If |value| is an object, assume it is a debuggee object and make a
    // Debugger.Object instance referring to it. Set that as the propery's
    // value.
    //
    // If |value| is another trusted object, store it directly as the
    // property's value.
    //
    // On error, report the problem on cx and return false.
    bool defineProperty(JSContext* cx, const char* name, JS::HandleValue value);
    bool defineProperty(JSContext* cx, const char* name,
                        JS::HandleObject value);
    bool defineProperty(JSContext* cx, const char* name, Object& value);

    using Base::operator bool;
  };

  // Build an empty object for direct use by debugger code, owned by this
  // Builder. If an error occurs, report it on cx and return a false Object.
  Object newObject(JSContext* cx);

 protected:
  Builder(JSContext* cx, js::Debugger* debugger);
};

// Debugger itself instantiates this subclass of Builder, which can unwrap
// BuiltThings that belong to it.
class BuilderOrigin : public Builder {
  template <typename T>
  T unwrapAny(const BuiltThing<T>& thing) {
    MOZ_ASSERT(&thing.owner == this);
    return thing.value.get();
  }

 public:
  BuilderOrigin(JSContext* cx, js::Debugger* debugger_)
      : Builder(cx, debugger_) {}

  JSObject* unwrap(Object& object) { return unwrapAny(object); }
};

// Finding the size of blocks allocated with malloc
// ------------------------------------------------
//
// Debugger.Memory wants to be able to report how many bytes items in memory are
// consuming. To do this, it needs a function that accepts a pointer to a block,
// and returns the number of bytes allocated to that block. SpiderMonkey itself
// doesn't know which function is appropriate to use, but the embedding does.

// Tell Debuggers in |cx| to use |mallocSizeOf| to find the size of
// malloc'd blocks.
JS_PUBLIC_API void SetDebuggerMallocSizeOf(JSContext* cx,
                                           mozilla::MallocSizeOf mallocSizeOf);

// Get the MallocSizeOf function that the given context is using to find the
// size of malloc'd blocks.
JS_PUBLIC_API mozilla::MallocSizeOf GetDebuggerMallocSizeOf(JSContext* cx);

// Debugger and Garbage Collection Events
// --------------------------------------
//
// The Debugger wants to report about its debuggees' GC cycles, however entering
// JS after a GC is troublesome since SpiderMonkey will often do something like
// force a GC and then rely on the nursery being empty. If we call into some
// Debugger's hook after the GC, then JS runs and the nursery won't be
// empty. Instead, we rely on embedders to call back into SpiderMonkey after a
// GC and notify Debuggers to call their onGarbageCollection hook.

// Determine whether it's necessary to call FireOnGarbageCollectionHook() after
// a GC. This is only required if there are debuggers with an
// onGarbageCollection hook observing a global in the set of collected zones.
JS_PUBLIC_API bool FireOnGarbageCollectionHookRequired(JSContext* cx);

// For each Debugger that observed a debuggee involved in the given GC event,
// call its `onGarbageCollection` hook.
JS_PUBLIC_API bool FireOnGarbageCollectionHook(
    JSContext* cx, GarbageCollectionEvent::Ptr&& data);

// Return true if the given value is a Debugger object, false otherwise.
JS_PUBLIC_API bool IsDebugger(JSObject& obj);

// Append each of the debuggee global objects observed by the Debugger object
// |dbgObj| to |vector|. Returns true on success, false on failure.
JS_PUBLIC_API bool GetDebuggeeGlobals(JSContext* cx, JSObject& dbgObj,
                                      MutableHandleObjectVector vector);

// Returns true if there's any debugger attached to the given context where
// the debugger's "shouldAvoidSideEffects" property is true.
//
// This is supposed to be used by native code that performs side-effectful
// operations where the debugger cannot hook it.
//
// If this function returns true, the native function should throw an
// uncatchable exception by returning `false` without setting any pending
// exception. The debugger will handle this exception by aborting the eager
// evaluation.
//
// The native code can opt into this behavior to help the debugger performing
// the side-effect-free evaluation.
//
// Expected consumers of this API include JSClassOps.resolve hooks which have
// any side-effect other than just resolving the property.
//
// Example:
//   static bool ResolveHook(JSContext* cx, HandleObject obj, HandleId id,
//                           bool* resolvedp) {
//     *resolvedp = false;
//     if (JS::dbg::ShouldAvoidSideEffects()) {
//       return false;
//     }
//     // Resolve the property with the side-effect.
//     ...
//     return true;
//   }
bool ShouldAvoidSideEffects(JSContext* cx);

}  // namespace dbg
}  // namespace JS

#endif /* js_Debug_h */
