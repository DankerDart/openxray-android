#pragma once

namespace xray::render::RENDER_NAMESPACE
{
// glPushDebugGroup() is a string-carrying driver call, and there are well over a
// hundred PIX_EVENT sites in the per-frame render path. It also arms the debug
// message route that CHK_GL used to feed on every single GL call. Shipping
// builds therefore compile it out; keep it for Debug/Mixed so a PIX_EVENT-timed
// frame is still readable in a graphics debugger.
#if defined(MASTER_GOLD) || (defined(NDEBUG) && !defined(DEBUG))
#   define PIX_EVENT(Name) do { } while (false)
#   define PIX_EVENT_CTX(C,Name) do { } while (false)
#else
#if defined(USE_DX11)
#   define PIX_EVENT(Name) dxPixEventWrapper pixEvent##Name(RCache,L ## #Name)
#   define PIX_EVENT_CTX(C,Name) dxPixEventWrapper pixEvent##Name(C,L ## #Name)

class dxPixEventWrapper
{
    CBackend& cmd_list;
public:
    dxPixEventWrapper(CBackend& cmd_list_in, const wchar_t* wszName)
    : cmd_list(cmd_list_in)
    {
        cmd_list.gpu_mark_begin(wszName);
    }
    ~dxPixEventWrapper() { cmd_list.gpu_mark_end(); }
};
#elif defined(USE_OGL)
#   define PIX_EVENT(Name) dxPixEventWrapper pixEvent##Name(#Name)
#   define PIX_EVENT_CTX(C,Name) dxPixEventWrapper pixEvent##Name(#Name)

class dxPixEventWrapper
{
public:
    dxPixEventWrapper(const char* name) { HW.BeginPixEvent(name); }
    ~dxPixEventWrapper() { HW.EndPixEvent(); }
};
#else
#   error No graphics API selected or enabled!
#endif // USE_OGL
#endif // MASTER_GOLD
} // namespace xray::render::RENDER_NAMESPACE
