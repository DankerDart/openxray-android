#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
// The GL backend renders the shadow maps depth-only. rt_smap_surf exists purely
// to give the FBO a color attachment, and at 2048x2048 with a D3DFMT_R5G6B5
// format that GL expands to RGBA8 it is a 16 MB texture that the sun pass then
// loads and stores for nothing, once per cascade and once per spot light. A
// depth-only FBO is legal in GLES 3.x, so skip the attachment entirely.
#if defined(USE_OGL)
#   define OXR_SMAP_COLOR_ATTACHMENT nullptr
#else
#   define OXR_SMAP_COLOR_ATTACHMENT rt_smap_surf
#endif

void CRenderTarget::phase_smap_direct(CBackend& cmd_list, light *L, u32 sub_phase)
{
    if (sub_phase == SE_SUN_RAIN_SMAP)
    {
        u_setrt(cmd_list, nullptr, nullptr, nullptr, rt_smap_rain);
        cmd_list.ClearZB(rt_smap_rain, 1.0f);
        cmd_list.SetViewport({0, 0, rt_smap_rain->dwWidth, rt_smap_rain->dwHeight, 0.0, 1.0});
    }
    else
    {
        rt_smap_depth->set_slice_write(cmd_list.context_id, sub_phase);
        cmd_list.set_pass_targets(
            OXR_SMAP_COLOR_ATTACHMENT,
            nullptr,
            nullptr,
            rt_smap_depth
        );
        cmd_list.ClearZB(rt_smap_depth, 1.0f);
    }

    // Stencil	- disable
    cmd_list.set_Stencil(FALSE);
}

void CRenderTarget::phase_smap_direct_tsh(CBackend& cmd_list, light *L, u32 sub_phase)
{
    VERIFY(RImplementation.o.Tshadows);
    cmd_list.set_ColorWriteEnable();
    //	Prepare viewport for shadow map rendering
    RImplementation.rmNormal(cmd_list);
#if !defined(USE_OGL)
    cmd_list.ClearRT(cmd_list.get_RT(), { 1.0f, 1.0f, 1.0f, 1.0f }); // color_rgba(127, 127, 12, 12);
#endif
}
} // namespace xray::render::RENDER_NAMESPACE
#undef OXR_SMAP_COLOR_ATTACHMENT
