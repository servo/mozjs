#!/bin/sh
# Detect gsed or sed
gsed=$(type gsed >/dev/null 2>&1 && echo gsed || echo sed)
# This is one big heuristic but seems to work well enough
grep_heur() {
  grep -v "link_name" "$1" | \
  grep -v '"\]' | \
  grep -F -v '/\*\*' | \
  $gsed -z 's/,\n */, /g' | \
  $gsed -z 's/:\n */: /g' | \
  $gsed -z 's/\n *->/ ->/g' | \
  grep -v '^\}$' | \
  $gsed 's/^ *pub/pub/' | \
  $gsed -z 's/\;\n/\n/g' | \
  grep 'pub fn' | \
  grep Handle | \
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
  grep -v 'MutableHandleObjectVector' # GetDebuggeeGlobals has it
}

# usage find_latest_version_of_file_and_parse $input_file $out_wrapper_module_name
find_latest_version_of_file_and_parse() {
  # clone file and reformat (this is needed for grep_heur to work properly)
  # this $(find) only gets last modified file
  cp $(find target -name "$1" -printf "%T@ %p\n" | sort -n | tail -n 1 | tr ' ' '\n' | tail -n 1) "target/wrap_$1"
  rustfmt "target/wrap_$1" --config max_width=1000
  
  # parse reformated file
  grep_heur "target/wrap_$1" | $gsed 's/\(.*\)/wrap!('"$2"': \1);/g'  > "mozjs/src/$2_wrappers.in"
}

find_latest_version_of_file_and_parse jsapi.rs jsapi
find_latest_version_of_file_and_parse gluebindings.rs glue
