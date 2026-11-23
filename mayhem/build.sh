#!/usr/bin/env bash
#
# mayhem/build.sh — Mir fuzz harnesses + the upstream unit tests that cover the same code.
#
# Mir's full build (CMake, C++26, a Cargo workspace, ~40 -dev packages, Wayland >= 1.24) is not
# needed for these targets: each harness compiles the exact upstream sources of the code it fuzzes
# straight from the tree, with the fuzz flags:
#   fuzz_xcursor_theme   src/miral/xcursor.c                         (Xcursor theme/file parser)
#   fuzz_live_config_ini src/miral/{live_config,basic_store,live_config_ini_file*}.cpp
#   fuzz_display_config_yaml src/miral/static_display_config.cpp (+ yaml-cpp, mircore's
#                        graphics/display_configuration.cpp + libdisplay-info)
#   all of them          src/core/{log,fatal}.cpp, src/core/logging/*.cpp (mir::log backend)
# The test build compiles the same sources with normal flags plus upstream's gtest suites
# tests/miral/{live_config,live_config_ini_file,static_display_config}.cpp, and a known-answer
# check of xcursor.c against upstream's testing cursor theme.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"            # gitignored by mayhem/.gitignore; rebuilt from scratch every run
rm -rf "$OUT"
mkdir -p "$OUT/fuzz" "$OUT/test"

CXXSTD="-std=c++2c"
# Defines and include dirs as Mir's CMake sets them for these sources.
DEFS=(-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DMIR_LOG_COMPONENT_FALLBACK=\"miral\" -DMIRAL_ENABLE_DEPRECATIONS=0)
INCS=(-Iinclude/core -Iinclude/common -Iinclude/platform -Iinclude/server -Iinclude/miral
      -Isrc/miral -Isrc/include/common -Isrc/include/platform -Isrc/include/server)
C_SRCS=(src/miral/xcursor.c)
CXX_SRCS=(
  src/miral/live_config.cpp
  src/miral/basic_store.cpp
  src/miral/live_config_ini_file_common.cpp
  src/miral/live_config_ini_file.cpp
  src/miral/static_display_config.cpp
  src/core/graphics/display_configuration.cpp
  src/core/output_type_names.cpp
  src/core/fd.cpp
  src/server/graphics/default_display_configuration_policy.cpp
  src/core/log.cpp
  src/core/fatal.cpp
  src/core/logging/logger.cpp
  src/core/logging/dumb_console_logger.cpp
  src/core/logging/tag.cpp
)
# static_display_config.cpp also holds ReloadingYamlFileDisplayConfig/StaticDisplayConfig, whose
# inotify auto-reload needs a running mir::Server. Nothing here calls them, and with per-function
# sections the linker drops them (and their references into mirserver) instead of failing.
SECT="-ffunction-sections -fdata-sections"
GC="-fuse-ld=lld -Wl,--gc-sections"
# libdisplay-info 0.3 is built into the image by mayhem/Dockerfile (Debian's 0.2 is too old).
: "${LDI_PREFIX:=/opt/toolchains/deps/libdisplay-info-0.3.0}"
INCS+=(-I"$LDI_PREFIX/include")
LIBS=(-lyaml-cpp "$LDI_PREFIX/lib/libdisplay-info.a" -lm)

# Compile the project sources with <flags> into <outdir>/libmirfuzzsrc.a (parallel, MAYHEM_JOBS).
build_lib() {  # <outdir> <flags>
  local out="$1" flags="$2" pids=() objs=() s o
  mkdir -p "$out"
  for s in "${C_SRCS[@]}" "${CXX_SRCS[@]}"; do
    o="$out/$(echo "$s" | tr / _).o"; objs+=("$o")
    case "$s" in
      *.c) $CC  $flags $SECT "${DEFS[@]}" -Isrc/miral -c "$s" -o "$o" & ;;
      *)   $CXX $flags $SECT $CXXSTD "${DEFS[@]}" "${INCS[@]}" -c "$s" -o "$o" & ;;
    esac
    pids+=($!)
    if [ "${#pids[@]}" -ge "$MAYHEM_JOBS" ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
  rm -f "$out/libmirfuzzsrc.a"
  ar rcs "$out/libmirfuzzsrc.a" "${objs[@]}"
}

# --- 1) sanitized project code, with SanCov (-fsanitize=fuzzer-no-link) on the project itself ---
FZ="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"
build_lib "$OUT/fuzz" "$FZ"
$CXX $FZ -c mayhem/lsan_off.cc -o "$OUT/fuzz/lsan_off.o"
$CC  $FZ -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"

# --- 2) harnesses: libFuzzer binary + standalone reproducer each ------------------------------
for h in fuzz_xcursor_theme fuzz_live_config_ini fuzz_display_config_yaml; do
  case "$h" in
    fuzz_xcursor_theme) $CC $FZ "${DEFS[@]}" -Isrc/miral -c "mayhem/harnesses/$h.c" -o "$OUT/fuzz/$h.o" ;;
    *) $CXX $FZ $CXXSTD "${DEFS[@]}" "${INCS[@]}" -c "mayhem/harnesses/$h.cpp" -o "$OUT/fuzz/$h.o" ;;
  esac
  $CXX $FZ $GC $LIB_FUZZING_ENGINE "$OUT/fuzz/$h.o" "$OUT/fuzz/libmirfuzzsrc.a" "$OUT/fuzz/lsan_off.o" \
    "${LIBS[@]}" -pthread -o "$SRC/$h"
  $CXX $FZ $GC "$OUT/fuzz/standalone_main.o" "$OUT/fuzz/$h.o" "$OUT/fuzz/libmirfuzzsrc.a" "$OUT/fuzz/lsan_off.o" \
    "${LIBS[@]}" -pthread -o "$SRC/$h-standalone"
done

# --- 3) test build, normal flags --------------------------------------------------------------
TF="-O1 $COVERAGE_FLAGS"
build_lib "$OUT/test" "$TF"
$CXX $TF $GC $CXXSTD "${DEFS[@]}" "${INCS[@]}" -Itests/include \
  tests/miral/live_config.cpp tests/miral/live_config_ini_file.cpp tests/miral/static_display_config.cpp \
  "$OUT/test/libmirfuzzsrc.a" "${LIBS[@]}" -lgmock_main -lgmock -lgtest -pthread -o "$OUT/test/miral-config-tests"
$CC $TF "${DEFS[@]}" -Isrc/miral mayhem/xcursor_selftest.c "$OUT/test/libmirfuzzsrc.a" -pthread -o "$OUT/test/xcursor-selftest"

echo "build.sh: done"
