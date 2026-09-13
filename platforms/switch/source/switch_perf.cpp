#include "switch/perf.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace dusk::sw {

#ifdef __SWITCH__
namespace {
constexpr u32 kDockedMax = 0x00010001;
constexpr u32 kDockedDefault = 0x00010000;
constexpr u32 kHandheldMax = 0x00020004;
constexpr u32 kHandheldDefault = 0x00020003;

bool docked() {
  return appletGetOperationMode() != AppletOperationMode_Handheld;
}
}

void perf_apply_boost(bool boost, bool) {
  apmSetPerformanceConfiguration(ApmPerformanceMode_Normal,
    boost ? (docked() ? kDockedMax : kHandheldMax)
          : (docked() ? kDockedDefault : kHandheldDefault));
}

void perf_restore_stock() {
  apmSetPerformanceConfiguration(ApmPerformanceMode_Normal,
    docked() ? kDockedDefault : kHandheldDefault);
}
#else
void perf_apply_boost(bool, bool) {}
void perf_restore_stock() {}
#endif

}
