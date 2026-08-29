/*
 * Copyright 2023 Mozilla Foundation
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

#include "wasm/WasmStaticTypeDefs.h"

#include "wasm/WasmTypeDef.h"

using namespace js;
using namespace js::wasm;

const TypeDef* StaticTypeDefs::arrayMutI16 = nullptr;
const TypeDef* StaticTypeDefs::jsExceptionTag = nullptr;
#ifdef ENABLE_WASM_JSPI
const TypeDef* StaticTypeDefs::jsPromiseTag = nullptr;
#endif

bool StaticTypeDefs::init() {
  RefPtr<TypeContext> types = js_new<TypeContext>();
  if (!types) {
    return false;
  }

  arrayMutI16 = types->addType(ArrayType(StorageType::I16, true));
  if (!arrayMutI16) {
    return false;
  }
  arrayMutI16->recGroup().AddRef();

  ValTypeVector exceptionParams;
  if (!exceptionParams.append(ValType(RefType::extern_()))) {
    return false;
  }
  jsExceptionTag =
      types->addType(FuncType(std::move(exceptionParams), ValTypeVector()));
  if (!jsExceptionTag) {
    return false;
  }
  jsExceptionTag->recGroup().AddRef();

#ifdef ENABLE_WASM_JSPI
  ValTypeVector promiseParams;
  if (!promiseParams.append(ValType(RefType::extern_()))) {
    return false;
  }
  jsPromiseTag =
      types->addType(FuncType(std::move(promiseParams), ValTypeVector()));
  if (!jsPromiseTag) {
    return false;
  }
  jsPromiseTag->recGroup().AddRef();
#endif

  return true;
}

void StaticTypeDefs::destroy() {
  if (arrayMutI16) {
    arrayMutI16->recGroup().Release();
    arrayMutI16 = nullptr;
  }
  if (jsExceptionTag) {
    jsExceptionTag->recGroup().Release();
    jsExceptionTag = nullptr;
  }
#ifdef ENABLE_WASM_JSPI
  if (jsPromiseTag) {
    jsPromiseTag->recGroup().Release();
    jsPromiseTag = nullptr;
  }
#endif
}

bool StaticTypeDefs::addAllToTypeContext(TypeContext* types) {
  const TypeDef* staticTypes[]{arrayMutI16, jsExceptionTag,
#ifdef ENABLE_WASM_JSPI
                               jsPromiseTag
#endif
  };

  for (const TypeDef* type : staticTypes) {
    MOZ_ASSERT(type, "static TypeDef was not initialized");
    SharedRecGroup recGroup = &type->recGroup();
    MOZ_ASSERT(recGroup->numTypes() == 1);
    if (!types->addRecGroup(recGroup)) {
      return false;
    }
  }
  return true;
}
