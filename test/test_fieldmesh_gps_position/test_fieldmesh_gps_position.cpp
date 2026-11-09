// FieldMesh: tests for the GPS position plausibility check (examples/companion_radio/GpsPosition.h).
// GpsCycle takes over a fix only when latitude is within +-90 and longitude within +-180 degrees
// (boundaries included), values in degrees * 1e6 as delivered by LocationProvider.

#include <gtest/gtest.h>

#include "../../examples/companion_radio/GpsPosition.h"

namespace {

// Stuttgart
constexpr long LAT = 48775800;
constexpr long LON = 9182900;

TEST(GpsPosition, NormalPositionsPass) {
  EXPECT_TRUE(fmGpsPositionInRange(LAT, LON));
  EXPECT_TRUE(fmGpsPositionInRange(-33868800, 151209300));   // Sydney
  EXPECT_TRUE(fmGpsPositionInRange(40712800, -74006000));    // New York
  EXPECT_TRUE(fmGpsPositionInRange(0, 0));
}

TEST(GpsPosition, BoundariesPass) {
  EXPECT_TRUE(fmGpsPositionInRange(90000000L, 0));
  EXPECT_TRUE(fmGpsPositionInRange(-90000000L, 0));
  EXPECT_TRUE(fmGpsPositionInRange(0, 180000000L));
  EXPECT_TRUE(fmGpsPositionInRange(0, -180000000L));
  EXPECT_TRUE(fmGpsPositionInRange(-90000000L, -180000000L));
}

TEST(GpsPosition, LatitudeOutOfRangeFails) {
  EXPECT_FALSE(fmGpsPositionInRange(90000001L, LON));
  EXPECT_FALSE(fmGpsPositionInRange(-90000001L, LON));
  EXPECT_FALSE(fmGpsPositionInRange(487758000L, LON));   // value off by a factor of 10
}

TEST(GpsPosition, LongitudeOutOfRangeFails) {
  EXPECT_FALSE(fmGpsPositionInRange(LAT, 180000001L));
  EXPECT_FALSE(fmGpsPositionInRange(LAT, -180000001L));
  EXPECT_FALSE(fmGpsPositionInRange(LAT, 233000000L));   // broken RMC, upstream PR #3626
}

TEST(GpsPosition, ExtremeValuesFail) {
  EXPECT_FALSE(fmGpsPositionInRange(2147483647L, LON));
  EXPECT_FALSE(fmGpsPositionInRange(LAT, -2147483647L - 1));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
