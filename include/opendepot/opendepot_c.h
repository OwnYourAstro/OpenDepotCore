// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

// Stable C ABI for OpenDepotCore.
//
// The C ABI is the long-lived boundary used by WebAssembly and Android/JNI
// consumers. C++ callers should prefer the headers under opendepot/core/.
// Nothing here may throw across the boundary; all failures are reported through
// od_status plus an optional heap-allocated message released with od_free_string.

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- version */

int od_version_major(void);
int od_version_minor(void);
int od_version_patch(void);

/* Returns a NUL-terminated static string. Do not free. */
const char* od_version_string(void);

/* ------------------------------------------------------------- foundation */

typedef enum od_status {
    OD_OK = 0,
    OD_E_IO = 1,
    OD_E_FORMAT = 2,
    OD_E_CRYPTO = 3,
    OD_E_INVALID = 4,
    OD_E_UNSUPPORTED = 5,
    OD_E_BUSY = 6,
    OD_E_CANCELLED = 7,
    OD_E_INTERNAL = 8
} od_status;

/* Releases strings returned through char** out-parameters. NULL is a no-op. */
void od_free_string(char* s);

#ifdef __cplusplus
}  /* extern "C" */
#endif
