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

/* -------------------------------------------------------------- codeplug */

typedef struct od_codeplug od_codeplug;

/* Opens a .cpg. Returns NULL on failure and optionally sets *err. */
od_codeplug* od_codeplug_open(const char* path, char** err);
void od_codeplug_close(od_codeplug* cp);
int od_codeplug_save(od_codeplug* cp, const char* path, char** err);

/* Heap JSON strings; release with od_free_string. */
char* od_codeplug_report_json(od_codeplug* cp);
char* od_codeplug_model_json(od_codeplug* cp);
char* od_codeplug_flashcode(od_codeplug* cp);

/* Returns 1 when the codeplug is safe for a radio write, else 0 and a message. */
int od_codeplug_validate_radio(od_codeplug* cp, char** message);

/* ------------------------------------------------------------- catalogs */

char* od_list_depot_profiles_json(const char* database_path, char** err);
char* od_list_models_json(void);
char* od_list_packages_json(const char* model);
char* od_list_addons_json(const char* model);

/* Creates a codeplug from a native profile (empty package picks the first
 * profile for the model) and writes it to out_path. */
int od_create_from_profile(const char* database_path, const char* model, const char* package,
                           const char* serial, const char* flashcode, const char* out_path,
                           char** err);

#ifdef __cplusplus
}  /* extern "C" */
#endif
