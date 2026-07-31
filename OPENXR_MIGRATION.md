# RiftRay OpenXR migration

## Purpose and scope

This document is the implementation record and completion checklist for replacing
the Oculus PC SDK (LibOVR) with OpenXR.

The current target is Windows desktop VR using GLFW, WGL, and OpenGL. RiftRay uses
the operating system's active OpenXR runtime, so the same binary can use SteamVR,
the Meta PC runtime, or another conformant runtime that exposes
`XR_KHR_opengl_enable`.

Android is a follow-on port. The desktop migration deliberately does not include an
Android activity, EGL/OpenGL ES rendering, APK packaging, or Android asset loading.
The Android section below defines the boundary and work order for that separate port.

## Current status

Last updated: 2026-07-31.

| Area | Status | Implementation |
|---|---|---|
| Loader and dependency | Complete | vcpkg `openxr-loader`; `OpenXR::openxr_loader` |
| Runtime/system discovery | Complete | `OpenXRBackend::Initialize` |
| WGL session and spaces | Complete | LOCAL and VIEW spaces |
| Stereo swapchains | Complete | One runtime-sized OpenGL swapchain per view |
| Frame lifecycle | Complete | wait, begin, locate, acquire, wait, render, release, end |
| Desktop mirror | Complete | Left-eye framebuffer blit to the GLFW window |
| Input actions | Complete in code | Touch, Valve Index, and simple-controller suggestions |
| Controller aim pose | Complete in code | Right-hand aim action space at predicted display time |
| HUD compositor quad | Complete in code | Independent OpenXR swapchain and quad layer |
| LibOVR removal | Complete | No Oculus includes, libraries, types, or API calls |
| Monitor fallback | Smoke-tested | Invalid runtime manifest leaves the app running in monitor mode |
| SteamVR startup | Smoke-tested | Instance, system, WGL session, actions, swapchains, and VISIBLE state |
| Visual/controller checks | Manual check remains | Confirm in-headset image, controls, HUD interaction |
| Meta PC runtime | Manual check remains | Repeat runtime matrix with Meta selected |
| Validation API layer | Optional development check remains | Run when the Khronos validation layer is installed |
| Android graphics binding | Boundary implemented | EGL/OpenGL ES adapter is compile-gated for Android |
| Android application/build | Not started | Follow the separate Android plan below |

“Complete in code” means the path builds and is connected to the application.
It does not claim a controller or headset model was physically verified unless the
runtime matrix records that check.

## Implementation checkpoints

The migration was committed in reviewable stages:

- `d677fd2` — OpenXR loader and discovery backend.
- `2cf3009` — OpenXR pose and projection utilities.
- `c9774e3` — WGL session, spaces, and stereo swapchain lifecycle.
- `0f9dc29` — OpenXR frame rendering, actions, controller pose, and HUD.
- `f0d75ec` — LibOVR removal, entry-point rename, clean builds, and tests.
- `f39a414` — Platform graphics-binding interface plus WGL and Android EGL adapters.

## Source layout and ownership

`src/Util/XR/OpenXRBackend.h/.cpp` owns OpenXR runtime state:

- `XrInstance`, `XrSystemId`, `XrSession`, session state, and frame state.
- LOCAL application space, VIEW space, and the right-hand aim action space.
- Runtime-supported environment blend mode.
- PRIMARY_STEREO view configuration, located views, and per-view swapchains.
- Per-image OpenGL texture names, framebuffer/depth objects, and acquisition state.
- Gameplay action set, actions, suggested bindings, and synchronized input state.
- Runtime result formatting and the `[RiftRay OpenXR]` diagnostic prefix.

`src/main_glfw_openxr.cpp` owns application policy:

- GLFW window/context creation and monitor rendering.
- Scene rendering and chassis/world movement.
- Mapping OpenXR action state to gallery, HUD, movement, and turning behavior.
- Left-eye mirroring and construction of the optional HUD quad layer.

`src/Scene/HudQuad.*`, `MousingQuad.*`, and `AntQuad.*` own the UI quad's
OpenXR swapchain and its OpenGL rendering.

Scene and shader-gallery classes remain unaware of OpenXR handles. They receive GLM
view/projection matrices as before.

## Runtime lifecycle contract

Initialization order:

1. Enumerate instance extensions and require `XR_KHR_opengl_enable`.
2. Create the OpenXR instance and query runtime properties.
3. Obtain the HMD system and enumerate supported blend modes.
4. Create the GLFW OpenGL context.
5. Load and call `xrGetOpenGLGraphicsRequirementsKHR`.
6. Validate the active OpenGL version.
7. Create the session with `XrGraphicsBindingOpenGLWin32KHR`.
8. Create LOCAL and VIEW reference spaces.
9. Create, suggest bindings for, and attach the action set.
10. Create the aim action space.
11. Enumerate PRIMARY_STEREO view configuration and swapchain formats.
12. Create one color swapchain and framebuffer/depth target per view.
13. Create the HUD swapchain.

Session begin/end is event-driven:

- READY calls `xrBeginSession`.
- STOPPING calls `xrEndSession`.
- EXITING and LOSS_PENDING request application exit.
- `XR_EVENT_UNAVAILABLE` is the normal end of event polling.

Per-frame order:

1. `xrWaitFrame`
2. `xrBeginFrame`
3. `xrSyncActions` when the session is focused
4. `xrLocateViews` at `predictedDisplayTime`
5. For each view: acquire, wait, attach, render, detach, release
6. Render/release the HUD swapchain when visible
7. Submit projection and optional quad layers through `xrEndFrame`

When `shouldRender` is false, RiftRay still ends the begun frame with zero layers.
If a view fails, all outstanding images are released and no partial projection layer
is submitted.

Shutdown order:

1. Release outstanding swapchain images.
2. Destroy aim and VIEW spaces.
3. Destroy view/HUD swapchains and GL framebuffer resources.
4. Destroy LOCAL space and session.
5. Destroy the action set.
6. Destroy the instance.

GL resources are destroyed while the GLFW context still exists.

## Rendering and coordinate contract

OpenXR and RiftRay use right-handed coordinates with +Y up and -Z forward. The port
does not apply a blanket handedness conversion.

`makeMatrixFromXrPose` converts an `XrPosef` to a GLM transform. The existing
`headSize` option scales translation only; it does not alter orientation.

`makeProjectionFromXrFov` builds the OpenGL projection from all four asymmetric
`XrFovf` angles. Unit tests cover symmetric and asymmetric frusta, pose scale, and
the negative-Z forward ray.

Dynamic framebuffer scale changes the submitted image rectangle. Cinemascope uses a
scissor within that rectangle. The runtime-recommended swapchain allocation remains
unchanged.

OpenXR has no vendor mirror object. RiftRay blits the rendered left-eye framebuffer
to the default GLFW framebuffer before releasing the image.

## Input contract

| Application behavior | OpenXR action | Touch binding |
|---|---|---|
| Move | `move` vector2 | Left thumbstick |
| Turn | `turn` vector2 | Right thumbstick |
| Move down/up | Two float actions | Left/right trigger |
| Enter/exit shader | `toggle_shader` | A |
| Show/hide HUD | `toggle_hud` | B |
| HUD click | `click` | X |
| Reset position | `reset` | Y |
| Reset position/yaw | `menu` | Left menu |
| Hold/move HUD | `hold` | Right thumbstick click |
| HUD ray | `aim_pose` | Right aim pose |

Valve Index bindings use the corresponding sticks, triggers, A/B buttons, right
thumbstick click, and right aim pose. The simple-controller profile binds select,
menu, and right aim pose.

Actions are attached before the session begins. Input is synchronized only in
FOCUSED state. Inactive actions return neutral values. The HUD uses the located
controller aim pose when valid and falls back to the left-eye ray otherwise.

## Failure and diagnostics policy

All migration-specific messages begin with `[RiftRay OpenXR]` and are written to
`RiftRay-log.txt` in Debug and Release builds.

No runtime, no HMD, a missing required extension, or failure before a session is
created leaves `g_hasHMD` false. The normal application then uses the monitor path
and never calls frame/session functions on null handles.

A test using a deliberately missing `XR_RUNTIME_JSON` manifest recorded the loader
failure and left the application running in monitor mode.

## Reproducible build and automated checks

Run from a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B out/audit/x64-Debug-openxr -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build out/audit/x64-Debug-openxr
ctest --test-dir out/audit/x64-Debug-openxr --output-on-failure

cmake -S . -B out/audit/x64-Release-openxr -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build out/audit/x64-Release-openxr
ctest --test-dir out/audit/x64-Release-openxr --output-on-failure
```

Both clean configurations and their matrix tests passed on 2026-07-31. Compiler
warnings remain in pre-existing application/third-party code; there were no build
errors.

## Desktop runtime verification matrix

Do not block code progress on unavailable hardware. Record what was actually tested
and leave unavailable rows pending.

For each runtime:

1. Select it as the active OpenXR runtime using that runtime's settings.
2. Start the runtime and connect/wake the headset.
3. Launch the Release build from its output directory.
4. Search the whole log for `[RiftRay OpenXR]`.
5. Confirm instance/system names, WGL requirements, actions, two swapchains, and
   session transitions without an OpenXR error.
6. In the headset, confirm stereo scale, head tracking, and no inverted projection.
7. Confirm left-eye desktop mirror.
8. Confirm movement, smooth/snap turn, triggers, A/B/X/Y/menu, and HUD hold.
9. Hide/show the HUD and confirm alpha composition and pointer/click behavior.
10. Close RiftRay and confirm no retained process or runtime handle error.

| Runtime | Startup/session | Stereo visually checked | Input/HUD checked |
|---|---|---|---|
| SteamVR 2.16.7, Meta compatibility mode | Passed 2026-07-31 through VISIBLE; no logged OpenXR error | Pending | Pending |
| Meta PC OpenXR | Pending | Pending | Pending |

The SteamVR smoke test reported a Vive OpenXR system, OpenGL 4.3 compatibility,
attached actions, two 2112×2304 three-image swapchains, and session progression
through VISIBLE.

### Optional validation-layer run

When the Khronos validation layer is installed, enable
`XR_APILAYER_LUNARG_core_validation` through the loader's normal API-layer
mechanism, run the same short scenario, and inspect the complete
`[RiftRay OpenXR]` log plus validation output. This is a development diagnostic,
not a runtime dependency.

## Desktop completion gate

The Windows migration is ready to merge/release when:

- [x] Clean x64 Debug configure/build passes.
- [x] Clean x64 Release configure/build passes.
- [x] Matrix/projection tests pass in both configurations.
- [x] LibOVR is absent from active source and build configuration.
- [x] Missing-runtime monitor fallback stays running and logs the reason.
- [x] SteamVR creates the instance, system, session, actions, and stereo swapchains.
- [ ] SteamVR stereo output, tracking, mirror, controls, and HUD are visually checked.
- [ ] Meta PC runtime repeats the startup and visual/input checks.
- [ ] Optional validation-layer run has no lifetime or frame-order findings, when the
      layer is available.

The unchecked hardware/runtime rows are verification tasks, not missing code paths.

## Android follow-on plan

Android is not produced by the current CMake target. Implement it as a separate
milestone after the Windows OpenXR backend is stable.

### Boundary to introduce

Split the backend into shared OpenXR policy and a graphics/application adapter:

| Shared OpenXR core | Windows adapter | Android adapter |
|---|---|---|
| Events/session state | GLFW lifecycle | NativeActivity/GameActivity lifecycle |
| Frame timing | WGL context | EGL context |
| Views/actions/layers | `XR_KHR_opengl_enable` | `XR_KHR_opengl_es_enable` |
| Swapchain ownership | `XrSwapchainImageOpenGLKHR` | `XrSwapchainImageOpenGLESKHR` |
| Input policy | Win32 loader packaging | Android loader/runtime initialization |
| Composition policy | Desktop mirror | Android surface presentation |

The adapter interface must provide:

- Required graphics extension name.
- Graphics-requirements query and validation.
- Platform graphics binding passed to `xrCreateSession`.
- Swapchain image enumeration and texture identifier extraction.
- GL/GLES framebuffer creation/destruction hooks.
- Application lifecycle state needed to decide when a session may run.
- Asset open/list operations independent of relative filesystem paths.

### Android work order

1. [x] Refactor WGL-specific requirements, session binding, and image enumeration
   behind the adapter without changing desktop behavior.
2. Add an Android CMake/Gradle target and OpenXR loader packaging.
3. Add NativeActivity or GameActivity lifecycle and an EGL OpenGL ES context.
4. Implement `XrGraphicsBindingOpenGLESAndroidKHR` and GLES swapchain images.
5. Make shaders GLES 3.x compatible:
   - version directive;
   - precision qualifiers;
   - unsupported desktop GLSL features;
   - texture format and extension assumptions.
6. Load packaged shaders/textures through `AAssetManager`.
7. Replace AntTweakBar on Android or render a portable in-scene UI.
8. Reuse shared frame/session/action policy.
9. Add Android packaging, permissions, ABI, signing, and device deployment.
10. Validate lifecycle suspend/resume, headset removal, controller profiles,
    performance, thermals, and swapchain ownership on device.

### Android completion gate

- Android arm64 package builds reproducibly.
- The loader/runtime initializes through the Android OpenXR path.
- EGL/GLES requirements and swapchain formats are queried, not assumed.
- App pause/resume and surface loss do not leak or reuse invalid handles.
- At least the curated default shader set compiles under GLES.
- Packaged shader/texture discovery works without desktop relative paths.
- Stereo rendering, controller actions, HUD replacement, and frame timing work on
  target hardware.
- Desktop Debug/Release builds and tests still pass after the shared-core refactor.
