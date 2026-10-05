/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "debugger/ExecutionTracer.h"

#include "mozilla/EndianUtils.h"    // NativeEndian
#include "mozilla/FloatingPoint.h"  // IsPositiveZero
#include "mozilla/Printf.h"         // SprintfBuf

#include "builtin/BigInt.h"     // BigIntObject
#include "builtin/MapObject.h"  // MapObject, SetObject
#include "builtin/Symbol.h"     // SymbolObject

#include "debugger/Frame.h"  // DebuggerFrameType

#include "vm/BooleanObject.h"  // BooleanObject
#include "vm/ErrorObject.h"
#include "vm/NumberObject.h"      // NumberObject
#include "vm/ObjectOperations.h"  // DefineDataElement
#include "vm/StringObject.h"      // StringObject
#include "vm/Time.h"

#include "debugger/Debugger-inl.h"
#include "vm/ErrorObject-inl.h"  // ErrorObject.fileName,lineNumber,...
#include "vm/Stack-inl.h"

using namespace js;

MOZ_RUNINIT mozilla::Vector<ExecutionTracer*> ExecutionTracer::globalInstances;
MOZ_RUNINIT Mutex
    ExecutionTracer::globalInstanceLock(mutexid::ExecutionTracerGlobalLock);

// This is a magic value we write as the last 64 bits of a FunctionEnter event
// in ExecutionTracer::inlineData_. It just means that the actual argc for the
// function call was 0. If the last 64 bits are not this value, they instead
// represent the index into ExecutionTracer::valueData_ at which we can find
// the actual argc count as well as the list of ValueSummaries for the argument
// values. Having this magic value allows us to avoid needing to write a 32-bit
// `0` to ExecutionTracer::valueData_ in the common case where a function is
// called with no arguments. This value is essentially the 64-bit mirror to
// JS::ExecutionTrace::ZERO_ARGUMENTS_MAGIC.
const uint64_t IN_BUFFER_ZERO_ARGUMENTS_MAGIC = 0xFFFFFFFFFFFFFFFFllu;

static JS::ExecutionTrace::ImplementationType GetImplementation(
    AbstractFramePtr frame) {
  if (frame.isBaselineFrame()) {
    return JS::ExecutionTrace::ImplementationType::Baseline;
  }

  if (frame.isRematerializedFrame()) {
    return JS::ExecutionTrace::ImplementationType::Ion;
  }

  if (frame.isWasmDebugFrame()) {
    return JS::ExecutionTrace::ImplementationType::Wasm;
  }

  return JS::ExecutionTrace::ImplementationType::Interpreter;
}

static DebuggerFrameType GetFrameType(AbstractFramePtr frame) {
  // Indirect eval frames are both isGlobalFrame() and isEvalFrame(), so the
  // order of checks here is significant.
  if (frame.isEvalFrame()) {
    return DebuggerFrameType::Eval;
  }

  if (frame.isGlobalFrame()) {
    return DebuggerFrameType::Global;
  }

  if (frame.isFunctionFrame()) {
    return DebuggerFrameType::Call;
  }

  if (frame.isModuleFrame()) {
    return DebuggerFrameType::Module;
  }

  if (frame.isWasmDebugFrame()) {
    return DebuggerFrameType::WasmCall;
  }

  MOZ_CRASH("Unknown frame type");
}

[[nodiscard]] static bool GetFunctionName(JSContext* cx,
                                          JS::Handle<JSFunction*> fun,
                                          JS::MutableHandle<JSAtom*> result) {
  if (!fun->getDisplayAtom(cx, result)) {
    return false;
  }

  if (result) {
    cx->markAtom(result);
  }
  return true;
}

static double GetNowMilliseconds() {
  return (mozilla::TimeStamp::Now() - mozilla::TimeStamp::ProcessCreation())
      .ToMilliseconds();
}

void ExecutionTracer::handleError(JSContext* cx) {
  inlineData_.beginWritingEntry();
  inlineData_.write(uint8_t(InlineEntryType::Error));
  inlineData_.finishWritingEntry();
  cx->clearPendingException();
  cx->suspendExecutionTracing();
}

void ExecutionTracer::writeScriptUrl(ScriptSource* scriptSource) {
  outOfLineData_.beginWritingEntry();
  outOfLineData_.write(uint8_t(OutOfLineEntryType::ScriptURL));
  outOfLineData_.write(scriptSource->id());

  if (scriptSource->hasDisplayURL()) {
    outOfLineData_.writeCString<char16_t, JS::TracerStringEncoding::TwoByte>(
        scriptSource->displayURL());
  } else {
    const char* filename =
        scriptSource->filename() ? scriptSource->filename() : "";
    outOfLineData_.writeCString<char, JS::TracerStringEncoding::UTF8>(filename);
  }
  outOfLineData_.finishWritingEntry();
}

bool ExecutionTracer::writeAtom(JSContext* cx, JS::Handle<JSAtom*> atom,
                                uint32_t id) {
  outOfLineData_.beginWritingEntry();
  outOfLineData_.write(uint8_t(OutOfLineEntryType::Atom));
  outOfLineData_.write(id);

  if (!atom) {
    outOfLineData_.writeEmptyString();
  } else {
    if (!outOfLineData_.writeString(cx, atom)) {
      return false;
    }
  }
  outOfLineData_.finishWritingEntry();
  return true;
}

bool ExecutionTracer::writeFunctionFrame(JSContext* cx,
                                         AbstractFramePtr frame) {
  JS::Rooted<JSFunction*> fn(cx, frame.callee());
  TracingCaches& caches = cx->caches().tracingCaches;
  if (fn->baseScript()) {
    uint32_t scriptSourceId = fn->baseScript()->scriptSource()->id();
    TracingCaches::GetOrPutResult scriptSourceRes =
        caches.putScriptSourceIfMissing(scriptSourceId);
    if (scriptSourceRes == TracingCaches::GetOrPutResult::OOM) {
      ReportOutOfMemory(cx);
      return false;
    }
    if (scriptSourceRes == TracingCaches::GetOrPutResult::NewlyAdded) {
      writeScriptUrl(fn->baseScript()->scriptSource());
    }
    inlineData_.write(fn->baseScript()->lineno());
    inlineData_.write(fn->baseScript()->column().oneOriginValue());
    inlineData_.write(scriptSourceId);
    inlineData_.write(
        fn->baseScript()->realm()->creationOptions().profilerRealmID());
  } else {
    // In the case of no baseScript, we just fill it out with 0s. 0 is an
    // invalid script source ID, so it is distinguishable from a real one
    inlineData_.write(uint32_t(0));  // line number
    inlineData_.write(uint32_t(0));  // column
    inlineData_.write(uint32_t(0));  // script source id
    inlineData_.write(uint64_t(0));  // realm id
  }

  JS::Rooted<JSAtom*> functionName(cx);
  if (!GetFunctionName(cx, fn, &functionName)) {
    return false;
  }
  uint32_t functionNameId = 0;
  TracingCaches::GetOrPutResult fnNameRes =
      caches.getOrPutAtom(functionName, &functionNameId);
  if (fnNameRes == TracingCaches::GetOrPutResult::OOM) {
    ReportOutOfMemory(cx);
    return false;
  }
  if (fnNameRes == TracingCaches::GetOrPutResult::NewlyAdded) {
    if (!writeAtom(cx, functionName, functionNameId)) {
      // It's worth noting here that this will leave the caches out of sync
      // with what has actually been written into the out of line data.
      // This is a normal and allowed situation for the tracer, so we have
      // no special handling here for it. However, if we ever want to make
      // a stronger guarantee in the future, we need to revisit this.
      return false;
    }
  }

  inlineData_.write(functionNameId);
  inlineData_.write(uint8_t(GetImplementation(frame)));
  inlineData_.write(GetNowMilliseconds());
  return true;
}

void ExecutionTracer::onEnterFrame(JSContext* cx, AbstractFramePtr frame) {
  LockGuard<Mutex> guard(bufferLock_);

  DebuggerFrameType type = GetFrameType(frame);
  if (type == DebuggerFrameType::Call) {
    if (frame.isFunctionFrame() && !frame.callee()->isSelfHostedBuiltin()) {
      inlineData_.beginWritingEntry();
      inlineData_.write(uint8_t(InlineEntryType::StackFunctionEnter));
      if (!writeFunctionFrame(cx, frame)) {
        handleError(cx);
        return;
      }

      if (frame.numActualArgs() == 0) {
        inlineData_.write(IN_BUFFER_ZERO_ARGUMENTS_MAGIC);
      } else {
        uint64_t argumentsIndex;
        if (!valueSummaries_.writeArguments(cx, frame, &argumentsIndex)) {
          handleError(cx);
          return;
        }
        inlineData_.write(argumentsIndex);
      }

      inlineData_.finishWritingEntry();
    }
  }
}

void ExecutionTracer::onLeaveFrame(JSContext* cx, AbstractFramePtr frame) {
  LockGuard<Mutex> guard(bufferLock_);

  DebuggerFrameType type = GetFrameType(frame);
  if (type == DebuggerFrameType::Call) {
    if (frame.isFunctionFrame() && !frame.callee()->isSelfHostedBuiltin()) {
      inlineData_.beginWritingEntry();
      inlineData_.write(uint8_t(InlineEntryType::StackFunctionLeave));
      if (!writeFunctionFrame(cx, frame)) {
        handleError(cx);
        return;
      }
      inlineData_.finishWritingEntry();
    }
  }
}

template <typename CharType, JS::TracerStringEncoding Encoding>
void ExecutionTracer::onEnterLabel(const CharType* eventType) {
  LockGuard<Mutex> guard(bufferLock_);

  inlineData_.beginWritingEntry();
  inlineData_.write(uint8_t(InlineEntryType::LabelEnter));
  inlineData_.writeCString<CharType, Encoding>(eventType);
  inlineData_.write(GetNowMilliseconds());
  inlineData_.finishWritingEntry();
}

template <typename CharType, JS::TracerStringEncoding Encoding>
void ExecutionTracer::onLeaveLabel(const CharType* eventType) {
  LockGuard<Mutex> guard(bufferLock_);

  inlineData_.beginWritingEntry();
  inlineData_.write(uint8_t(InlineEntryType::LabelLeave));
  inlineData_.writeCString<CharType, Encoding>(eventType);
  inlineData_.write(GetNowMilliseconds());
  inlineData_.finishWritingEntry();
}

bool ExecutionTracer::readFunctionFrame(
    JS::ExecutionTrace::EventKind kind,
    JS::ExecutionTrace::TracedEvent& event) {
  MOZ_ASSERT(kind == JS::ExecutionTrace::EventKind::FunctionEnter ||
             kind == JS::ExecutionTrace::EventKind::FunctionLeave);

  event.kind = kind;

  uint8_t implementation;
  inlineData_.read(&event.functionEvent.lineNumber);
  inlineData_.read(&event.functionEvent.column);
  inlineData_.read(&event.functionEvent.scriptId);
  inlineData_.read(&event.functionEvent.realmID);
  inlineData_.read(&event.functionEvent.functionNameId);
  inlineData_.read(&implementation);
  inlineData_.read(&event.time);

  event.functionEvent.implementation =
      JS::ExecutionTrace::ImplementationType(implementation);

  if (kind == JS::ExecutionTrace::EventKind::FunctionEnter) {
    uint64_t argumentsIndex;
    inlineData_.read(&argumentsIndex);
    if (argumentsIndex == IN_BUFFER_ZERO_ARGUMENTS_MAGIC) {
      event.functionEvent.values = JS::ExecutionTrace::ZERO_ARGUMENTS_MAGIC;
    } else {
      event.functionEvent.values =
          valueSummaries_.getOutputBufferIndex(argumentsIndex);
    }
  } else {
    event.functionEvent.values = JS::ExecutionTrace::FUNCTION_LEAVE_VALUES;
  }

  return true;
}

bool ExecutionTracer::readLabel(JS::ExecutionTrace::EventKind kind,
                                JS::ExecutionTrace::TracedEvent& event,
                                TracingScratchBuffer& scratchBuffer,
                                mozilla::Vector<char>& stringBuffer) {
  MOZ_ASSERT(kind == JS::ExecutionTrace::EventKind::LabelEnter ||
             kind == JS::ExecutionTrace::EventKind::LabelLeave);

  event.kind = kind;
  size_t index;
  if (!inlineData_.readString(scratchBuffer, stringBuffer, &index)) {
    return false;
  }
  event.labelEvent.label = index;

  double time;
  inlineData_.read(&time);
  event.time = time;

  return true;
}

bool ExecutionTracer::readInlineEntry(
    mozilla::Vector<JS::ExecutionTrace::TracedEvent>& events,
    TracingScratchBuffer& scratchBuffer, mozilla::Vector<char>& stringBuffer) {
  uint8_t entryType;
  inlineData_.read(&entryType);

  switch (InlineEntryType(entryType)) {
    case InlineEntryType::StackFunctionEnter:
    case InlineEntryType::StackFunctionLeave: {
      JS::ExecutionTrace::EventKind kind;
      if (InlineEntryType(entryType) == InlineEntryType::StackFunctionEnter) {
        kind = JS::ExecutionTrace::EventKind::FunctionEnter;
      } else {
        kind = JS::ExecutionTrace::EventKind::FunctionLeave;
      }
      JS::ExecutionTrace::TracedEvent event;
      if (!readFunctionFrame(kind, event)) {
        return false;
      }

      if (!events.append(std::move(event))) {
        return false;
      }
      return true;
    }
    case InlineEntryType::LabelEnter:
    case InlineEntryType::LabelLeave: {
      JS::ExecutionTrace::EventKind kind;
      if (InlineEntryType(entryType) == InlineEntryType::LabelEnter) {
        kind = JS::ExecutionTrace::EventKind::LabelEnter;
      } else {
        kind = JS::ExecutionTrace::EventKind::LabelLeave;
      }

      JS::ExecutionTrace::TracedEvent event;
      if (!readLabel(kind, event, scratchBuffer, stringBuffer)) {
        return false;
      }

      if (!events.append(std::move(event))) {
        return false;
      }

      return true;
    }
    case InlineEntryType::Error: {
      JS::ExecutionTrace::TracedEvent event;
      event.kind = JS::ExecutionTrace::EventKind::Error;

      if (!events.append(std::move(event))) {
        return false;
      }

      return true;
    }
    default:
      return false;
  }
}

bool ExecutionTracer::readOutOfLineEntry(
    mozilla::HashMap<uint32_t, size_t>& scriptUrls,
    mozilla::HashMap<uint32_t, size_t>& atoms,
    mozilla::Vector<JS::ExecutionTrace::ShapeSummary>& shapes,
    TracingScratchBuffer& scratchBuffer, mozilla::Vector<char>& stringBuffer) {
  uint8_t entryType;
  outOfLineData_.read(&entryType);

  switch (OutOfLineEntryType(entryType)) {
    case OutOfLineEntryType::ScriptURL: {
      uint32_t id;
      outOfLineData_.read(&id);

      size_t index;
      if (!outOfLineData_.readString(scratchBuffer, stringBuffer, &index)) {
        return false;
      }

      if (!scriptUrls.put(id, index)) {
        return false;
      }

      return true;
    }
    case OutOfLineEntryType::Atom: {
      uint32_t id;
      outOfLineData_.read(&id);

      size_t index;
      if (!outOfLineData_.readString(scratchBuffer, stringBuffer, &index)) {
        return false;
      }

      if (!atoms.put(id, index)) {
        return false;
      }

      return true;
    }
    case OutOfLineEntryType::Shape: {
      JS::ExecutionTrace::ShapeSummary shape;
      outOfLineData_.read(&shape.id);
      outOfLineData_.read(&shape.numProperties);
      shape.stringBufferOffset = stringBuffer.length();

      size_t dummyIndex;
      if (!outOfLineData_.readString(scratchBuffer, stringBuffer,
                                     &dummyIndex)) {
        return false;
      }

      size_t realPropertyCount =
          std::min(size_t(shape.numProperties),
                   size_t(JS::ValueSummary::MAX_COLLECTION_VALUES));
      for (uint32_t i = 0; i < realPropertyCount; ++i) {
        uint8_t propKeyKind;
        outOfLineData_.read(&propKeyKind);
        switch (PropertyKeyKind(propKeyKind)) {
          case PropertyKeyKind::Undefined:
            if (!stringBuffer.growByUninitialized(sizeof("undefined"))) {
              return false;
            }
            memcpy(stringBuffer.end() - sizeof("undefined"), "undefined",
                   sizeof("undefined"));
            break;
          case PropertyKeyKind::Symbol: {
            constexpr size_t prefixLength = sizeof("Symbol(") - 1;
            if (!stringBuffer.growByUninitialized(prefixLength)) {
              return false;
            }
            memcpy(stringBuffer.end() - prefixLength, "Symbol(", prefixLength);

            if (!outOfLineData_.readSmallString(scratchBuffer, stringBuffer,
                                                &dummyIndex)) {
              return false;
            }

            // Remove the null terminator
            stringBuffer.shrinkBy(1);
            if (!stringBuffer.append(')')) {
              return false;
            }
            if (!stringBuffer.append(0)) {
              return false;
            }

            break;
          }
          case PropertyKeyKind::Int: {
            int32_t intVal;
            outOfLineData_.read(&intVal);
            size_t reserveLength = sizeof("-2147483648");
            if (!stringBuffer.reserve(stringBuffer.length() + reserveLength)) {
              return false;
            }

            char* writePtr = stringBuffer.end();
            int len = SprintfBuf(writePtr, reserveLength, "%d", intVal);

            if (!stringBuffer.growByUninitialized(len + 1)) {
              return false;
            }
            break;
          }
          case PropertyKeyKind::String: {
            if (!outOfLineData_.readSmallString(scratchBuffer, stringBuffer,
                                                &dummyIndex)) {
              return false;
            }
            break;
          }
          default:
            MOZ_CRASH("Bad PropertyKeyKind");
        }
      }

      if (!shapes.append(shape)) {
        return false;
      }

      return true;
    }
    default:
      return false;
  }
}

bool ExecutionTracer::readInlineEntries(
    mozilla::Vector<JS::ExecutionTrace::TracedEvent>& events,
    TracingScratchBuffer& scratchBuffer, mozilla::Vector<char>& stringBuffer) {
  while (inlineData_.readable()) {
    inlineData_.beginReadingEntry();
    if (!readInlineEntry(events, scratchBuffer, stringBuffer)) {
      inlineData_.skipEntry();
      return false;
    }
    inlineData_.finishReadingEntry();
  }
  return true;
}

bool ExecutionTracer::readOutOfLineEntries(
    mozilla::HashMap<uint32_t, size_t>& scriptUrls,
    mozilla::HashMap<uint32_t, size_t>& atoms,
    mozilla::Vector<JS::ExecutionTrace::ShapeSummary>& shapes,
    TracingScratchBuffer& scratchBuffer, mozilla::Vector<char>& stringBuffer) {
  while (outOfLineData_.readable()) {
    outOfLineData_.beginReadingEntry();
    if (!readOutOfLineEntry(scriptUrls, atoms, shapes, scratchBuffer,
                            stringBuffer)) {
      outOfLineData_.skipEntry();
      return false;
    }
    outOfLineData_.finishReadingEntry();
  }
  return true;
}

bool ExecutionTracer::getNativeTrace(
    JS::ExecutionTrace::TracedJSContext& context,
    TracingScratchBuffer& scratchBuffer, mozilla::Vector<char>& stringBuffer) {
  LockGuard<Mutex> guard(bufferLock_);

  if (!readOutOfLineEntries(context.scriptUrls, context.atoms,
                            context.shapeSummaries, scratchBuffer,
                            stringBuffer)) {
    return false;
  }

  if (!readInlineEntries(context.events, scratchBuffer, stringBuffer)) {
    return false;
  }

  if (!valueSummaries_.populateOutputBuffer(context)) {
    return false;
  }

  return true;
}

bool ExecutionTracer::getNativeTraceForAllContexts(JS::ExecutionTrace& trace) {
  LockGuard<Mutex> guard(globalInstanceLock);
  TracingScratchBuffer scratchBuffer;
  for (ExecutionTracer* tracer : globalInstances) {
    JS::ExecutionTrace::TracedJSContext* context = nullptr;
    for (JS::ExecutionTrace::TracedJSContext& t : trace.contexts) {
      if (t.id == tracer->threadId_) {
        context = &t;
        break;
      }
    }
    if (!context) {
      if (!trace.contexts.append(JS::ExecutionTrace::TracedJSContext())) {
        return false;
      }
      context = &trace.contexts[trace.contexts.length() - 1];
      context->id = tracer->threadId_;
    }
    if (!tracer->getNativeTrace(*context, scratchBuffer, trace.stringBuffer)) {
      return false;
    }
  }

  return true;
}

struct JS_TracerSummaryWriterImpl {
  ValueSummaries* valueSummaries;
  friend struct JS_TracerSummaryWriter;
};

enum class GetNativeDataPropertyResult {
  // We need to do something other than grab a value from a slot to read this.
  // Either the class may want to resolve the id with a hook or we have to look
  // it up on a proto that's not a NativeObject
  Other,

  // Simplest case: the property is just somewhere in the objects slots
  DataProperty,

  // The property is an accessor
  Getter,

  // The property is some kind of special derived property, like an Array's
  // length, for example
  CustomDataProperty,

  // The property is missing from the object and its proto chain
  Missing,
};

// Note: `result` will only be set in the case where this returns
// GetNativeDataPropertyResult::DataProperty
GetNativeDataPropertyResult GetNativeDataProperty(JSContext* cx,
                                                  NativeObject* nobj,
                                                  JS::PropertyKey id,
                                                  JS::Value* result) {
  while (true) {
    MOZ_ASSERT(!nobj->getOpsLookupProperty());

    uint32_t index;
    if (PropMap* map = nobj->shape()->lookup(cx, id, &index)) {
      PropertyInfo prop = map->getPropertyInfo(index);
      if (prop.isDataProperty()) {
        *result = nobj->getSlot(prop.slot());
        return GetNativeDataPropertyResult::DataProperty;
      } else if (prop.isCustomDataProperty()) {
        return GetNativeDataPropertyResult::CustomDataProperty;
      }

      MOZ_ASSERT(prop.isAccessorProperty());
      return GetNativeDataPropertyResult::Getter;
    }

    if (!nobj->is<PlainObject>()) {
      if (ClassMayResolveId(cx->names(), nobj->getClass(), id, nobj)) {
        return GetNativeDataPropertyResult::Other;
      }
    }

    JSObject* proto = nobj->staticPrototype();
    if (!proto) {
      return GetNativeDataPropertyResult::Missing;
    }

    if (!proto->is<NativeObject>()) {
      return GetNativeDataPropertyResult::Other;
    }
    nobj = &proto->as<NativeObject>();
  }

  MOZ_ASSERT_UNREACHABLE();
}

void ValueSummaries::writeHeader(JS::ValueType type, uint8_t flags) {
  // 4 bits for the type, 4 bits for the flags
  MOZ_ASSERT((uint8_t(type) & 0xF0) == 0);
  MOZ_ASSERT((flags & 0xF0) == 0);
  JS::ValueSummary header;
  header.type = type;
  header.flags = flags;
  MOZ_ASSERT(*reinterpret_cast<uint8_t*>(&header) !=
             JS::ObjectSummary::GETTER_SETTER_MAGIC);
  valueData_->writeBytes(reinterpret_cast<const uint8_t*>(&header),
                         sizeof(header));
}

bool ValueSummaries::writeShapeSummary(JSContext* cx,
                                       JS::Handle<NativeShape*> shape) {
  TracingCaches& caches = cx->caches().tracingCaches;

  uint32_t shapeId = 0;
  TracingCaches::GetOrPutResult cacheResult =
      caches.getOrPutShape(shape, &shapeId);
  if (cacheResult == TracingCaches::GetOrPutResult::OOM) {
    ReportOutOfMemory(cx);
    return false;
  }
  if (cacheResult == TracingCaches::GetOrPutResult::NewlyAdded) {
    outOfLineData_->beginWritingEntry();
    outOfLineData_->write(uint8_t(OutOfLineEntryType::Shape));
    outOfLineData_->write(shapeId);

    uint32_t numProps = 0;
    for (ShapePropertyIter<NoGC> iter(shape); !iter.done(); iter++) {
      if (iter->isCustomDataProperty()) {
        continue;
      }
      numProps += 1;
    }

    outOfLineData_->write(numProps);
    outOfLineData_->writeCString<char, JS::TracerStringEncoding::Latin1>(
        shape->getObjectClass()->name);

    uint32_t countWritten = 0;
    for (ShapePropertyIter<NoGC> iter(shape); !iter.done(); iter++) {
      if (iter->isCustomDataProperty()) {
        continue;
      }
      PropertyKey key = iter->key();
      if (key.isVoid()) {
        outOfLineData_->write(uint8_t(PropertyKeyKind::Undefined));
      } else if (key.isInt()) {
        outOfLineData_->write(uint8_t(PropertyKeyKind::Int));
        outOfLineData_->write(key.toInt());
      } else if (key.isSymbol()) {
        outOfLineData_->write(uint8_t(PropertyKeyKind::Symbol));
        JS::Rooted<JSString*> str(cx, key.toSymbol()->description());
        if (str) {
          if (!outOfLineData_->writeSmallString(cx, str)) {
            return false;
          }
        } else {
          outOfLineData_
              ->writeSmallCString<char, JS::TracerStringEncoding::Latin1>(
                  "<unknown>");
        }
      } else if (key.isString()) {
        outOfLineData_->write(uint8_t(PropertyKeyKind::String));
        JS::Rooted<JSString*> str(cx, key.toString());
        if (!outOfLineData_->writeSmallString(cx, str)) {
          return false;
        }
      }
      if (++countWritten >= JS::ValueSummary::MAX_COLLECTION_VALUES) {
        break;
      }
    }
    outOfLineData_->finishWritingEntry();
  }

  valueData_->write(shapeId);
  return true;
}

bool ValueSummaries::writeErrorObjectSummary(JSContext* cx,
                                             JS::Handle<JSObject*> obj,
                                             JS::Handle<ErrorObject*> error,
                                             IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::Error, 0);

  JS::Rooted<Shape*> shape(cx, obj->shape());
  if (!writeMinimalShapeSummary(cx, shape)) {
    return false;
  }

  JS::Rooted<JS::Value> nameVal(cx);
  JS::Rooted<JSString*> name(cx);
  if (!GetProperty(cx, obj, obj, cx->names().name, &nameVal) ||
      !(name = ToString<CanGC>(cx, nameVal))) {
    valueData_->writeEmptySmallString();
  } else {
    if (!valueData_->writeSmallString(cx, name)) {
      return false;
    }
  }

  JS::Rooted<JSString*> message(cx, error->getMessage());
  if (message) {
    if (!valueData_->writeSmallString(cx, message)) {
      return false;
    }
  } else {
    valueData_->writeEmptySmallString();
  }

  JS::Rooted<JSObject*> stack(cx, error->stack());
  if (stack) {
    JSPrincipals* principals =
        JS::GetRealmPrincipals(js::GetNonCCWObjectRealm(stack));
    JS::Rooted<JSString*> formattedStack(cx);
    if (!JS::BuildStackString(cx, principals, stack, &formattedStack)) {
      return false;
    }
    if (!valueData_->writeSmallString(cx, formattedStack)) {
      return false;
    }
  } else {
    valueData_->writeEmptySmallString();
  }

  JS::Rooted<JSString*> filename(cx, error->fileName(cx));
  if (filename) {
    if (!valueData_->writeSmallString(cx, filename)) {
      return false;
    }
  } else {
    valueData_->writeEmptySmallString();
  }

  valueData_->write((uint32_t)error->lineNumber());

  valueData_->write((uint32_t)error->columnNumber().oneOriginValue());

  return true;
}

bool ValueSummaries::writeMinimalShapeSummary(JSContext* cx,
                                              JS::Handle<Shape*> shape) {
  TracingCaches& caches = cx->caches().tracingCaches;

  uint32_t shapeId = 0;
  TracingCaches::GetOrPutResult cacheResult =
      caches.getOrPutShape(shape, &shapeId);
  if (cacheResult == TracingCaches::GetOrPutResult::OOM) {
    ReportOutOfMemory(cx);
    return false;
  }
  if (cacheResult == TracingCaches::GetOrPutResult::NewlyAdded) {
    outOfLineData_->beginWritingEntry();
    outOfLineData_->write(uint8_t(OutOfLineEntryType::Shape));
    outOfLineData_->write(shapeId);

    outOfLineData_->write(uint32_t(0));  // numProps
    outOfLineData_->writeCString<char, JS::TracerStringEncoding::Latin1>(
        shape->getObjectClass()->name);

    outOfLineData_->finishWritingEntry();
  }

  valueData_->write(shapeId);
  return true;
}

void ValueSummaries::writeObjectHeader(JS::ObjectSummary::Kind kind,
                                       uint8_t flags) {
  writeHeader(JS::ValueType::Object, flags);
  JS::ObjectSummary header;
  header.kind = kind;
  valueData_->writeBytes(reinterpret_cast<const uint8_t*>(&header),
                         sizeof(header));
}

bool ValueSummaries::writeFunctionSummary(JSContext* cx,
                                          JS::Handle<JSFunction*> fn,
                                          IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::Function, 0);

  JS::Rooted<JSAtom*> functionName(cx);
  if (!GetFunctionName(cx, fn, &functionName)) {
    return false;
  }

  if (functionName) {
    if (!valueData_->writeSmallString(cx, functionName)) {
      return false;
    }
  } else {
    valueData_->writeEmptySmallString();
  }

  JS::Rooted<ArrayObject*> parameterNames(
      cx, GetFunctionParameterNamesArray(cx, fn));
  if (!parameterNames) {
    return false;
  }

  uint32_t length = parameterNames->length();

  valueData_->write(length);
  if (length > JS::ValueSummary::MAX_COLLECTION_VALUES) {
    length = JS::ValueSummary::MAX_COLLECTION_VALUES;
  }
  MOZ_RELEASE_ASSERT(parameterNames->getDenseInitializedLength() >= length);

  for (uint32_t i = 0; i < length; ++i) {
    if (parameterNames->getDenseElement(i).isString()) {
      JS::Rooted<JSString*> str(cx,
                                parameterNames->getDenseElement(i).toString());
      if (!valueData_->writeSmallString(cx, str)) {
        return false;
      }
    } else {
      valueData_->writeEmptySmallString();
    }
  }

  return true;
}

bool ValueSummaries::writeArrayObjectSummary(JSContext* cx,
                                             JS::Handle<ArrayObject*> arr,
                                             IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::ArrayLike, 0);

  JS::Rooted<Shape*> shape(cx, arr->shape());
  if (!writeMinimalShapeSummary(cx, shape)) {
    return false;
  }

  size_t length = arr->length();
  MOZ_ASSERT(length == uint32_t(length));
  valueData_->write(uint32_t(length));

  if (nested == IsNested::Yes) {
    return true;
  }

  size_t initlen = arr->getDenseInitializedLength();
  for (uint32_t i = 0;
       i < initlen && i < JS::ValueSummary::MAX_COLLECTION_VALUES; ++i) {
    JS::Rooted<JS::Value> rv(cx, arr->getDenseElement(i));
    if (!writeValue(cx, rv, IsNested::Yes)) {
      return false;
    }
  }

  for (uint32_t i = initlen;
       i < length && i < JS::ValueSummary::MAX_COLLECTION_VALUES; ++i) {
    // Write holes into the array to fill out the discrepancy between the
    // length and the dense initialized length.
    writeHeader(JS::ValueType::Magic, 0);
  }

  return true;
}

bool ValueSummaries::writeSetObjectSummary(JSContext* cx,
                                           JS::Handle<SetObject*> obj,
                                           IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::ArrayLike, 0);

  JS::Rooted<Shape*> shape(cx, obj->shape());
  if (!writeMinimalShapeSummary(cx, shape)) {
    return false;
  }

  JS::Rooted<GCVector<JS::Value>> keys(cx, GCVector<JS::Value>(cx));
  if (!obj->keys(&keys)) {
    return false;
  }

  valueData_->write(uint32_t(keys.length()));

  if (nested == IsNested::Yes) {
    return true;
  }

  for (size_t i = 0;
       i < keys.length() && i < JS::ValueSummary::MAX_COLLECTION_VALUES; ++i) {
    JS::Rooted<JS::Value> val(cx, keys[i]);
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  }

  return true;
}

bool ValueSummaries::writeMapObjectSummary(JSContext* cx,
                                           JS::Handle<MapObject*> obj,
                                           IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::MapLike, 0);

  JS::Rooted<Shape*> shape(cx, obj->shape());
  if (!writeMinimalShapeSummary(cx, shape)) {
    return false;
  }

  valueData_->write(obj->size());

  if (nested == IsNested::Yes) {
    return true;
  }

  JS::Rooted<JS::Value> iter(cx);
  if (!JS::MapEntries(cx, obj, &iter)) {
    return false;
  }
  JS::Rooted<js::MapIteratorObject*> miter(
      cx, &iter.toObject().as<js::MapIteratorObject>());
  JS::Rooted<ArrayObject*> entryPair(
      cx,
      static_cast<ArrayObject*>(js::MapIteratorObject::createResultPair(cx)));
  if (!entryPair) {
    return false;
  }

  uint32_t count = 0;
  while (!js::MapIteratorObject::next(miter, entryPair)) {
    JS::Rooted<JS::Value> key(cx, entryPair->getDenseElement(0));
    JS::Rooted<JS::Value> val(cx, entryPair->getDenseElement(1));
    if (!writeValue(cx, key, IsNested::Yes)) {
      return false;
    }

    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }

    if (++count >= JS::ValueSummary::MAX_COLLECTION_VALUES) {
      break;
    }
  }

  return true;
}

bool ValueSummaries::writeGenericOrWrappedPrimitiveObjectSummary(
    JSContext* cx, JS::Handle<NativeObject*> nobj, IsNested nested) {
  uint8_t flags = 0;
  if (nobj->getDenseInitializedLength() > 0) {
    flags |= JS::ValueSummary::GENERIC_OBJECT_HAS_DENSE_ELEMENTS;
  }

  if (nobj->is<StringObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::WrappedPrimitiveObject, flags);
    JS::Rooted<JS::Value> val(cx,
                              StringValue(nobj->as<StringObject>().unbox()));
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  } else if (nobj->is<BooleanObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::WrappedPrimitiveObject, flags);
    JS::Rooted<JS::Value> val(cx,
                              BooleanValue(nobj->as<BooleanObject>().unbox()));
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  } else if (nobj->is<NumberObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::WrappedPrimitiveObject, flags);
    JS::Rooted<JS::Value> val(cx,
                              NumberValue(nobj->as<NumberObject>().unbox()));
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  } else if (nobj->is<SymbolObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::WrappedPrimitiveObject, flags);
    JS::Rooted<JS::Value> val(cx,
                              SymbolValue(nobj->as<SymbolObject>().unbox()));
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  } else if (nobj->is<BigIntObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::WrappedPrimitiveObject, flags);
    JS::Rooted<JS::Value> val(cx,
                              BigIntValue(nobj->as<BigIntObject>().unbox()));
    if (!writeValue(cx, val, IsNested::Yes)) {
      return false;
    }
  } else {
    writeObjectHeader(JS::ObjectSummary::Kind::GenericObject, flags);
  }

  JS::Rooted<NativeShape*> shape(cx, nobj->shape());
  if (!writeShapeSummary(cx, shape)) {
    return false;
  }

  uint32_t numProps = 0;
  for (ShapePropertyIter<NoGC> iter(shape); !iter.done(); iter++) {
    if (iter->isCustomDataProperty()) {
      continue;
    }
    numProps += 1;
  }
  valueData_->write(numProps);

  if (nested == IsNested::No) {
    size_t countWritten = 0;
    for (ShapePropertyIter<CanGC> iter(cx, nobj->shape()); !iter.done();
         iter++) {
      if (iter->isCustomDataProperty()) {
        continue;
      }

      if (iter->isDataProperty()) {
        JS::Rooted<JS::Value> rv(cx, nobj->getSlot(iter->slot()));
        if (!writeValue(cx, rv, IsNested::Yes)) {
          return false;
        }
      } else {
        valueData_->write(JS::ObjectSummary::GETTER_SETTER_MAGIC);
        MOZ_ASSERT(iter->isAccessorProperty());
        JS::Rooted<JS::Value> getter(cx, nobj->getGetterValue(*iter));
        if (!writeValue(cx, getter, IsNested::Yes)) {
          return false;
        }
        JS::Rooted<JS::Value> setter(cx, nobj->getSetterValue(*iter));
        if (!writeValue(cx, setter, IsNested::Yes)) {
          return false;
        }
      }

      if (++countWritten >= JS::ValueSummary::MAX_COLLECTION_VALUES) {
        break;
      }
    }
  }

  // If this condition is true, GENERIC_OBJECT_HAS_DENSE_ELEMENTS will have
  // been set on the ValueSummary flags, allowing the reader to know to expect
  // an array of additional values here.
  if (nobj->getDenseInitializedLength() > 0) {
    size_t initlen = nobj->getDenseInitializedLength();
    MOZ_ASSERT(initlen == uint32_t(initlen));
    valueData_->write(uint32_t(initlen));

    if (nested == IsNested::No) {
      for (uint32_t i = 0;
           i < initlen && i < JS::ValueSummary::MAX_COLLECTION_VALUES; ++i) {
        JS::Rooted<JS::Value> rv(cx, nobj->getDenseElement(i));
        if (!writeValue(cx, rv, IsNested::Yes)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool ValueSummaries::writeExternalObjectSummary(JSContext* cx,
                                                JS::Handle<NativeObject*> obj,
                                                IsNested nested) {
  writeObjectHeader(JS::ObjectSummary::Kind::External, 0);

  JS::Rooted<Shape*> shape(cx, obj->shape());
  if (!writeMinimalShapeSummary(cx, shape)) {
    return false;
  }

  // Save space for the external size written, which we'll populate after
  // calling the callback.
  uint64_t externalSizeOffset = valueData_->uncommittedWriteHead();
  valueData_->write(uint32_t(0));

  JS_TracerSummaryWriterImpl writerImpl = {this};
  JS_TracerSummaryWriter writer = {&writerImpl};
  CustomObjectSummaryCallback cb = cx->getCustomObjectSummaryCallback();
  MOZ_ASSERT(cb);
  if (!cb(cx, obj, nested == IsNested::Yes, &writer)) {
    return false;
  }

  uint64_t amountWritten64 =
      valueData_->uncommittedWriteHead() - externalSizeOffset;
  MOZ_ASSERT(amountWritten64 + sizeof(uint32_t) < ValueDataBuffer::SIZE);
  uint32_t amountWritten = uint32_t(amountWritten64);

  valueData_->writeAtOffset(amountWritten, externalSizeOffset);

  return true;
}

bool ValueSummaries::writeObject(JSContext* cx, JS::Handle<JSObject*> obj,
                                 IsNested nested) {
  if (obj->is<JSFunction>()) {
    JS::Rooted<JSFunction*> typed(cx, &obj->as<JSFunction>());
    if (!writeFunctionSummary(cx, typed, nested)) {
      return false;
    }
  } else if (obj->is<ArrayObject>()) {
    JS::Rooted<ArrayObject*> typed(cx, &obj->as<ArrayObject>());
    if (!writeArrayObjectSummary(cx, typed, nested)) {
      return false;
    }
  } else if (obj->is<SetObject>()) {
    JS::Rooted<SetObject*> typed(cx, &obj->as<SetObject>());
    if (!writeSetObjectSummary(cx, typed, nested)) {
      return false;
    }
  } else if (obj->is<MapObject>()) {
    JS::Rooted<MapObject*> typed(cx, &obj->as<MapObject>());
    if (!writeMapObjectSummary(cx, typed, nested)) {
      return false;
    }
  } else if (obj->is<ErrorObject>()) {
    JS::Rooted<ErrorObject*> typed(cx, &obj->as<ErrorObject>());
    if (!writeErrorObjectSummary(cx, obj, typed, nested)) {
      return false;
    }
  } else if (obj->is<NativeObject>()) {
    JS::Rooted<NativeObject*> nobj(cx, &obj->as<NativeObject>());

    // TODO: see the comment in Debug.h for Kind::External
    if (cx->getCustomObjectSummaryCallback() &&
        nobj->shape()->getObjectClass()->flags & JSCLASS_IS_DOMJSCLASS) {
      if (!writeExternalObjectSummary(cx, nobj, nested)) {
        return false;
      }
    } else {
      if (!writeGenericOrWrappedPrimitiveObjectSummary(cx, nobj, nested)) {
        return false;
      }
    }
  } else if (obj->is<ProxyObject>()) {
    writeObjectHeader(JS::ObjectSummary::Kind::ProxyObject, 0);
    JS::Rooted<Shape*> shape(cx, obj->shape());
    if (!writeMinimalShapeSummary(cx, shape)) {
      return false;
    }
  } else {
    writeObjectHeader(JS::ObjectSummary::Kind::NotImplemented, 0);
    JS::Rooted<Shape*> shape(cx, obj->shape());
    if (!writeMinimalShapeSummary(cx, shape)) {
      return false;
    }
  }

  return true;
}

bool ValueSummaries::writeArguments(JSContext* cx, AbstractFramePtr frame,
                                    uint64_t* valueBufferIndex) {
  uint32_t argc = frame.numActualArgs();

  valueData_->beginWritingEntry();
  *valueBufferIndex = valueData_->uncommittedWriteHead();

  if (argc > JS::ExecutionTrace::MAX_ARGUMENTS_TO_RECORD) {
    argc = JS::ExecutionTrace::MAX_ARGUMENTS_TO_RECORD;
  }
  valueData_->write(argc);

  for (uint32_t i = 0;
       i < argc && i < JS::ExecutionTrace::MAX_ARGUMENTS_TO_RECORD; ++i) {
    Rooted<JS::Value> val(cx, frame.argv()[i]);
    if (!writeValue(cx, val, IsNested::No)) {
      return false;
    }
  }
  valueData_->finishWritingEntry();

  return true;
}

bool ValueSummaries::populateOutputBuffer(
    JS::ExecutionTrace::TracedJSContext& context) {
  size_t valueBytes =
      valueData_->uncommittedWriteHead() - valueData_->readHead();
  if (!context.valueBuffer.initLengthUninitialized(
          valueBytes + sizeof(JS::ValueSummary::VERSION))) {
    return false;
  }
  uint32_t version =
      mozilla::NativeEndian::swapToLittleEndian(JS::ValueSummary::VERSION);
  memcpy(context.valueBuffer.begin(), &version, sizeof(version));

  valueData_->readBytes(
      context.valueBuffer.begin() + sizeof(JS::ValueSummary::VERSION),
      valueBytes);
  return true;
}

int32_t ValueSummaries::getOutputBufferIndex(uint64_t argumentsIndex) {
  if (argumentsIndex > valueData_->readHead()) {
    MOZ_ASSERT(argumentsIndex - valueData_->readHead() <
               std::numeric_limits<int32_t>::max() - sizeof(uint32_t) -
                   sizeof(JS::ValueSummary::VERSION));
    return int32_t(argumentsIndex - valueData_->readHead() +
                   sizeof(JS::ValueSummary::VERSION));
  }

  return JS::ExecutionTrace::EXPIRED_VALUES_MAGIC;
}

bool ValueSummaries::writeStringLikeValue(JSContext* cx,
                                          JS::ValueType valueType,
                                          JS::Handle<JSString*> str) {
  writeHeader(valueType, 0);
  return valueData_->writeSmallString(cx, str);
}

bool ValueSummaries::writeValue(JSContext* cx, JS::Handle<JS::Value> val,
                                IsNested nested) {
  switch (val.type()) {
    case JS::ValueType::Double:
      if (mozilla::IsPositiveZero(val.toDouble())) {
        writeHeader(JS::ValueType::Double, 0);
      } else {
        writeHeader(JS::ValueType::Double,
                    JS::ValueSummary::NUMBER_IS_OUT_OF_LINE_MAGIC);
        valueData_->write(val.toDouble());
      }
      return true;
    case JS::ValueType::Int32: {
      int32_t intVal = val.toInt32();
      if (intVal > JS::ValueSummary::MAX_INLINE_INT ||
          intVal < JS::ValueSummary::MIN_INLINE_INT) {
        writeHeader(JS::ValueType::Int32,
                    JS::ValueSummary::NUMBER_IS_OUT_OF_LINE_MAGIC);
        valueData_->write(val.toInt32());
      } else {
        writeHeader(JS::ValueType::Int32,
                    intVal - JS::ValueSummary::MIN_INLINE_INT);
      }
      return true;
    }
    case JS::ValueType::Boolean:
      writeHeader(JS::ValueType::Boolean, uint8_t(val.toBoolean()));
      return true;
    case JS::ValueType::Magic:
      // The one kind of magic we can actually see is a hole in the dense
      // elements of an object, which will need to be specially interpreted
      // as such by the reader.
      MOZ_ASSERT(val.isMagic(JSWhyMagic::JS_ELEMENTS_HOLE));
      writeHeader(JS::ValueType::Magic, 0);
      return true;
    case JS::ValueType::Undefined:
      writeHeader(JS::ValueType::Undefined, 0);
      return true;
    case JS::ValueType::Null:
      writeHeader(JS::ValueType::Null, 0);
      return true;
    case JS::ValueType::BigInt: {
      JS::Rooted<JS::BigInt*> bi(cx, val.toBigInt());
      JS::Rooted<JSString*> str(cx, BigInt::toString<CanGC>(cx, bi, 10));
      if (!str) {
        return false;
      }
      return writeStringLikeValue(cx, JS::ValueType::BigInt, str);
    }
    case JS::ValueType::Symbol: {
      JS::Rooted<JSString*> str(cx, val.toSymbol()->description());
      if (!str) {
        writeHeader(JS::ValueType::Symbol,
                    JS::ValueSummary::SYMBOL_NO_DESCRIPTION);
        return true;
      }
      return writeStringLikeValue(cx, JS::ValueType::Symbol, str);
    }
    case JS::ValueType::String: {
      JS::Rooted<JSString*> str(cx, val.toString());
      return writeStringLikeValue(cx, JS::ValueType::String, str);
    }
    case JS::ValueType::Object: {
      JS::Rooted<JSObject*> obj(cx, &val.toObject());
      mozilla::Maybe<AutoRealm> ar;
      if (IsCrossCompartmentWrapper(obj)) {
        obj = UncheckedUnwrap(obj, true);
        ar.emplace(cx, obj);
      }
      return writeObject(cx, obj, nested);
    }
    default:
      MOZ_CRASH("Unexpected value type in JS Execution Tracer");
      return false;
  }
}

void JS_TracerSummaryWriter::writeUint8(uint8_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeUint16(uint16_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeUint32(uint32_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeUint64(uint64_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeInt8(int8_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeInt16(int16_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeInt32(int32_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeInt64(int64_t val) {
  impl->valueSummaries->valueData_->write(val);
}

void JS_TracerSummaryWriter::writeUTF8String(const char* val) {
  impl->valueSummaries->valueData_
      ->writeSmallCString<char, JS::TracerStringEncoding::UTF8>(val);
}

void JS_TracerSummaryWriter::writeTwoByteString(const char16_t* val) {
  impl->valueSummaries->valueData_
      ->writeSmallCString<char16_t, JS::TracerStringEncoding::TwoByte>(val);
}

bool JS_TracerSummaryWriter::writeValue(JSContext* cx,
                                        JS::Handle<JS::Value> val) {
  return impl->valueSummaries->writeValue(cx, val,
                                          js::ValueSummaries::IsNested::Yes);
}

void JS_SetCustomObjectSummaryCallback(JSContext* cx,
                                       CustomObjectSummaryCallback callback) {
  cx->setCustomObjectSummaryCallback(callback);
}

void JS_TracerEnterLabelTwoByte(JSContext* cx, const char16_t* label) {
  CHECK_THREAD(cx);
  if (cx->hasExecutionTracer()) {
    cx->getExecutionTracer()
        .onEnterLabel<char16_t, JS::TracerStringEncoding::TwoByte>(label);
  }
}

void JS_TracerEnterLabelLatin1(JSContext* cx, const char* label) {
  CHECK_THREAD(cx);
  if (cx->hasExecutionTracer()) {
    cx->getExecutionTracer()
        .onEnterLabel<char, JS::TracerStringEncoding::Latin1>(label);
  }
}

void JS_TracerLeaveLabelTwoByte(JSContext* cx, const char16_t* label) {
  CHECK_THREAD(cx);
  if (cx->hasExecutionTracer()) {
    cx->getExecutionTracer()
        .onLeaveLabel<char16_t, JS::TracerStringEncoding::TwoByte>(label);
  }
}

void JS_TracerLeaveLabelLatin1(JSContext* cx, const char* label) {
  CHECK_THREAD(cx);
  if (cx->hasExecutionTracer()) {
    cx->getExecutionTracer()
        .onLeaveLabel<char, JS::TracerStringEncoding::Latin1>(label);
  }
}

bool JS_TracerIsTracing(JSContext* cx) { return cx->hasExecutionTracer(); }

bool JS_TracerBeginTracing(JSContext* cx) {
  CHECK_THREAD(cx);
  return cx->enableExecutionTracing();
}

bool JS_TracerEndTracing(JSContext* cx) {
  CHECK_THREAD(cx);
  cx->disableExecutionTracing();
  return true;
}

bool JS_TracerSnapshotTrace(JS::ExecutionTrace& trace) {
  return ExecutionTracer::getNativeTraceForAllContexts(trace);
}
