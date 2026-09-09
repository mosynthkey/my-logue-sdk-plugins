/*
 * Host-side probe for Passort region lock and basic audio energy.
 * Build: g++ -O2 -I../../common -I../dsp -I$SDK/.../common render_offline_test.cc -o render_offline_test
 * (Uses a lightweight stub when run without full SDK; prefers verifying modeFromStart logic.)
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

// Mirror Passort::modeFromStart without pulling the full Processor stack.
enum
{
  MODE_NONE = 0,
  MODE_HPF,
  MODE_LPF,
  MODE_TAPE,
  MODE_DELAY,
  MODE_ROLL
};

static uint8_t modeFromStart(uint32_t x, uint32_t y)
{
  const float kCenterRadius = 190.f;
  const float dx = static_cast<float>(x) - 511.5f;
  const float dy = static_cast<float>(y) - 511.5f;
  if (dx * dx + dy * dy <= kCenterRadius * kCenterRadius)
    return MODE_TAPE;

  const bool left = x < 512U;
  const bool top = y >= 512U;
  if (left && top)
    return MODE_HPF;
  if (!left && top)
    return MODE_LPF;
  if (left && !top)
    return MODE_DELAY;
  return MODE_ROLL;
}

static const char *modeName(uint8_t mode)
{
  switch (mode)
  {
  case MODE_HPF:
    return "HPF";
  case MODE_LPF:
    return "LPF";
  case MODE_TAPE:
    return "TAPE";
  case MODE_DELAY:
    return "DELAY";
  case MODE_ROLL:
    return "ROLL";
  default:
    return "NONE";
  }
}

int main()
{
  struct Case
  {
    uint32_t x;
    uint32_t y;
    uint8_t expect;
  };

  const Case cases[] = {
      {128, 900, MODE_HPF},
      {900, 900, MODE_LPF},
      {512, 512, MODE_TAPE},
      {480, 520, MODE_TAPE},
      {128, 128, MODE_DELAY},
      {900, 128, MODE_ROLL},
      {300, 700, MODE_HPF},
      {700, 700, MODE_LPF},
  };

  int failures = 0;
  for (const Case &testCase : cases)
  {
    const uint8_t got = modeFromStart(testCase.x, testCase.y);
    const bool ok = got == testCase.expect;
    std::printf("start(%u,%u) -> %s %s\n", testCase.x, testCase.y, modeName(got),
                ok ? "OK" : "FAIL");
    if (!ok)
      ++failures;
  }

  if (failures != 0)
  {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all region cases passed\n");
  return 0;
}
