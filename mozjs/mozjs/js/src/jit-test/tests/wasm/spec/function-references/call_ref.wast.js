/* Copyright 2021 Mozilla Foundation
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

// ./test/core/call_ref.wast

// ./test/core/call_ref.wast:1
let $0 = instantiate(`(module
  (type $$ii (func (param i32) (result i32)))

  (func $$apply (param $$f (ref $$ii)) (param $$x i32) (result i32)
    (call_ref $$ii (local.get $$x) (local.get $$f))
  )

  (func $$f (type $$ii) (i32.mul (local.get 0) (local.get 0)))
  (func $$g (type $$ii) (i32.sub (i32.const 0) (local.get 0)))

  (elem declare func $$f $$g)

  (func (export "run") (param $$x i32) (result i32)
    (local $$rf (ref null $$ii))
    (local $$rg (ref null $$ii))
    (local.set $$rf (ref.func $$f))
    (local.set $$rg (ref.func $$g))
    (call_ref $$ii (call_ref $$ii (local.get $$x) (local.get $$rf)) (local.get $$rg))
  )

  (func (export "null") (result i32)
    (call_ref $$ii (i32.const 1) (ref.null $$ii))
  )
)`);

// ./test/core/call_ref.wast:94
assert_return(() => invoke($0, `run`, [0]), [value("i32", 0)]);

// ./test/core/call_ref.wast:95
assert_return(() => invoke($0, `run`, [3]), [value("i32", -9)]);

// ./test/core/call_ref.wast:97
assert_trap(() => invoke($0, `null`, []), `null function`);

// ./test/core/call_ref.wast:129
let $1 = instantiate(`(module
  (type $$t (func))
  (func (export "unreachable") (result i32)
    (unreachable)
    (call_ref $$t)
  )
)`);

// ./test/core/call_ref.wast:136
assert_trap(() => invoke($1, `unreachable`, []), `unreachable`);

// ./test/core/call_ref.wast:138
let $2 = instantiate(`(module
  (elem declare func $$f)
  (type $$t (func (param i32) (result i32)))
  (func $$f (param i32) (result i32) (local.get 0))

  (func (export "unreachable") (result i32)
    (unreachable)
    (ref.func $$f)
    (call_ref $$t)
  )
)`);

// ./test/core/call_ref.wast:149
assert_trap(() => invoke($2, `unreachable`, []), `unreachable`);

// ./test/core/call_ref.wast:151
let $3 = instantiate(`(module
  (elem declare func $$f)
  (type $$t (func (param i32) (result i32)))
  (func $$f (param i32) (result i32) (local.get 0))

  (func (export "unreachable") (result i32)
    (unreachable)
    (i32.const 0)
    (ref.func $$f)
    (call_ref $$t)
    (drop)
    (i32.const 0)
  )
)`);

// ./test/core/call_ref.wast:165
assert_trap(() => invoke($3, `unreachable`, []), `unreachable`);

// ./test/core/call_ref.wast:167
assert_invalid(
  () => instantiate(`(module
    (elem declare func $$f)
    (type $$t (func (param i32) (result i32)))
    (func $$f (param i32) (result i32) (local.get 0))

    (func (export "unreachable") (result i32)
      (unreachable)
      (i64.const 0)
      (ref.func $$f)
      (call_ref $$t)
    )
  )`),
  `type mismatch`,
);

// ./test/core/call_ref.wast:183
assert_invalid(
  () => instantiate(`(module
    (elem declare func $$f)
    (type $$t (func (param i32) (result i32)))
    (func $$f (param i32) (result i32) (local.get 0))

    (func (export "unreachable") (result i32)
      (unreachable)
      (ref.func $$f)
      (call_ref $$t)
      (drop)
      (i64.const 0)
    )
  )`),
  `type mismatch`,
);

// ./test/core/call_ref.wast:200
assert_invalid(
  () => instantiate(`(module
    (type $$t (func))
    (func $$f (param $$r externref)
      (call_ref $$t (local.get $$r))
    )
  )`),
  `type mismatch`,
);
