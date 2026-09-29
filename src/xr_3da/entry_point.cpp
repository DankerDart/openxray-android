#include "stdafx.h"

#include "xrEngine/x_ray.h"
#include "xrGame/xrGame.h"
#include "Include/xrRender/xrRender.h"

#if !defined(XR_PLATFORM_WINDOWS)
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#endif

#if defined(XR_PLATFORM_ANDROID)
#include <android/log.h>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <unwind.h>

namespace
{
struct backtrace_state
{
    void** frames;
    int count;
    int capacity;
};

// The NDK ships no execinfo.h, so walk the stack through the compiler's unwind
// tables (clang always provides libunwind) and resolve the PCs with dladdr.
_Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* context, void* arg)
{
    auto* state = static_cast<backtrace_state*>(arg);
    if (state->count < state->capacity)
    {
        const uintptr_t pc = _Unwind_GetIP(context);
        if (pc != 0)
            state->frames[state->count++] = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

// R_ASSERT aborts and bad pointers fault inside the engine, where nothing reaches
// the Java UncaughtExceptionHandler, so the only trace is whatever logcat has.
// Dump the signal and a backtrace before letting the default handler terminate
// the process, otherwise the system still writes a tombstone but we lose it.
void AndroidCrashHandler(int signal_number, siginfo_t* info, void*)
{
    char line[1024];

    snprintf(line, sizeof(line), "\n*** OpenXRay NATIVE CRASH: signal %d (%s), si_code %d, si_addr %p, pid %d ***",
        signal_number, strsignal(signal_number), info ? info->si_code : 0,
        info ? info->si_addr : nullptr, getpid());
    __android_log_write(ANDROID_LOG_FATAL, "OpenXRay", line);

    void* frames[64];
    backtrace_state state{frames, 0, 64};
    _Unwind_Backtrace(UnwindCallback, &state);

    for (int i = 0; i < state.count; ++i)
    {
        Dl_info dl_info;
        const char* symbol = "??";
        const char* object = "??";
        if (dladdr(frames[i], &dl_info) != 0)
        {
            if (dl_info.dli_sname)
                symbol = dl_info.dli_sname;
            if (dl_info.dli_fname)
                object = dl_info.dli_fname;
        }
        snprintf(line, sizeof(line), "    #%02d %p %s (%s)", i, frames[i], symbol, object);
        __android_log_write(ANDROID_LOG_FATAL, "OpenXRay", line);
    }

    // Restore the default disposition and re-raise, so the process still dies the
    // way the platform expects (and produces its own tombstone).
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL;
    sigaction(signal_number, &action, nullptr);
    raise(signal_number);
}

void InstallCrashHandlers()
{
    // Stack overflow cannot be reported from the overflowed stack.
    static char alt_stack[SIGSTKSZ > 65536 ? SIGSTKSZ : 65536];
    stack_t ss;
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    action.sa_sigaction = AndroidCrashHandler;

    for (const int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(sig, &action, nullptr);
}
} // namespace
#endif

#if defined(XR_PLATFORM_APPLE)
#include <SDL.h>

namespace
{
bool EnvironmentFlagEnabled(pcstr name)
{
    pcstr value = SDL_getenv(name);
    return value && value[0] && xr_strcmp(value, "0") != 0 && xr_strcmp(value, "false") != 0 &&
        xr_strcmp(value, "off") != 0;
}

std::string GetBundledDefaultCommandLine()
{
    char* basePath = SDL_GetBasePath();
    if (!basePath)
        return {};

    std::string commandLinePath = basePath;
    SDL_free(basePath);
    commandLinePath += "../Resources/openxray/default_command_line.txt";

    FILE* file = fopen(commandLinePath.c_str(), "r");
    if (!file)
        return {};

    char commandLine[1024]{};
    const bool hasCommandLine = fgets(commandLine, sizeof(commandLine), file) != nullptr;
    fclose(file);

    if (!hasCommandLine)
        return {};

    commandLine[strcspn(commandLine, "\r\n")] = '\0';
    return commandLine;
}
} // namespace
#endif

// Always request high performance GPU
extern "C"
{
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
XR_EXPORT u32 NvOptimusEnablement = 0x00000001; // NVIDIA Optimus

// https://gpuopen.com/amdpowerxpressrequesthighperformance/
XR_EXPORT u32 AmdPowerXpressRequestHighPerformance = 0x00000001; // PowerXpress or Hybrid Graphics
}

std::array<RendererModule*, 2> s_render_modules =
{
#ifdef XR_PLATFORM_WINDOWS
    xray::render::render_r4::GetRendererModule(),
#endif
    xray::render::render_gl::GetRendererModule(),
};

struct tracy_raii
{
    ~tracy_raii()
    {
#ifdef TRACY_ENABLE
        tracy::GetProfiler().RequestShutdown();
        while (!tracy::GetProfiler().HasShutdownFinished())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#endif
    }
};

int entry_point(pcstr commandLine)
{
    tracy_raii raii;
    auto* game = strstr(commandLine, "-nogame") ? nullptr : &xrGame;

    CApplication app{ commandLine, game, s_render_modules };

    return app.Run();
}

#if defined(XR_PLATFORM_WINDOWS)
int StackoverflowFilter(const int exceptionCode)
{
    if (exceptionCode == EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_EXECUTE_HANDLER;
    return EXCEPTION_CONTINUE_SEARCH;
}

int APIENTRY WinMain(HINSTANCE inst, HINSTANCE prevInst, char* commandLine, int cmdShow)
{
    int result = 0;
    // BugTrap can't handle stack overflow exception, so handle it here
    __try
    {
        result = entry_point(commandLine);
    }
    __except (StackoverflowFilter(GetExceptionCode()))
    {
        _resetstkoflw();
        FATAL("stack overflow");
    }

    return result;
}
#elif defined(XR_PLATFORM_ANDROID)
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char *argv[]);

extern "C" __attribute__((visibility("default"))) int main(int argc, char *argv[])
{
    return SDL_main(argc, argv);
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    int result = EXIT_FAILURE;

#if defined(XR_PLATFORM_ANDROID)
    InstallCrashHandlers();
#endif

    try
    {
#if defined(XR_PLATFORM_APPLE)
        std::string commandLine = GetBundledDefaultCommandLine();
        if (EnvironmentFlagEnabled("OPENXRAY_SKIP_INTRO") && commandLine.find("-nointro") == std::string::npos)
        {
            if (!commandLine.empty())
                commandLine += ' ';
            commandLine += "-nointro";
        }
#else
        std::string commandLine;
#endif

        if (!commandLine.empty())
            commandLine += ' ';

        for (int i = 1; i < argc; ++i)
        {
            commandLine += argv[i];
            commandLine += ' ';
        }

        result = entry_point(commandLine.c_str());
    }
    catch (const std::overflow_error& e)
    {
        _resetstkoflw();
        FATAL_F("stack overflow: %s", e.what());
    }
    catch (const std::runtime_error& e)
    {
        FATAL_F("runtime error: %s", e.what());
    }
    catch (const std::exception& e)
    {
        FATAL_F("exception: %s", e.what());
    }
    catch (...)
    {
    // this executes if f() throws std::string or int or any other unrelated type
    }

    return result;
}
