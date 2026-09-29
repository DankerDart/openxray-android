include_guard()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output all libraries and executables to one folder
set(XRAY_COMPILE_OUTPUT_FOLDER "${CMAKE_SOURCE_DIR}/bin/${CMAKE_SYSTEM_PROCESSOR}/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${XRAY_COMPILE_OUTPUT_FOLDER}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${XRAY_COMPILE_OUTPUT_FOLDER}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${XRAY_COMPILE_OUTPUT_FOLDER}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${XRAY_COMPILE_OUTPUT_FOLDER}")

add_compile_definitions(
    # _DEBUG, DEBUG, MIXED, NDEBUG defines
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug,Mixed>:DEBUG>
    $<$<CONFIG:Mixed>:MIXED>
    $<$<CONFIG:Release,ReleaseMasterGold>:NDEBUG>
    # Tracy profiler
    $<$<BOOL:${XRAY_ENABLE_TRACY}>:TRACY_ENABLE>
    $<$<BOOL:${XRAY_ENABLE_TRACY}>:TRACY_NO_FRAME_IMAGE>
    # glGetError after every GL call -- see the comment in xrDebug_macros.h
    $<$<BOOL:${XRAY_ENABLE_GL_ERROR_CHECK}>:XRAY_ENABLE_GL_ERROR_CHECK=1>
    # Luabind
    $<$<CONFIG:Release,ReleaseMasterGold>:LUABIND_NO_EXCEPTIONS>
    $<$<CONFIG:Release,ReleaseMasterGold>:LUABIND_NO_ERROR_CHECKING>
)

# Link-time optimization
include(CheckIPOSupported)
if (ANDROID)
    # check_ipo_supported() runs its probe outside the Android ABI context: the
    # NDK then builds it for armeabi-v7a with the long-removed -fuse-ld=gold,
    # the probe fails and LTO is silently dropped from every release build.
    # clang/lld do support ThinLTO for the real target, so enable it directly.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASEMASTERGOLD ON)
    set(LTO_IS_SUPPORTED "ON (forced for Android)")
    # Same guardrail for the link step: CMake 3.22 picks the IPO linker flag via
    # CMAKE_ANDROID_NDK_VERSION, which the NDK r27 toolchain never sets, so the
    # comparison falls through and -fuse-ld=gold is used instead of lld. That
    # breaks linking as soon as LTO is on.
    #
    # Full LTO, not ThinLTO (CMake's default is -flto=thin). ThinLTO keeps the
    # per-module partitioning barrier, so it can still emit calls between
    # translation units that full LTO would have inlined; the engine is one
    # giant call graph and its cost is dominated by that. -flto-job-count lets
    # the fat-LTO codegen run across cores, otherwise the link step is
    # single-threaded and takes forever on this codebase.
    if (NOT DEFINED XRAY_LTO_JOB_COUNT)
        include(ProcessorCount)
        ProcessorCount(XRAY_LTO_JOB_COUNT)
        if (XRAY_LTO_JOB_COUNT EQUAL 0)
            set(XRAY_LTO_JOB_COUNT 1)
        endif()
    endif()
    message(STATUS "LTO_JOB_COUNT:        ${XRAY_LTO_JOB_COUNT}")
    # clang emits bitcode-only objects for plain -flto, so the fat-LTO link sees
    # every object and not a stale pre-LTO copy.
    set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto")
    set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto")
    # These have to be CMake *lists* (semicolon separated), not one string with
    # spaces: CMake quotes a plain string and hands the linker driver a single
    # argument, which it then rejects with "invalid linker name in argument".
    # The lld in NDK r27 is 18, which has no -flto-job-count (that arrived in a
    # later lld); --threads is the knob it does have, and it is what governs the
    # LTO plugin's codegen here.
    set(CMAKE_C_LINK_OPTIONS_IPO "-fuse-ld=lld;-flto;-Wl,--threads=${XRAY_LTO_JOB_COUNT}")
    set(CMAKE_CXX_LINK_OPTIONS_IPO "-fuse-ld=lld;-flto;-Wl,--threads=${XRAY_LTO_JOB_COUNT}")

    # NDK r27 already defaults the release flags to "-O3 -DNDEBUG", but that is a
    # toolchain default, not something this build asked for. Append -O3 so a
    # toolchain change cannot quietly drop the engine to -O2: it is appended
    # last, so it wins over anything earlier in the string.
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
else()
    check_ipo_supported(RESULT LTO_IS_SUPPORTED)
    if (LTO_IS_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASEMASTERGOLD ON)
    endif()
endif()

# Main compiler settings
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    include(XRay.Compiler.MSVC)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|LCC|Clang")
    include(XRay.Compiler.GNULike)
else()
    message(FATAL_ERROR "Unsupported or unknown compiler.")
endif()

# https://gitlab.kitware.com/cmake/cmake/-/issues/25650
if (CMAKE_VERSION VERSION_EQUAL "3.28.2" AND CMAKE_UNITY_BUILD)
    message(WARNING
        "In CMake 3.28.2, precompiled headers are broken when Unity build is enabled. \
        We have to disable Unity build. Please, update to CMake 3.28.3 or downgrade to 3.28.1."
    )
    set(CMAKE_UNITY_BUILD OFF)
endif()

query_git_info(XRAY_GIT_SHA XRAY_GIT_BRANCH)

message(VERBOSE "CMAKE_UNITY_BUILD:     ${CMAKE_UNITY_BUILD}")
message(STATUS  "CMAKE_PROJECT_VERSION: ${CMAKE_PROJECT_VERSION}")
message(STATUS  "XRAY_GIT_SHA:          ${XRAY_GIT_SHA}")
message(STATUS  "XRAY_GIT_BRANCH:       ${XRAY_GIT_BRANCH}")

message(STATUS "BUILD_SHARED_LIBS:     ${BUILD_SHARED_LIBS}")
message(STATUS "LTO_IS_SUPPORTED:      ${LTO_IS_SUPPORTED}")

message(DEBUG)
message(DEBUG "C++ Flags:")
message(DEBUG "           Global: ${CMAKE_CXX_FLAGS}")
message(DEBUG "            Debug: ${CMAKE_CXX_FLAGS_DEBUG}")
message(DEBUG "            Mixed: ${CMAKE_CXX_FLAGS_MIXED}")
message(DEBUG "          Release: ${CMAKE_CXX_FLAGS_RELEASE}")
message(DEBUG "ReleaseMasterGold: ${CMAKE_CXX_FLAGS_RELEASEMASTERGOLD}")

message(DEBUG)
message(DEBUG "C Flags:")
message(DEBUG "           Global: ${CMAKE_C_FLAGS}")
message(DEBUG "            Debug: ${CMAKE_C_FLAGS_DEBUG}")
message(DEBUG "            Mixed: ${CMAKE_C_FLAGS_MIXED}")
message(DEBUG "          Release: ${CMAKE_C_FLAGS_RELEASE}")
message(DEBUG "ReleaseMasterGold: ${CMAKE_C_FLAGS_RELEASEMASTERGOLD}")
message(DEBUG)

unset(LTO_IS_SUPPORTED)
