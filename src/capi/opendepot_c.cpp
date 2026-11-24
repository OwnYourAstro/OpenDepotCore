// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/opendepot_c.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "opendepot/core/adk.h"
#include "opendepot/core/depot_profiles.h"
#include "opendepot/core/version.h"

namespace {

char* Dup(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

void SetErr(char** err, const std::string& msg) {
    if (err) *err = Dup(msg);
}

std::string JsonEscape(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(c));
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    o += "\"";
    return o;
}

char* StringArrayJson(const std::vector<std::string>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += JsonEscape(values[i]);
    }
    out += "]";
    return Dup(out);
}

}  // namespace

struct od_codeplug {
    opendepot::Codeplug cp;
};

extern "C" {

int od_version_major(void) { return opendepot::VersionMajor(); }
int od_version_minor(void) { return opendepot::VersionMinor(); }
int od_version_patch(void) { return opendepot::VersionPatch(); }
const char* od_version_string(void) { return opendepot::VersionString(); }

void od_free_string(char* s) { std::free(s); }

od_codeplug* od_codeplug_open(const char* path, char** err) {
    if (err) *err = nullptr;
    if (!path) {
        SetErr(err, "no path");
        return nullptr;
    }
    auto* handle = new (std::nothrow) od_codeplug();
    if (!handle) {
        SetErr(err, "out of memory");
        return nullptr;
    }
    if (!opendepot::LoadCodeplug(path, handle->cp)) {
        SetErr(err, handle->cp.error.empty() ? "cannot load codeplug" : handle->cp.error);
        delete handle;
        return nullptr;
    }
    return handle;
}

void od_codeplug_close(od_codeplug* cp) { delete cp; }

int od_codeplug_save(od_codeplug* cp, const char* path, char** err) {
    if (err) *err = nullptr;
    if (!cp || !path) {
        SetErr(err, "invalid argument");
        return 0;
    }
    std::string e;
    if (!opendepot::SaveCodeplug(path, cp->cp, e)) {
        SetErr(err, e);
        return 0;
    }
    return 1;
}

char* od_codeplug_report_json(od_codeplug* cp) {
    if (!cp) return nullptr;
    return Dup(opendepot::ServiceReportJson(cp->cp, ""));
}

char* od_codeplug_model_json(od_codeplug* cp) {
    if (!cp) return nullptr;
    std::string serial, model;
    opendepot::ReadIdentityFromCodeplug(cp->cp, serial, model);
    std::string out = "{";
    out += "\"model\":" + JsonEscape(model);
    out += ",\"serial\":" + JsonEscape(serial);
    out += ",\"flashcode\":" + JsonEscape(opendepot::GetFlashcode(cp->cp));
    out += "}";
    return Dup(out);
}

char* od_codeplug_flashcode(od_codeplug* cp) {
    if (!cp) return nullptr;
    return Dup(opendepot::GetFlashcode(cp->cp));
}

int od_codeplug_validate_radio(od_codeplug* cp, char** message) {
    if (message) *message = nullptr;
    if (!cp) {
        SetErr(message, "invalid codeplug");
        return 0;
    }
    std::string err;
    if (!opendepot::ValidateRadioCodeplug(cp->cp, err)) {
        SetErr(message, err);
        return 0;
    }
    return 1;
}

char* od_list_depot_profiles_json(const char* database_path, char** err) {
    if (err) *err = nullptr;
    if (!database_path) {
        SetErr(err, "no database path");
        return nullptr;
    }
    std::vector<opendepot::DepotProfileInfo> profiles;
    std::string e;
    if (!opendepot::ListDepotProfiles(database_path, profiles, e)) {
        SetErr(err, e);
        return nullptr;
    }
    std::string out = "[";
    for (size_t i = 0; i < profiles.size(); ++i) {
        if (i) out += ",";
        out += "{\"model\":" + JsonEscape(profiles[i].model) +
               ",\"package\":" + JsonEscape(profiles[i].package) + "}";
    }
    out += "]";
    return Dup(out);
}

char* od_list_models_json(void) {
    const auto& models = opendepot::ModelCatalog();
    std::string out = "[";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i) out += ",";
        out += "{\"band\":" + JsonEscape(models[i].band ? models[i].band : "") +
               ",\"radio\":" + JsonEscape(models[i].radio ? models[i].radio : "") +
               ",\"type\":" + JsonEscape(models[i].type ? models[i].type : "") +
               ",\"model\":" + JsonEscape(models[i].model ? models[i].model : "") + "}";
    }
    out += "]";
    return Dup(out);
}

char* od_list_packages_json(const char* model) {
    if (!model) return nullptr;
    return StringArrayJson(opendepot::PackagesForModel(model));
}

char* od_list_addons_json(const char* model) {
    if (!model) return nullptr;
    return StringArrayJson(opendepot::AddonsForModel(model));
}

int od_create_from_profile(const char* database_path, const char* model, const char* package,
                           const char* serial, const char* flashcode, const char* out_path,
                           char** err) {
    if (err) *err = nullptr;
    if (!database_path || !model || !serial || !out_path) {
        SetErr(err, "invalid argument");
        return 0;
    }
    std::string chosen = package ? package : "";
    std::string e;
    if (chosen.empty()) {
        std::vector<opendepot::DepotProfileInfo> profiles;
        if (!opendepot::ListDepotProfiles(database_path, profiles, e)) {
            SetErr(err, e);
            return 0;
        }
        for (const auto& p : profiles) {
            if (p.model == model) {
                chosen = p.package;
                break;
            }
        }
        if (chosen.empty()) {
            SetErr(err, "no native profile for " + std::string(model));
            return 0;
        }
    }
    opendepot::Codeplug tpl;
    if (!opendepot::LoadDepotProfile(database_path, model, chosen, tpl, e)) {
        SetErr(err, e);
        return 0;
    }
    opendepot::CreateOptions opt;
    opt.model = model;
    opt.serial = serial;
    opt.flashcode = flashcode ? flashcode : "";
    opendepot::Codeplug created;
    if (!opendepot::CreateCodeplug(tpl, opt, created, e)) {
        SetErr(err, e);
        return 0;
    }
    if (!opendepot::SaveCodeplug(out_path, created, e)) {
        SetErr(err, e);
        return 0;
    }
    return 1;
}

}  // extern "C"
