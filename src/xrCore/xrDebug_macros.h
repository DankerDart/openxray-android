#pragma once

#include "xrDebug.h"

// glGetError() is a synchronous client->driver query: the driver has to
// validate its command buffer before it can answer. On tiled mobile GPUs
// (Mali, Adreno) that behaves like a miniature glFinish, and CHK_GL sits in
// the per-object state path -- roughly a dozen calls per rendered object, i.e.
// tens of thousands of round-trips per frame. That alone costs whole frames.
//
// Default to off and let the release build issue the call unchecked, which is
// what the other CHK_* macros already do here. Turn it back on when chasing a
// GL problem: -DXRAY_ENABLE_GL_ERROR_CHECK=1
#ifndef XRAY_ENABLE_GL_ERROR_CHECK
#   define XRAY_ENABLE_GL_ERROR_CHECK 0
#endif

#define DEBUG_INFO {__FILE__, __LINE__, __FUNCTION__}
#define CHECK_OR_EXIT(expr, message)\
    do\
    {\
        if (!(expr))\
            xrDebug::DoExit(message);\
    } while (false)
#define R_ASSERT(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
    } while (false)
#define R_ASSERT2(expr, desc)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
    } while (false)
#define R_ASSERT3(expr, desc, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
    } while (false)
#define R_ASSERT4(expr, desc, arg1, arg2)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
    } while (false)

#define R_CHK(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr = expr;\
        if (!ignoreAlways && FAILED(hr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr);\
    } while (false)
#define R_CHK2(expr, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr = expr;\
        if (!ignoreAlways && FAILED(hr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr, arg1);\
    } while (false)
#define FATAL(desc) xrDebug::Fatal(DEBUG_INFO, "%s", desc)
#define FATAL_F(format, ...) xrDebug::Fatal(DEBUG_INFO, format, __VA_ARGS__)

#ifdef VERIFY
#undef VERIFY
#endif

#ifdef DEBUG
#define NODEFAULT FATAL("nodefault reached")

#define R_ASSERT1_CURE(expr, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
            cure;\
        }\
    } while (false)

#define R_ASSERT2_CURE(expr, desc, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
            cure;\
        }\
    } while (false)

#define R_ASSERT3_CURE(expr, desc, arg1, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
            cure;\
        }\
    } while (false)

#define R_ASSERT4_CURE(expr, cure, desc, arg1, arg2)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
            cure;\
        }\
    } while (false)

#define VERIFY(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
    } while (false)
#define VERIFY2(expr, desc)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
    } while (false)
#define VERIFY3(expr, desc, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
    } while (false)
#define VERIFY4(expr, desc, arg1, arg2)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
    } while (false)
#define CHK_DX(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr_ = expr;\
        if (!ignoreAlways && FAILED(hr_))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr_);\
    } while (false)
#define CHK_GL(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        expr;\
        GLenum err = glGetError();\
        if (!ignoreAlways && err != GL_NO_ERROR)\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, (long)err);\
    } while (false)
#else // DEBUG
#define R_ASSERT1_CURE(expr, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT2_CURE(expr, desc, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT3_CURE(expr, desc, arg1, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT4_CURE(expr, cure, desc, arg1, arg2)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define NODEFAULT XR_ASSUME(0)
#define VERIFY(expr) do {} while (false)
#define VERIFY2(expr, desc) do {} while (false)
#define VERIFY3(expr, desc, arg1) do {} while (false)
#define VERIFY4(expr, desc, arg1, arg2) do {} while (false)
#define CHK_DX(expr) expr
#if XRAY_ENABLE_GL_ERROR_CHECK
#define CHK_GL(expr)\
    do\
    {\
        expr;\
        GLenum glErr_ = glGetError();\
        if (glErr_ != GL_NO_ERROR)\
            Msg("! GL error 0x%X after: %s", glErr_, #expr);\
    } while (false)
#else
#define CHK_GL(expr) expr
#endif // XRAY_ENABLE_GL_ERROR_CHECK
#endif // DEBUG

// Framebuffer completeness is a driver query too, and u_setrt()/set_pass_targets()
// issue it on every render target switch -- ~25-35 times per frame. VERIFY()
// discards the result outside debug builds, so in release the query bought
// nothing and still stalled the pipeline. Keep it wherever assertions are live,
// or when the GL error check is explicitly turned on.
#if XRAY_ENABLE_GL_ERROR_CHECK || defined(DEBUG) || defined(_DEBUG)
#define XR_GL_CHECK_FBO() \
    do \
    { \
        [[maybe_unused]] GLenum status_ = glCheckFramebufferStatus(GL_FRAMEBUFFER); \
        VERIFY(status_ == GL_FRAMEBUFFER_COMPLETE); \
    } while (false)
#else
#define XR_GL_CHECK_FBO() do {} while (false)
#endif // GL check

#if XRAY_EXCEPTIONS
#define THROW3(expr, msg0, msg1)\
    do\
    {\
        if (!(expr))\
        {\
            string4096 assertionInfo;\
            xrDebug::GatherInfo(assertionInfo, sizeof(assertionInfo), DEBUG_INFO, #expr, msg0, msg1, nullptr);\
            throw assertionInfo;\
        }\
    }\
    while (false)

#define THROW(expr) THROW3(expr, nullptr, nullptr)
#define THROW2(expr, msg0) THROW3(expr, msg0, nullptr)

#else
#define THROW VERIFY
#define THROW2 VERIFY2
#define THROW3 VERIFY3
#endif

//---------------------------------------------------------------------------------------------
// FIXMEs / TODOs / NOTE macros
//---------------------------------------------------------------------------------------------
#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)
#define __FILE__LINE__ __FILE__ "(" QUOTE(__LINE__) ") : "

#define NOTE(x) message(x)
#define FILE_LINE message(__FILE__LINE__)

#define TODO(x) message(__FILE__LINE__"\n"\
    " ------------------------------------------------\n"\
    "| TODO : " #x "\n"\
    " -------------------------------------------------\n")
#define FIXME(x) message(__FILE__LINE__"\n"\
    " ------------------------------------------------\n"\
    "| FIXME : " #x "\n"\
    " -------------------------------------------------\n")
#define todo(x) message(__FILE__LINE__" TODO : " #x "\n")
#define fixme(x) message(__FILE__LINE__" FIXME: " #x "\n")

