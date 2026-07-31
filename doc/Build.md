## Build Instructions

RiftRay currently builds on Windows with CMake and a vcpkg manifest. The desktop
OpenXR backend uses GLFW, WGL, and OpenGL; the Android graphics/lifecycle backend
described in `OPENXR_MIGRATION.md` is follow-on work.

Install Visual Studio 2022 with the Desktop development with C++ workload, CMake,
and vcpkg. Configure with the repository's `vcpkg.json` manifest and the vcpkg
toolchain file.

### Windows

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B out/build/x64-Release `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build out/build/x64-Release
```

The build copies `openxr_loader.dll`, GLFW, GLEW, AntTweakBar, shaders, and
textures beside `RiftRay3.exe`. Select SteamVR or Meta Quest Link as the active
OpenXR runtime before starting the executable for VR. Without an available
runtime/HMD, the same executable starts in monitor mode and records the reason in
`RiftRay-log.txt`.

The current OpenXR graphics binding is Windows-only. Other desktop platforms need
their native OpenGL binding implementation before they can be enabled in CMake.
