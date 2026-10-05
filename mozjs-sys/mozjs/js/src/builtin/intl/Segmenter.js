/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * CreateSegmentDataObject ( segmenter, string, startIndex, endIndex )
 */
function CreateSegmentDataObject(string, boundaries) {
  assert(typeof string === "string", "CreateSegmentDataObject");
  assert(
    IsPackedArray(boundaries) && boundaries.length === 3,
    "CreateSegmentDataObject"
  );

  var startIndex = boundaries[0];
  assert(
    typeof startIndex === "number" && (startIndex | 0) === startIndex,
    "startIndex is an int32-value"
  );

  var endIndex = boundaries[1];
  assert(
    typeof endIndex === "number" && (endIndex | 0) === endIndex,
    "endIndex is an int32-value"
  );

  // In our implementation |granularity| is encoded in |isWordLike|.
  var isWordLike = boundaries[2];
  assert(
    typeof isWordLike === "boolean" || isWordLike === undefined,
    "isWordLike is either a boolean or undefined"
  );

  // Step 1 (Not applicable).

  // Step 2.
  assert(startIndex >= 0, "startIndex is a positive number");

  // Step 3.
  assert(
    endIndex <= string.length,
    "endIndex is less-than-equals the string length"
  );

  // Step 4.
  assert(startIndex < endIndex, "startIndex is strictly less than endIndex");

  // Step 6.
  var segment = Substring(string, startIndex, endIndex - startIndex);

  // Steps 5, 7-12.
  if (isWordLike === undefined) {
    return {
      segment,
      index: startIndex,
      input: string,
    };
  }

  return {
    segment,
    index: startIndex,
    input: string,
    isWordLike,
  };
}

/**
 * %Segments.prototype%.containing ( index )
 *
 * Return a Segment Data object describing the segment at the given index. If
 * the index exceeds the string bounds, undefined is returned.
 */
function Intl_Segments_containing(index) {
  // Step 1.
  var segments = this;

  // Step 2.
  if (
    !IsObject(segments) ||
    (segments = intl_GuardToSegments(segments)) === null
  ) {
    return callFunction(
      intl_CallSegmentsMethodIfWrapped,
      this,
      index,
      "Intl_Segments_containing"
    );
  }

  // Step 3 (not applicable).

  // Step 4.
  var string = UnsafeGetStringFromReservedSlot(
    segments,
    INTL_SEGMENTS_STRING_SLOT
  );

  // Step 5.
  var len = string.length;

  // Step 6.
  var n = ToInteger(index);

  // Step 7.
  if (n < 0 || n >= len) {
    return undefined;
  }

  // Steps 8-9.
  var boundaries = intl_FindSegmentBoundaries(segments, n | 0);

  // Step 10.
  return CreateSegmentDataObject(string, boundaries);
}

/**
 * %Segments.prototype% [ @@iterator ] ()
 *
 * Create a new Segment Iterator object.
 */
function Intl_Segments_iterator() {
  // Step 1.
  var segments = this;

  // Step 2.
  if (
    !IsObject(segments) ||
    (segments = intl_GuardToSegments(segments)) === null
  ) {
    return callFunction(
      intl_CallSegmentsMethodIfWrapped,
      this,
      "Intl_Segments_iterator"
    );
  }

  // Steps 3-5.
  return intl_CreateSegmentIterator(segments);
}

/**
 * %SegmentIterator.prototype%.next ()
 *
 * Advance the Segment iterator to the next segment within the string.
 */
function Intl_SegmentIterator_next() {
  // Step 1.
  var iterator = this;

  // Step 2.
  if (
    !IsObject(iterator) ||
    (iterator = intl_GuardToSegmentIterator(iterator)) === null)
  {
    return callFunction(
      intl_CallSegmentIteratorMethodIfWrapped,
      this,
      "Intl_SegmentIterator_next"
    );
  }

  // Step 3 (Not applicable).

  // Step 4.
  var string = UnsafeGetStringFromReservedSlot(
    iterator,
    INTL_SEGMENT_ITERATOR_STRING_SLOT
  );

  // Step 5.
  var index = UnsafeGetInt32FromReservedSlot(
    iterator,
    INTL_SEGMENT_ITERATOR_INDEX_SLOT
  );

  var result = { value: undefined, done: false };

  // Step 7.
  if (index === string.length) {
    result.done = true;
    return result;
  }

  // Steps 6, 8.
  var boundaries = intl_FindNextSegmentBoundaries(iterator);

  // Step 9.
  result.value = CreateSegmentDataObject(string, boundaries);

  // Step 10.
  return result;
}
