// Test String.prototype.charAt with out-of-bounds indices.

function* characters(...ranges) {
  for (let [start, end] of ranges) {
    for (let i = start; i <= end; ++i) {
      yield i;
    }
  }
}

const empty = [];

const ascii = [...characters(
  [0x41, 0x5A], // A..Z
  [0x61, 0x7A], // a..z
)];

const latin1 = [...characters(
  [0xC0, 0xFF], // À..ÿ
)];

const twoByte = [...characters(
  [0x100, 0x17E], // Ā..ž
)];

function atomize(s) {
  return Object.keys({[s]: 0})[0];
}

function codePoints() {
  return [empty, ascii, latin1, twoByte];
}

function makeStrings() {
  let strings = codePoints()
  .map(codePoints => String.fromCodePoint(...codePoints))
  .flatMap(x => [
    x,
    newRope(x, ""),
    newString(x, {twoByte: true}),
    atomize(x),
  ]);
  return strings;
}

function testNegativeIndexConstant() {
  let strings = makeStrings();
  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let ch = str.charAt(-1);
    assertEq(ch, "");
  }
}
for (let i = 0; i < 2; ++i) {
  testNegativeIndexConstant();
}

function testNegativeIndexVariable() {
  let indices = [-1, -2];
  let strings = makeStrings();
  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let ch = str.charAt(indices[i & 1]);
    assertEq(ch, "");
  }
}
for (let i = 0; i < 2; ++i) {
  testNegativeIndexVariable();
}

function testNegativeOrValidIndex() {
  let indices = [-1, 0];
  let strings = makeStrings();

  // Number of string kinds created in makeStrings.
  const N = 4;

  let cpoints = codePoints();
  assertEq(strings.length, cpoints.length * N);

  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let index = indices[i & 1];
    let ch = str.charAt(index);

    let cp = cpoints[Math.trunc((i % strings.length) / N)];
    assertEq(ch, (0 <= index && index < cp.length ? String.fromCodePoint(cp[index]) : ""));
  }
}
for (let i = 0; i < 2; ++i) {
  testNegativeOrValidIndex();
}

function testTooLargeIndexConstant() {
  let strings = makeStrings();
  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let ch = str.charAt(1000);
    assertEq(ch, "");
  }
}
for (let i = 0; i < 2; ++i) {
  testTooLargeIndexConstant();
}

function testTooLargeIndexVariable() {
  let indices = [1000, 2000];
  let strings = makeStrings();
  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let ch = str.charAt(indices[i & 1]);
    assertEq(ch, "");
  }
}
for (let i = 0; i < 2; ++i) {
  testTooLargeIndexVariable();
}

function testTooLargeOrValidIndex() {
  let indices = [1000, 0];
  let strings = makeStrings();

  // Number of string kinds created in makeStrings.
  const N = 4;

  let cpoints = codePoints();
  assertEq(strings.length, cpoints.length * N);

  for (let i = 0; i < 200; ++i) {
    let str = strings[i % strings.length];
    let index = indices[i & 1];
    let ch = str.charAt(index);

    let cp = cpoints[Math.trunc((i % strings.length) / N)];
    assertEq(ch, (0 <= index && index < cp.length ? String.fromCodePoint(cp[index]) : ""));
  }
}
for (let i = 0; i < 2; ++i) {
  testTooLargeOrValidIndex();
}
