// opdarkness - mode boost et placement des fils sur la Switch (od_switch_cpu.h).

#include "od_switch_cpu.h"

#if defined(__SWITCH__)
#include <switch.h>
extern "C" int RexSwitchSystemCore(void);
extern "C" bool RexSwitchMoveCurrentThreadToSystemCore(int priority);
extern "C" int RexSwitchSystemCoreMixerPriority(void);
#endif

namespace opdarkness::switch_cpu {

#if defined(__SWITCH__)
bool Boost(bool actif) {
  return R_SUCCEEDED(appletSetCpuBoostMode(actif ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal));
}

bool FixerCoeur(int coeur) {
  if (coeur < 0 || coeur > 2) return false;
  return R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(), coeur, 1u << coeur));
}
int CoeurSysteme() { return RexSwitchSystemCore(); }
bool VersCoeurSysteme(int priorite) { return RexSwitchMoveCurrentThreadToSystemCore(priorite); }
int PrioriteMixage() { return RexSwitchSystemCoreMixerPriority(); }
#else
bool Boost(bool) { return false; }
bool FixerCoeur(int) { return false; }
int CoeurSysteme() { return -1; }
bool VersCoeurSysteme(int) { return false; }
int PrioriteMixage() { return -1; }
#endif

}  // namespace opdarkness::switch_cpu
