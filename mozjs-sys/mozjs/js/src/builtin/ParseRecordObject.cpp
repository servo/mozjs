/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/ParseRecordObject.h"

#include "builtin/Object.h"
#include "js/PropertyAndElement.h"  // JS_SetPropertyById
#include "vm/PlainObject.h"

#include "vm/JSObject-inl.h"  // NewBuiltinClassInstance

using namespace js;

// https://tc39.es/proposal-json-parse-with-source/#sec-json-parse-record

const JSClass ParseRecordObject::class_ = {
    "ParseRecordObject",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount),
};

/* static */
ParseRecordObject* ParseRecordObject::create(JSContext* cx, const Value& val) {
  Rooted<JSONParseNode*> parseNode(cx);
  return ParseRecordObject::create(cx, parseNode, val);
}

/* static */
ParseRecordObject* ParseRecordObject::create(JSContext* cx,
                                             Handle<JSONParseNode*> parseNode,
                                             const Value& val) {
  auto* obj = NewObjectWithGivenProto<ParseRecordObject>(cx, nullptr);
  if (!obj) {
    return nullptr;
  }

  if (parseNode) {
    obj->initReservedSlot(ParseNodeSlot, StringValue(parseNode));
  }
  obj->initReservedSlot(ValueSlot, val);
  return obj;
}

bool ParseRecordObject::addEntries(JSContext* cx, Handle<JS::PropertyKey> key,
                                   Handle<ParseRecordObject*> parseRecord) {
  Rooted<Value> pro(cx, ObjectValue(*parseRecord));
  Rooted<JSObject*> obj(cx, this);
  return JS_SetPropertyById(cx, obj, key, pro);
}
