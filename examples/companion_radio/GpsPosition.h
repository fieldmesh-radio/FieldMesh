#pragma once

// FieldMesh v2.3: plausibility check for a GPS position (degrees * 1e6, LocationProvider format).
//
// MicroNMEA can report a fix whose position did not parse: a checksum-valid sentence with a
// field missing (e.g. RMC without the N/S hemisphere) still sets the fix flag while the
// longitude parse lands on the wrong characters (233 degrees seen in upstream PR #3626).
// GpsCycle takes over a position only when this check passes, so such values never reach
// tracking, adverts or the SOS message. Boundaries are included.
//
// Header-only and free of Arduino dependencies so it can be unit-tested natively.

#include <stdint.h>

static inline bool fmGpsPositionInRange(long lat_e6, long lon_e6) {
  return lat_e6 >= -90000000L && lat_e6 <= 90000000L
      && lon_e6 >= -180000000L && lon_e6 <= 180000000L;
}
