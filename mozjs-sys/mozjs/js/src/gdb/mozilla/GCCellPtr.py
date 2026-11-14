# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# Pretty-printers for GCCellPtr values.

import gdb

import mozilla.prettyprinters
from mozilla.CellHeader import get_cell_alloc_kind
from mozilla.prettyprinters import pretty_printer

# Forget any printers from previous loads of this module.
mozilla.prettyprinters.clear_module_printers(__name__)

# Cache information about the types for this objfile.


class GCCellPtrTypeCache:
    def __init__(self, cache):
        self.TraceKind_t = gdb.lookup_type("JS::TraceKind")
        self.AllocKind_t = gdb.lookup_type("js::gc::AllocKind")

        trace_kinds = gdb.types.make_enum_dict(self.TraceKind_t)
        alloc_kinds = gdb.types.make_enum_dict(self.AllocKind_t)

        def trace_kind(k):
            return trace_kinds["JS::TraceKind::" + k]

        def alloc_kind(k):
            return alloc_kinds["js::gc::AllocKind::" + k]

        # Build a mapping from TraceKind enum values to the types they denote.
        trace_map = {
            # Inline types.
            "Object": "JSObject",
            "BigInt": "JS::BigInt",
            "String": "JSString",
            "GetterSetter": "js::GetterSetter",
            "Symbol": "JS::Symbol",
            "Shape": "js::Shape",
            "Null": "std::nullptr_t",
            # Out-of-line types.
            "BaseShape": "js::BaseShape",
            "JitCode": "js::jit::JitCode",
            "Script": "js::BaseScript",
            "Scope": "js::Scope",
            "RegExpShared": "js::RegExpShared",
            "PropMap": "js::PropMap",
        }

        # Map from AllocKind to TraceKind for out-of-line types.
        alloc_map = {
            "BASE_SHAPE": "BaseShape",
            "JITCODE": "JitCode",
            "SCRIPT": "Script",
            "SCOPE": "Scope",
            "REGEXP_SHARED": "RegExpShared",
            "COMPACT_PROP_MAP": "PropMap",
            "NORMAL_PROP_MAP": "PropMap",
            "DICT_PROP_MAP": "PropMap",
        }

        self.trace_kind_to_type = {
            trace_kind(k): gdb.lookup_type(v) for k, v in trace_map.items()
        }
        self.alloc_kind_to_trace_kind = {
            alloc_kind(k): trace_kind(v) for k, v in alloc_map.items()
        }

        self.Null = trace_kind("Null")
        self.tracekind_mask = gdb.parse_and_eval("JS::OutOfLineTraceKindMask")


@pretty_printer("JS::GCCellPtr")
class GCCellPtr:
    def __init__(self, value, cache):
        self.value = value
        if not cache.mod_GCCellPtr:
            cache.mod_GCCellPtr = GCCellPtrTypeCache(cache)
        self.cache = cache

    def to_string(self):
        ptr = self.value["ptr"]
        kind = ptr & self.cache.mod_GCCellPtr.tracekind_mask
        if kind == self.cache.mod_GCCellPtr.Null:
            return "JS::GCCellPtr(nullptr)"
        if kind == self.cache.mod_GCCellPtr.tracekind_mask:
            # Out-of-line trace kinds.
            #
            # Compute the underlying type for out-of-line kinds by
            # reimplementing the GCCellPtr::outOfLineKind() method.

            # GCCellPtr::asCell()
            cell_ptr = ptr & ~self.cache.mod_GCCellPtr.tracekind_mask

            alloc_idx = get_cell_alloc_kind(cell_ptr, self.cache)

            # Map the AllocKind to a TraceKind.
            kind = self.cache.mod_GCCellPtr.alloc_kind_to_trace_kind[alloc_idx]
        type_name = self.cache.mod_GCCellPtr.trace_kind_to_type[int(kind)]
        return f"JS::GCCellPtr(({type_name}*) {ptr.cast(self.cache.void_ptr_t)})"
