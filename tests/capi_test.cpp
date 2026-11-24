// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/opendepot_c.h"

#include <cstdio>
#include <cstring>

static bool StartsWith(const char* s, char c) { return s && *s == c; }

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: capi_test <codeplug> [profiles.bin]\n");
        return 2;
    }
    if (!od_version_string() || std::strlen(od_version_string()) == 0) {
        std::printf("version string missing\n");
        return 1;
    }

    char* err = nullptr;
    od_codeplug* cp = od_codeplug_open(argv[1], &err);
    if (!cp) {
        std::printf("open failed: %s\n", err ? err : "");
        od_free_string(err);
        return 1;
    }

    char* report = od_codeplug_report_json(cp);
    if (!StartsWith(report, '{')) {
        std::printf("report json invalid\n");
        od_free_string(report);
        od_codeplug_close(cp);
        return 1;
    }
    od_free_string(report);

    char* model = od_codeplug_model_json(cp);
    if (!StartsWith(model, '{')) {
        std::printf("model json invalid\n");
        od_free_string(model);
        od_codeplug_close(cp);
        return 1;
    }
    od_free_string(model);

    char* models = od_list_models_json();
    if (!StartsWith(models, '[')) {
        std::printf("models json invalid\n");
        od_free_string(models);
        od_codeplug_close(cp);
        return 1;
    }
    od_free_string(models);

    if (argc >= 3) {
        char* profiles = od_list_depot_profiles_json(argv[2], &err);
        if (!StartsWith(profiles, '[')) {
            std::printf("profiles json invalid: %s\n", err ? err : "");
            od_free_string(err);
            od_free_string(profiles);
            od_codeplug_close(cp);
            return 1;
        }
        od_free_string(profiles);

        const char* out = "capi-created.cpg";
        std::remove(out);
        if (!od_create_from_profile(argv[2], "H46UCF9PW6BN", "", "TEST123", "", out, &err)) {
            std::printf("create failed: %s\n", err ? err : "");
            od_free_string(err);
            od_codeplug_close(cp);
            return 1;
        }
        od_codeplug* created = od_codeplug_open(out, &err);
        if (!created) {
            std::printf("created open failed: %s\n", err ? err : "");
            od_free_string(err);
            od_codeplug_close(cp);
            return 1;
        }
        od_codeplug_close(created);
        std::remove(out);
    }

    od_codeplug_close(cp);
    std::printf("capi ok\n");
    return 0;
}
