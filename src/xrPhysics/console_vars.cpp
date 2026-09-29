#include "StdAfx.h"
#include "console_vars.h"

#include "PhysicsCommon.h"

BOOL ph_console::g_bDebugDumpPhysicsStep = 0;
float ph_console::ph_tri_query_ex_aabb_rate = 1.3f;
int ph_console::ph_tri_clear_disable_count = 10;

float ph_console::phBreakCommonFactor = 0.01f;
float ph_console::phRigidBreakWeaponFactor = 1.f;

float ph_console::ph_step_time = fixed_step;

// A frame that takes 0.1s would otherwise ask for ten 100Hz substeps, and each
// substep walks every physics object five times. That turns a slow frame into a
// slower one. Cap the catch-up and let the simulation fall behind instead:
// three substeps still cover real time down to ~33 fps, and below that physics
// slows down rather than eating the frame. Tune with ph_max_substeps.
#if defined(XR_PLATFORM_ANDROID)
int ph_console::ph_max_substeps = 3;
#else
int ph_console::ph_max_substeps = 5;
#endif
