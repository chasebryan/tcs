#ifndef TCS_LAUNCH_PROFILE_H
#define TCS_LAUNCH_PROFILE_H
#include "tcs/launch.h"
#ifndef TCS_LAUNCH_MODE
#error "Choose an explicit launch mode for the separate interactive image"
#elif TCS_LAUNCH_MODE == 0
#define TCS_LAUNCH_NAME "experimental-operator"
#elif TCS_LAUNCH_MODE == 1
#define TCS_LAUNCH_NAME "PUBLIC-FIXTURE-ONLY"
#else
#error "Unknown launch mode"
#endif
#define TCS_LAUNCH_CONTEXT UINT64_C(0x401)
#define TCS_LAUNCH_DATA UINT64_C(0x480)
#define TCS_LAUNCH_ITEM "opt/tcs/launch-context"
#endif
