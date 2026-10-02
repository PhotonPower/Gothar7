#pragma once

/// Requests the high-performance GPU on systems with two GPUs (laptops with NVIDIA Optimus or AMD
/// switchable graphics). Without it, Windows starts OpenGL programs on the integrated GPU.
///
/// The drivers look for these exported symbols in the *executable*, so the macro must be used once
/// at global scope in a source file of each executable (not in a library):
///
///     G7_REQUEST_HIGH_PERFORMANCE_GPU();
///
/// No effect on other platforms or on systems with a single GPU.
#if defined(_WIN32)
#define G7_REQUEST_HIGH_PERFORMANCE_GPU()                                                                    \
    extern "C" __declspec(dllexport) unsigned long NvOptimusEnablement = 1;                                  \
    extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1
#else
#define G7_REQUEST_HIGH_PERFORMANCE_GPU() static_assert(true, "")
#endif
