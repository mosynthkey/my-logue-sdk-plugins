#pragma once

/*
 * Logue-sdk touch phase ABI (unit_genericfx / runtime.h).
 * Defined here so NTS-1 headers can interpret NTS-3 pad events
 * without pulling platform-only SDK headers.
 */

enum
{
  kLogueTouchBegan = 0,
  kLogueTouchMoved = 1,
  kLogueTouchEnded = 2,
  kLogueTouchCancelled = 3,
  kLogueTouchStationary = 4
};
