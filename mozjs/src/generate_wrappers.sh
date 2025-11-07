#!/bin/sh
# Detect gsed or sed
gsed=$(type gsed >/dev/null 2>&1 && echo gsed || echo sed)
# Detect gfind or find
gfind=$(type gfind >/dev/null 2>&1 && echo gfind || echo find)

grep_functions() {
  grep -v "link_name" "$1" | \
  grep -v '"\]' | \
  grep -F -v '/\*\*' | \
  $gsed -z 's/,\n */, /g' | \
  $gsed -z 's/:\n */: /g' | \
  $gsed -z 's/\n *->/ ->/g' | \
  grep -v '^\}$' | \
  $gsed 's/^ *pub/pub/' | \
  $gsed -z 's/\;\n/\n/g' | \
  grep 'pub fn'
}

# This is one big heuristic but seems to work well enough
grep_heur() {
  grep_functions "$1" |
  grep -e Handle | \
  grep -v roxyHandler | \
  grep -v '\bIdVector\b' | # name clash between rust::IdVector and JS::IdVector \
  grep -v 'pub fn Unbox' | # this function seems to be platform specific \
  grep -v 'CopyAsyncStack' | # arch-specific bindgen output
  $gsed 's/root:://g' |
  $gsed 's/JS:://g' |
  $gsed 's/js:://g' |
  $gsed 's/mozilla:://g' |
  $gsed 's/Handle<\*mut JSObject>/HandleObject/g' |
  grep -F -v '> HandleObject' | # We are only wrapping handles in args not in results
  grep -F -v '> HandleValue' |
  grep -F -v '> HandleString' |
  grep -v 'MutableHandleObjectVector' # GetDebuggeeGlobals has it
}

# usage find_latest_version_of_file_and_parse $input_file $out_wrapper_module_name $heur_fn
find_latest_version_of_file_and_parse() {
  # clone file and reformat (this is needed for grep_heur to work properly)
  # this $(find) only gets last modified file
  cp $($gfind target -name "$1" -printf "%T@ %p\n" | sort -n | tail -n 1 | tr ' ' '\n' | tail -n 1) "target/wrap_$1"
  rustfmt "target/wrap_$1" --config max_width=1000
  
  # parse reformated file
  ($3 "target/wrap_$1") | $gsed 's/\(.*\)/wrap!('"$2"': \1);/g'  > "mozjs/src/$2$4_wrappers.in.rs"
}

find_latest_version_of_file_and_parse jsapi.rs jsapi grep_heur
find_latest_version_of_file_and_parse gluebindings.rs glue grep_heur

# This is one big heuristic but seems to work well enough
grep_heur2() {
  grep_functions "$1" |
  grep -e Handle -e JSContext | \
  grep -v roxyHandler | \
  grep -v '\bIdVector\b' | # name clash between rust::IdVector and JS::IdVector \
  # platform specific
  grep -v 'pub fn Unbox' |
  grep -v 'CopyAsyncStack' |
  grep -F -v 'Opaque' |
  grep -v 'pub fn JS_WrapPropertyDescriptor1' |
  grep -v 'pub fn EncodeWideToUtf8' |
  grep -v 'pub fn JS_NewContext' | # returns jscontext
  # gc module causes problems in macro
  grep -v 'pub fn NewMemoryInfo' |
  grep -v 'pub fn GetGCContext' |
  grep -v 'pub fn SetDebuggerMalloc' |
  grep -v 'pub fn GetDebuggerMallocSizeOf' |
  grep -v 'pub fn FireOnGarbageCollectionHookRequired' |
  grep -v 'pub fn ShouldAvoidSideEffects' |
  # vargs
  grep -F -v '...' |
  grep -F -v 'VA(' |
  $gsed 's/root:://g' |
  $gsed 's/JS:://g' |
  $gsed 's/js:://g' |
  $gsed 's/mozilla:://g' |
  $gsed 's/\*mut JSContext/\&mut JSContext/g' |
  $gsed 's/\*const JSContext/\&JSContext/g' |
  grep -F -v '> Handle' | # We are only wrapping handles in args not in results
  grep -v 'MutableHandleObjectVector' # GetDebuggeeGlobals has it
}

find_latest_version_of_file_and_parse jsapi.rs jsapi grep_heur2 2
find_latest_version_of_file_and_parse gluebindings.rs glue grep_heur2 2

# make functions that do not GC take &JSContext instead of &mut JSContext
mark_as_no_gc() {
  # Functions with AutoRequireNoGC arg do not trigger GC
  sed -i '/\*const AutoRequireNoGC/ s/\&mut JSContext/\&JSContext/' $1
  
  # Functions that also do not trigger GC
  for fn in \
  "JS_GetRuntime" \
  "JS_GetParentRuntime" \
  "JS_GetGCParameter" \
  ; do
    sed -i "/pub fn $fn/ s/\&mut JSContext/\&JSContext/g" $1
  done
}

mark_as_no_gc mozjs/src/jsapi2_wrappers.in.rs