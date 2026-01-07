# Development Log: Fixing the `example_vgm_player`

This document details the debugging process undertaken to fix the `example_vgm_player` application, which was initially non-functional. The process involved diagnosing and resolving a series of issues, from build-time linker errors to subtle runtime bugs.

## 1. Initial State

The project failed to build and run correctly. The primary goal was to make the VGM player functional, which included:
- Compiling the application successfully.
- Initializing the audio system.
- Playing VGM music with audible sound.

## 2. Build System and Linker Errors

The first major hurdle was a linker error indicating a symbol conflict. The `libaudio.a` static library contained multiple implementations of the same audio driver functions.

**Solution:**
- Modified `libvgm-player/Makefile` to isolate a single audio driver (DirectSound) for the Windows build.
- Commented out the compilation of other drivers (e.g., `AudDrv_WaveWriter.o`) to ensure only one implementation was archived into `libaudio.a`.

```makefile
# In libvgm-player/Makefile
# ... (other objects)
#AUDDRV_OBJS += $(OBJDIR)/AudDrv_WaveWriter.o
#AUDDRV_OBJS += $(OBJDIR)/AudDrv_WavWriter.o
# ...
```

This resolved the linker errors and allowed the application to compile.

## 3. Runtime Initialization Failures

After fixing the build, the application would run but immediately crash or fail during audio initialization. Debugging with `printf` statements revealed two key problems:

### 3.1. Incorrect Audio Driver Index

The application was attempting to use `AudioOutDrv = 1`, but since we had isolated a single driver, the correct index was `0`.

**Solution:**
- In `examples/example_vgm_player/main.cpp`, changed the `AudioOutDrv` variable to `0`.

```cpp
// In examples/example_vgm_player/main.cpp
UINT8 AudioOutDrv = 0; // Was 1
```

### 3.2. DirectSound Driver Failure (Error `0xF1`)

The `StartAudioDevice` function consistently returned an error code `0xF1`. Investigating `libvgm-player/audio/AudDrv_DSound.cpp` revealed that the DirectSound driver requires a valid window handle (`HWND`) to function correctly. The application was attempting to start the audio device *before* the main window was created.

**Solution:**
- Reordered the initialization logic in `examples/example_vgm_player/main.cpp`.
- Ensured the GLFW window was created first.
- Retrieved the native Win32 `HWND` from the GLFW window.
- Passed the `HWND` to the DirectSound driver using the `DSound_SetHWnd` function *before* calling `StartAudioDevice`.

```cpp
// In examples/example_vgm_player/main.cpp

// ... create GLFW window ...
#if defined(_WIN32)
    HWND hWnd = glfwGetWin32Window(window);
    DSound_SetHWnd(hWnd);
#endif

// Now start the audio device
Res = StartAudioDevice(AudioOutDrv, &VgmPlayer.SampleRate, &VgmPlayer.Channels);
// ...
```

## 4. The Final Bug: Missing Function Prototype

Despite the reordering, the `0xF1` error persisted. The `DSound_SetHWnd(hWnd)` call seemed to have no effect. This pointed to a subtle C/C++ compilation issue.

The prototype for `DSound_SetHWnd` is located in `AudioStream_SpcDrvFuns.h` and is conditionally compiled, guarded by the `AUDDRV_DSOUND` preprocessor macro. When `main.cpp` was compiled, this macro was not defined, so the compiler never saw the function prototype.

Without a prototype, the C++ compiler "guessed" the function signature, assuming it took no arguments. As a result, the `hWnd` argument was never actually passed to the function, the driver's internal `hWnd` remained `NULL`, and initialization failed.

**Solution:**
- Added the `-DAUDDRV_DSOUND` flag to the `CXXFLAGS` in `examples/example_vgm_player/Makefile`.

```makefile
# In examples/example_vgm_player/Makefile
CXXFLAGS = -std=c++11 -I... -DAUDDRV_DSOUND
```

This ensured the compiler saw the correct function prototype for `DSound_SetHWnd`, allowing the `HWND` to be passed correctly. With this final change, the audio device initialized successfully, and the VGM player became fully functional.

## 5. Conclusion

The debugging process was a journey from high-level linker issues to a very specific, low-level C++ compilation bug. The key takeaways were:
- **Isolate Dependencies:** Simplify the build to reduce variables, as was done with the audio drivers.
- **Trace Execution:** Use `printf` or a debugger to understand the runtime flow and pinpoint the exact point of failure.
- **Read the Source:** When a library function fails, read its implementation to understand its requirements (like the `HWND` dependency).
- **Compiler Warnings are Clues:** The lack of a warning about an unknown function signature was a missed clue. Enabling stricter warnings could have caught this earlier. The root cause was a missing preprocessor definition that hid a function prototype from the compiler.

This systematic approach allowed for the successful resolution of a complex series of cascading failures.
