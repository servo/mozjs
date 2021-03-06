var BUGNUMBER = 887016;
var summary = "RegExp.prototype[@@search] should check this value.";

print(BUGNUMBER + ": " + summary);

for (var v of [null, 1, true, undefined, "", Symbol.iterator]) {
  assertThrowsInstanceOf(() => RegExp.prototype[Symbol.search].call(v),
                         TypeError);
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
