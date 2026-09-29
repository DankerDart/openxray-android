#include "stdafx.h"

#include "Render.h"
#include "xr_input.h"

#include "frame_trace.h"

void CRenderDevice::Destroy()
{
    if (!b_is_Ready)
        return;

    ZoneScoped;
    Log("Destroying Render...");
    b_is_Ready = false;
    Statistic->OnDeviceDestroy();
    GEnv.Render->OnDeviceDestroy(false);
    Memory.mem_compact();
    GEnv.Render->Destroy();
    seqRender.Clear();
    seqAppActivate.Clear();
    seqAppDeactivate.Clear();
    seqAppEnd.Clear();
    seqFrame.Clear();
    seqFrameMT.Clear();
    seqDeviceReset.Clear();
    seqParallel.clear();
    xr_delete(Statistic);

    SDL_DestroyWindow(m_sdlWnd);
}

void CRenderDevice::Reset(bool precache /*= true*/)
{
    ZoneScoped;

    const auto dwWidth_before = dwWidth;
    const auto dwHeight_before = dwHeight;
    pInput->GrabInput(false);

    const auto tm_start = TimerAsync();

    FT_RESET("Reset: imgui begin");
    m_imgui_render->OnDeviceResetBegin();

    FT_RESET("Reset: UpdateWindowProps#1");
    UpdateWindowProps();
    FT_RESET("Reset: GEnv.Render->Reset (GL teardown/rebuild)");
    GEnv.Render->Reset(m_sdlWnd, dwWidth, dwHeight, fWidth_2, fHeight_2);
    FT_RESET("Reset: GEnv.Render->Reset done");

    m_imgui_render->OnDeviceResetEnd();

    FT_RESET("Reset: UpdateWindowProps#2");
    UpdateWindowProps(); // hack

    FT_RESET("Reset: SetupStates");
    SetupStates();
    FT_RESET("Reset: SetupStates done");

    if (precache)
    {
        FT_RESET("Reset: PreCache(20)");
        PreCache(20, false);
        FT_RESET("Reset: PreCache done");
    }

    const auto tm_end = TimerAsync();
    Msg("*** RESET [%d ms]", tm_end - tm_start);

    // TODO: Remove this! It may hide crash
    Memory.mem_compact();

    FT_RESET("Reset: seqDeviceReset");
    seqDeviceReset.Process();
    if (dwWidth_before != dwWidth || dwHeight_before != dwHeight)
    {
        FT_RESET("Reset: seqUIReset");
        seqUIReset.Process();
    }

    if (!GEnv.isDedicatedServer)
        pInput->GrabInput(true);
}
