// dobby.h - Dobby Inline Hook Framework (minimal API header)
// https://github.com/jmpews/Dobby
// Only the functions needed for this project are declared.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

// ============================================================
// Hook a function by replacing its entry with a jump to new_func.
// The original function logic is preserved and accessible via origin_func.
//
// Parameters:
//   target_func  - Address of the function to hook (in target process)
//   replace_func - Address of the replacement function
//   origin_func  - [out] Pointer that will receive the original function trampoline
//
// Returns 0 on success, non-zero on failure.
// ============================================================
int DobbyHook(void *target_func, void *replace_func, void **origin_func);

// ============================================================
// Restore a previously hooked function.
// This removes the inline hook and restores the original instructions.
//
// Parameters:
//   target_func - Address of the hooked function
//
// Returns 0 on success, non-zero on failure.
// ============================================================
int DobbyDestroy(void *target_func);

// ============================================================
// Enable/disable debug logging from Dobby internals.
// Set to 1 to enable debug output via logcat (tag: Dobby).
// ============================================================
int DobbyGlobalSetDebugLog(int enabled);

// ============================================================
// Manually patch memory protection (useful for SELinux contexts).
// ============================================================
int DobbyCodePatch(void *address, const void *buffer, size_t size);

#if defined(__cplusplus)
}
#endif
