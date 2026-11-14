# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import gdb
import gdb.types


class CellHeaderTypeCache:
    def __init__(self, cache):
        self.AllocKind_t = gdb.lookup_type("js::gc::AllocKind")
        self.Arena_t = gdb.lookup_type("js::gc::Arena")
        self.Cell_t = gdb.lookup_type("js::gc::Cell")
        self.TenuredCell_t = gdb.lookup_type("js::gc::TenuredCell")
        self.arena_mask = gdb.parse_and_eval("js::gc::ArenaMask")

        alloc_kinds = gdb.types.make_enum_dict(self.AllocKind_t)
        self.SYMBOL = alloc_kinds["js::gc::AllocKind::SYMBOL"]


def get_cell_alloc_kind(cell_ptr, cache):
    # Return the AllocKind index of a tenured cell, given a pointer to it as a
    # gdb.Value. This reimplements TenuredCell::getAllocKind().
    if not cache.mod_CellHeader:
        cache.mod_CellHeader = CellHeaderTypeCache(cache)
    types = cache.mod_CellHeader

    # Cell::asTenured()
    cell = cell_ptr.reinterpret_cast(types.Cell_t.pointer())
    tenured = cell.cast(types.TenuredCell_t.pointer())

    # TenuredCell::arena()
    addr = int(tenured)
    arena_ptr = addr & ~int(types.arena_mask)
    arena = gdb.Value(arena_ptr).reinterpret_cast(types.Arena_t.pointer())

    # Arena::getAllocKind()
    alloc_kind = arena["allocKind"].cast(types.AllocKind_t)
    return int(alloc_kind.cast(types.AllocKind_t.target()))


def get_header_ptr(value, ptr_t):
    # Return the pointer stored in Cell::header_ for subclasses of
    # TenuredCellWithNonGCPointer and CellWithTenuredGCPointer.
    return value["header_"]["value_"].cast(ptr_t)


def get_header_length_and_flags(value, cache):
    # Return the length and flags values for subclasses of
    # CellWithLengthAndFlags.
    flags = value["header_"]["value_"].cast(cache.uintptr_t)
    try:
        length = value["length_"]
    except gdb.error:
        # If we couldn't fetch the length directly, it must be stored
        # within `flags`.
        length = flags >> 32
        flags = flags % 2**32
    return length, flags
