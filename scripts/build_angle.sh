#!/usr/bin/env bash
#
# Builds ANGLE (libEGL_angle.so + libGLESv2_angle.so) for Android arm64-v8a and
# installs the result into android/app/src/main/jniLibs/arm64-v8a, which is what
# LauncherActivity looks for when the "angle_vulkan" / "angle_gles" backend is
# selected (OpenXRayActivity preloads the two libraries and points
# SDL_VIDEO_EGL_DRIVER / SDL_VIDEO_GL_DRIVER at them).
#
# Usage:
#   scripts/build_angle.sh [--clean] [--debug]
#
# Environment overrides:
#   ANDROID_SDK_ROOT    defaults to sdk.dir from android/local.properties
#   ANDROID_NDK_HOME    defaults to <ndkVersion> from android/app/build.gradle
#   ANGLE_REF           git ref to build (default: main)
#   ANGLE_WORKSPACE     gclient workspace (default: third_party/angle_ws)
#   DEPOT_TOOLS         depot_tools checkout (default: third_party/depot_tools)
#   JOBS                parallel ninja jobs (default: nproc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ABI="arm64-v8a"
MIN_SDK=24
MAX_SDK=34
ANGLE_REF="${ANGLE_REF:-main}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$ROOT_DIR/third_party/depot_tools}"
WORKSPACE="${ANGLE_WORKSPACE:-$ROOT_DIR/third_party/angle_ws}"
ANGLE_SRC="$WORKSPACE/angle"
JOBS="${JOBS:-$(nproc)}"
CONFIG="Release"
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --debug) CONFIG="Debug" ;;
        -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

log() { printf '\033[1;34m[angle]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[angle] %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- toolchain --
command -v git >/dev/null || die "git is required"
command -v python3 >/dev/null || die "python3 is required"
command -v curl >/dev/null || die "curl is required"

if [[ -z "${ANDROID_SDK_ROOT:-}" ]]; then
    local_props="$ROOT_DIR/android/local.properties"
    [[ -f "$local_props" ]] || die "android/local.properties not found; set ANDROID_SDK_ROOT"
    ANDROID_SDK_ROOT="$(sed -n 's/^sdk\.dir=//p' "$local_props" | tail -1)"
    [[ -n "$ANDROID_SDK_ROOT" ]] || die "could not read sdk.dir from $local_props"
fi
log "Android SDK: $ANDROID_SDK_ROOT"

if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    ndk_ver="$(sed -n 's/^[[:space:]]*ndkVersion[[:space:]]*"\(.*\)"/\1/p' "$ROOT_DIR/android/app/build.gradle" | head -1)"
    [[ -n "$ndk_ver" ]] || die "could not read ndkVersion from android/app/build.gradle; set ANDROID_NDK_HOME"
    ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/$ndk_ver"
fi
[[ -d "$ANDROID_NDK_HOME" ]] || die "NDK not found at $ANDROID_NDK_HOME (set ANDROID_NDK_HOME)"
log "NDK: $ANDROID_NDK_HOME"

# ------------------------------------------------------------------ checkout --
if [[ $CLEAN -eq 1 ]]; then
    log "Removing $WORKSPACE"
    rm -rf "$WORKSPACE"
fi

if [[ ! -d "$DEPOT_TOOLS/.git" ]]; then
    log "Cloning depot_tools into $DEPOT_TOOLS"
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
fi
export PATH="$DEPOT_TOOLS:$PATH"
export DEPOT_TOOLS_UPDATE=0
export GCLIENT_PY3=1
export PYTHONDONTWRITEBYTECODE=1
command -v gclient >/dev/null || die "gclient is not available after adding depot_tools to PATH"

# depot_tools ships its own python3/ninja, which are only unpacked on first use.
# autoninja refuses to run until that happened.
if [[ ! -f "$DEPOT_TOOLS/python3_bin_reldir.txt" ]]; then
    log "Bootstrapping depot_tools"
    "$DEPOT_TOOLS/ensure_bootstrap" || die "depot_tools bootstrap failed"
fi

# ANGLE is a Chromium-style checkout: building it standalone needs the dependency
# graph from DEPS (build/, third_party/rust, spirv-tools, abseil, ...), which only
# gclient can resolve. A plain git clone fails with "Unable to load
# build/dotfile_settings.gni".
if [[ ! -f "$WORKSPACE/.gclient" ]]; then
    mkdir -p "$WORKSPACE"
    log "Writing $WORKSPACE/.gclient"
    cat > "$WORKSPACE/.gclient" <<EOF
solutions = [
  {
    "name": "angle",
    "url": "https://github.com/google/angle.git@$ANGLE_REF",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
target_os = ['android']
checkout_android = True
EOF
fi

log "gclient sync (downloads several GB, be patient)"
# --no-history keeps the checkout to the pinned revisions; the full history of llvm,
# dawn, SwiftShader and VK-GL-CTS would add tens of gigabytes for no benefit.
(cd "$WORKSPACE" && gclient sync --shallow --no-history --force -j "$JOBS")

ANGLE_SRC="$WORKSPACE/angle"
[[ -d "$ANGLE_SRC/build" ]] || die "gclient sync did not populate $ANGLE_SRC/build"

GN="$ANGLE_SRC/buildtools/linux64/gn"
NINJA="$ANGLE_SRC/buildtools/linux64/ninja"
AUTONINJA="autoninja"

# The chromium buildtools submodule no longer vendors the gn binary, but ANGLE's DEPS
# still pins it as a CIPD package. Fetch the same revision instead of failing.
if [[ ! -x "$GN" ]]; then
    gn_rev="$(awk "/'package': 'gn\/gn\/linux-/ {found=1} found && /git_revision:/ {print; exit}" "$ANGLE_SRC/DEPS" \
        | sed -n "s/.*git_revision:\([0-9a-f]*\).*/\1/p")"
    [[ -n "$gn_rev" ]] || die "could not determine the gn revision from $ANGLE_SRC/DEPS"
    gn_url="https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/git_revision:$gn_rev"
    log "gn is missing from buildtools, downloading $gn_rev from CIPD"
    tmp_zip="$(mktemp -d)/gn.zip"
    curl -sSL -o "$tmp_zip" "$gn_url" || die "failed to download gn from $gn_url"
    mkdir -p "$(dirname "$GN")"
    unzip -o -j "$tmp_zip" gn -d "$(dirname "$GN")" >/dev/null || die "failed to extract gn"
    chmod +x "$GN"
    log "gn $("$GN" --version | head -1) installed to $GN"
fi
[[ -x "$GN" ]] || die "gn not available at $GN"

# -------------------------------------------------------------------- build --
OUT_DIR="$ANGLE_SRC/out/$CONFIG"
mkdir -p "$OUT_DIR"

log "Generating build files ($CONFIG, $ABI, minSdk $MIN_SDK)"
(cd "$ANGLE_SRC" && "$GN" gen "$OUT_DIR" --args="
  is_debug = $([[ $CONFIG == Debug ]] && echo true || echo false)
  is_component_build = false
  is_clang = true
  target_os = \"android\"
  target_cpu = \"arm64\"
  android_sdk_path = \"$ANDROID_SDK_ROOT\"
  android_ndk_path = \"$ANDROID_NDK_HOME\"
  default_min_sdk_version = $MIN_SDK
  default_max_sdk_version = $MAX_SDK
  chromeos_component_build = false
  angle_enable_vulkan = true
  angle_enable_gl = true
  angle_enable_d3d11 = false
  angle_enable_metal = false
  angle_enable_trace = false
  angle_enable_debug_overlay = false
  angle_build_all_tests = false
  treat_warnings_as_errors = false
  use_siso = false
")

log "Building libEGL_angle.so and libGLESv2_angle.so with $JOBS jobs"
if [[ -x "$AUTONINJA" ]]; then
    (cd "$ANGLE_SRC" && "$AUTONINJA" -C "$OUT_DIR" -j "$JOBS" libEGL_angle.so libGLESv2_angle.so)
else
    command -v ninja >/dev/null || die "ninja is required (no autoninja in buildtools)"
    ninja -C "$OUT_DIR" -j "$JOBS" libEGL_angle.so libGLESv2_angle.so
fi

# ------------------------------------------------------------------- install --
DEST_DIR="$ROOT_DIR/android/app/src/main/jniLibs/$ABI"
mkdir -p "$DEST_DIR"

install_lib() {
    local src="$1"
    [[ -f "$src" ]] || die "build did not produce $src"
    install -m 0644 "$src" "$DEST_DIR/lib${2}_angle.so"
    log "installed $(basename "$DEST_DIR/lib${2}_angle.so") ($(du -h "$DEST_DIR/lib${2}_angle.so" | cut -f1))"
}

install_lib "$OUT_DIR/libEGL_angle.so" "EGL"
install_lib "$OUT_DIR/libGLESv2_angle.so" "GLESv2"

log "Done. Rebuild the APK so the libraries are packaged:"
log "  cd android && ./gradlew assembleRelease"
log "Then pick \"OpenGL ES -> Vulkan (ANGLE)\" or \"-> OpenGL ES (ANGLE)\" in the launcher."
