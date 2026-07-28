# backend_ndl

`backend_ndl` 0.3 is a standalone C11 wrapper around the process-global webOS
NDL DirectMedia facade. It owns runtime symbol loading, the combined
audio/video control plane, mutex-free feed paths, optional buffer guards,
effects, video controls, and firmware callbacks. It has no dependency on
gnomecast, RDP, SDL, or a media framing parser.

The wrapper dlopens `libNDL_directmedia.so.1`; the facade itself loads
`libNDL_directmedia_impl.so.1` and its private dependencies. Applications and
the wrapper never link or dlopen the implementation directly.

## Embedding with CMake

```cmake
set(BACKEND_NDL_ENABLE ON)
set(BACKEND_NDL_BUILD_TESTS OFF)
set(BACKEND_NDL_INSTALL OFF)
add_subdirectory(path/to/backend_ndl)

target_link_libraries(my_app PRIVATE backend_ndl::backend_ndl)
```

Production builds use minimal, independently maintained private declarations
for both supported DirectMedia ABI profiles. Static assertions pin the ARM32
combined-media layouts. The wrapper therefore needs no NDK headers, libraries,
compiler, sysroot, or `pkg-config` metadata.

The wrapper resolves every NDL entry point with `dlopen`/`dlsym`; the final
application therefore has no NDL `DT_NEEDED`.

Standalone build and install:

```sh
cmake -S backend_ndl -B /tmp/backend-ndl-build
cmake --build /tmp/backend-ndl-build
ctest --test-dir /tmp/backend-ndl-build --output-on-failure
cmake --install /tmp/backend-ndl-build --prefix /tmp/backend-ndl-prefix
```

Host tests use the system-provided GoogleTest package (`GTest::gtest_main`).
They are disabled automatically for cross-compilation and are never linked
into `backend_ndl` or the product executable. Host testing does not require the
webOS NDK.

When an official target-NDK header is available, enable the separate
conformance test by pointing CMake at its target include root:

```sh
cmake -S backend_ndl -B /tmp/backend-ndl-conformance \
  -DBACKEND_NDL_NDK_INCLUDE_DIR=/path/to/target-sysroot/usr/include
cmake --build /tmp/backend-ndl-conformance
ctest --test-dir /tmp/backend-ndl-conformance \
  -R backend-ndl-ndk-conformance --output-on-failure
```

The test compares enum values, sizes, alignment, member offsets, callbacks and
all DirectMedia function signatures. It is skipped when the NDK is absent.
The include root must be the ARM target sysroot; the x86 host-tools sysroot is
rejected. The official headers are reference-only inputs to this test and are
never used by the production library.

ThreadSanitizer concurrency run:

```sh
cmake -S backend_ndl -B /tmp/backend-ndl-tsan \
  -DBACKEND_NDL_ENABLE_TSAN=ON
cmake --build /tmp/backend-ndl-tsan
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/backend-ndl-tsan -R backend-ndl-concurrency \
  --repeat until-fail:20 --output-on-failure
```

## API and concurrency boundary

- A non-empty `app_id` is required. Optional `webos_sdk_version` is copied and
  versions below SDK 5 are rejected before loading the facade.
- `NDL_DirectMediaSetWindowId` selects the current
  `DirectMediaInit(app_id)` profile. Its absence selects the webOS 5+
  `DirectMediaInit(app_id, NULL)` callback profile. `Load` and `Unload` are
  required in both cases; an Init signature is never retried after failure.
- `window_id` is accepted only by the window-id profile. Legacy mode guarantees
  H.264 plus interleaved S16LE mono/stereo PCM and uses its own sample-rate map;
  HDR, effects, multichannel callbacks and newer video codecs are masked.
- The facade owns its internal DL lifecycle. The wrapper never calls the
  exported `DirectMedia_DL_Initialize/Finalize` helpers.
- `backend_ndl_set_media()` replaces both tracks because DirectMedia loads
  audio and video as one pipeline.
- `backend_ndl_feed_audio()` and `backend_ndl_feed_video()` acquire no wrapper
  mutex. Audio and video may run concurrently and may cross reload/unload.
- A C11 atomic generation snapshot keeps readiness, AUTO PTS and the keyframe
  gate coherent. A crossed generation is returned as transient
  `NOT_READY`/`NEED_KEYFRAME`.
- Ordering within one track belongs to the caller. Stop and join all callers
  before `backend_ndl_close()`; concurrent close is not supported.
- Log and firmware callbacks may execute concurrently and must be thread-safe.
- DirectMedia error text is best-effort under concurrent calls; the operation's
  return code is authoritative.

Optional controls are represented by capabilities. Audio and video feed
symbols are independently optional; unsafe legacy HDR/effect/multichannel
signatures are never exposed as capabilities.

## ABI audit

Run the local audit against libraries copied from the TV:

```sh
backend_ndl/tools/audit-tv-libs.sh tmp/tvlibs-27U731SA-20260715
```

The script classifies callback-init/window-id profiles, rejects incomplete v2
control planes, labels link stubs, and reports hashes, build IDs, SONAMEs,
dependencies and optional symbols. TV binaries are never installed or
redistributed by this project.
