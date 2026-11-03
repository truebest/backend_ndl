# backend_ndl

`backend_ndl` is a standalone C11 wrapper around the process-global webOS
NDL DirectMedia facade. It owns runtime symbol loading, the combined
audio/video control plane, feeds independent of the lifecycle mutex, optional buffer guards,
effects, video controls, and firmware callbacks. It has no dependency on
gnomecast, RDP, SDL, or a media framing parser.

The wrapper dlopens `libNDL_directmedia.so.1`; the facade itself loads
`libNDL_directmedia_impl.so.1` and its private dependencies. Applications and
the wrapper never link or dlopen the implementation directly.

## Embedding with CMake

```cmake
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
application therefore has no NDL `DT_NEEDED`. The runtime implementation is
always compiled; a missing DirectMedia facade is reported by
`backend_ndl_open()` at runtime.

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
- `window_id` is accepted only by the window-id profile. The callback profile
  guarantees H.264 plus interleaved S16LE mono/stereo PCM and uses its own
  sample-rate map; HDR, effects, multichannel callbacks and newer video codecs
  are masked.
- The facade owns its internal DL lifecycle. The wrapper never calls the
  exported `DirectMedia_DL_Initialize/Finalize` helpers.
- `backend_ndl_set_media()` replaces both tracks because DirectMedia loads
  audio and video as one pipeline.
- Media audio uses a tagged `BackendNdlAudioInfo` and supports PCM S16LE or
  raw Opus packets. The independent DirectEffect API remains PCM-only.
- `backend_ndl_feed_audio()` and `backend_ndl_feed_video()` never acquire the
  lifecycle mutex. Audio and video may run concurrently and may cross reload/unload.
  The optional GStreamer module uses short private locks for metadata and counters.
- `backend_ndl_get_runtime_stats()` returns a mutex-free coherent snapshot of
  video-feed call count, errors, and `DirectVideoPlay` EWMA/max wall time. The
  counters reset after every successful combined media load.
- A C11 atomic generation snapshot keeps readiness, AUTO PTS and the keyframe
  gate coherent. A crossed generation is returned as transient
  `NOT_READY`/`NEED_KEYFRAME`.
- Ordering within one track belongs to the caller. Stop and join all callers
  before `backend_ndl_close()`; concurrent close is not supported.
- Log and firmware callbacks may execute concurrently and must be thread-safe.
- DirectMedia error text is best-effort under concurrent calls; the operation's
  return code is authoritative.

Optional controls are represented by capabilities. Audio and video feed
symbols are independently optional; unsafe callback-profile
HDR/effect/multichannel signatures are never exposed as capabilities.

## ABI audit

Run the local audit against libraries copied from the TV:

```sh
backend_ndl/tools/audit-tv-libs.sh tmp/tvlibs-27U731SA-20260715
```

The script classifies callback-init/window-id profiles, rejects incomplete v2
control planes, labels link stubs, and reports hashes, build IDs, SONAMEs,
dependencies and optional symbols. TV binaries are never installed or
redistributed by this project.

## Optional firmware GStreamer integration

`BACKEND_NDL_WITH_GSTREAMER=ON` adds private control of the GStreamer pipeline
created by NDL. NDL continues to acquire hardware resources and own media loading.
The default OFF build has no GStreamer dependency. With the option enabled,
GStreamer and gst-app development packages >= 1.18 are required. The exported
CMake target supplies dependencies and interposition exports, including for an
installed-package consumer; applications need no hook declarations or preload.

Set `BackendNdlVideoInfo.app_type` and `BackendNdlAudioInfo.app_type` independently
before `backend_ndl_set_media()`. Zero (`BACKEND_NDL_APP_DEFAULT`) preserves the
firmware route. Explicit profiles are RTC, MIRACAST, ATSC3, CAMERA, JP4K, AIRPLAY,
DIRECTMEDIA, APPCAST, R2R and SYE. They select vendor plugin policies, not complete
implementations of the named services. This version supports explicit profiles
on H.264/H.265 video and PCM audio. Other codecs retain DEFAULT support.
Unknown profile values are invalid; builds without this module reject explicit
profiles as unsupported. Profile changes use the ordinary combined load/rollback
lifecycle. A failed load's objects cannot deliver events or errors to its replacement.

Profile names do not promise compatibility on every TV. Some require extra
firmware context. An earlier R2R experiment was followed by a TV reboot; its
component-level cause remains unknown. Profiles are never probed automatically.
RTC PCM retains `sync=false, async=false`. Profile side effects on firmware clocks
and buffering are separate from GStreamer sink scheduling and physical presentation.

`backend_ndl_feed_video_ex()` accepts flags:

- `0`: decode an image and permit its output.
- `BACKEND_NDL_VIDEO_DECODE_ONLY`: decode the image and suppress its output.
- `BACKEND_NDL_VIDEO_CONFIG_ONLY`: parameter-only AU, with no expected image.

CONFIG_ONLY cannot accompany DECODE_ONLY or the keyframe argument. With the
keyframe gate armed, it returns NEED_KEYFRAME like other non-keyframe input;
it does not clear that gate. Unknown flags return INVALID_ARGUMENT. The original
feed function is the flags-zero wrapper. Tracked parameter-only AUs must use the
extended API. Metadata is copied before return;
the original byte-buffer lifetime contract is unchanged. No bitstream parser or
application-specific picture classification is included in this library.

`backend_ndl_get_video_status()` reports readiness, a latched error, generation,
feature availability, input queue occupancy and picture counters. DECODE_ONLY
returns UNSUPPORTED without an observable decoder-output path. H.264 profiles
that retain the OMX output pads support tracking; DIRECTMEDIA bypasses those pads.
H.265 tracking is not implemented. The feed checks latched errors before and
after submission. Limits are 512 queued buffers, 32 MiB and 128 pending images.
Flush invalidates pending identities; input and output remain ordered by metadata,
including when original PTS values collide. Queue saturation fails explicitly.

Generations use 29 bits and restart at 1 after 536870911. Together with three
flags they fit in one atomic 32-bit feed state. Capabilities, feed counters,
picture counters and the runtime-statistics epoch also use 32-bit storage.
Feed and picture counters wrap modulo 2^32, so compute deltas with unsigned arithmetic.
Load ownership remains tied to distinct private contexts when generations repeat.
Exhausting a flush epoch latches an error until reload so stale input identities
cannot be reused. Timestamps remain 64-bit values.

Runtime-statistics slots retain 64-bit atomic storage to update a generation tag
and a full-range 32-bit metric together; EWMA also packs its initialized bit.
NDL PTS/callback numbers, monotonic clock values and GStreamer timestamps retain
their required 64-bit types. The GStreamer `max-bytes` property is also 64-bit,
independently of the wrapper's smaller queue limit.

Optional `BackendNdlConfig.video_frame_fn` reports each matched output once,
including suppressed images. Events carry the load-attempt generation, original
PTS in milliseconds, monotonic submission/output times in microseconds and the
suppression flag. Their time difference includes queueing. CONFIG_ONLY, stale
outputs and errors do not produce image events. Without a supported tracking
path there are no events. Existing synchronous DirectVideoPlay timings remain
available through `backend_ndl_get_runtime_stats()`.

Callbacks run outside internal locks on streaming/feed threads. Keep them short,
thread-safe and non-reentrant into the same context. Event memory expires on
return. Stop API/feed callers before close as usual; close drains callbacks.
Probes or retained buffers surviving close hold detached private state, never a
freed application context. Events and forwarded counters do not establish panel
FPS or physical presentation. There is no per-frame success logging.

All consumers must rebuild against the updated header: the changed public types
and structures do not preserve binary compatibility with earlier headers.
