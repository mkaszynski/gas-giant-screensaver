#pragma once
#include <cassert>
#include <cmath>
#include <limits>

namespace observatory::detail {
// Grid construction already bounds corners to [-extent, extent]. Avoid two
// integer divisions for every interpolation tap in that known interval.
inline int wrapAdjacentGridIndex(int index, int extent) {
  assert(extent > 0 && index >= -extent && index <= extent);
  return index < 0 ? index + extent : index >= extent ? index - extent : index;
}

struct PeriodicGridCell {
  int first, next;
  double fraction;
};

// Preserve the original interpolation fraction and periodic table entries.
// Normal geographic coordinates fit in int; huge finite inputs retain the
// original floating remainder before conversion, avoiding integer overflow.
inline PeriodicGridCell periodicGridCell(double coordinate, int extent) {
  assert(std::isfinite(coordinate) && extent > 0);
  const double floorValue = std::floor(coordinate);
  int index;
  if (floorValue >= std::numeric_limits<int>::min() &&
      floorValue <= std::numeric_limits<int>::max()) {
    const int integer = int(floorValue);
    if ((extent & (extent - 1)) == 0)
      index = unsigned(integer) & unsigned(extent - 1);
    else {
      index = integer % extent;
      if (index < 0)
        index += extent;
    }
  } else {
    index = int(std::floor(std::remainder(coordinate, double(extent))));
    if (index < 0)
      index += extent;
  }
  return {index, index + 1 == extent ? 0 : index + 1, coordinate - floorValue};
}
} // namespace observatory::detail
