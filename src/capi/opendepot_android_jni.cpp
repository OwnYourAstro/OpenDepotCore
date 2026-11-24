// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <jni.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opendepot/core/adk.h"
#include "opendepot/core/depot_profiles.h"
#include "opendepot/core/radio.h"
#include "opendepot/core/serial_link.h"
#include "radio/android_serial_link.h"

namespace {

thread_local std::string g_last_error;

std::string FromJava(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

jstring ToJava(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
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
            default: o += c;
        }
    }
    o += "\"";
    return o;
}

opendepot::RadioSession* Session(jlong handle) {
    return reinterpret_cast<opendepot::RadioSession*>(handle);
}

bool LoadTemplate(const std::string& db, const std::string& model, opendepot::Codeplug& tpl,
                  std::string& err) {
    std::string berr;
    if (opendepot::BuiltinTemplate(model, tpl, berr) && !tpl.records.empty()) return true;
    if (!db.empty()) {
        std::vector<opendepot::DepotProfileInfo> profiles;
        std::string derr;
        if (opendepot::ListDepotProfiles(db, profiles, derr)) {
            for (const auto& p : profiles) {
                if (p.model != model) continue;
                if (opendepot::LoadDepotProfile(db, model, p.package, tpl, err)) return true;
            }
        }
    }
    err = berr.empty() ? "no template or native profile for " + model : berr;
    return false;
}

jlong NativeOpen(JNIEnv* env, jobject thiz) {
    g_last_error.clear();
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || !vm) {
        g_last_error = "cannot get JavaVM";
        return 0;
    }
    auto link = opendepot::CreateAndroidSerialLink(vm, thiz);
    if (!link) {
        g_last_error = "cannot create USB serial link";
        return 0;
    }
    std::string err;
    auto session = opendepot::RadioSession::OpenWithLink(std::move(link), err);
    if (!session) {
        g_last_error = err.empty() ? "radio open failed" : err;
        return 0;
    }
    return reinterpret_cast<jlong>(session.release());
}

jstring NativeInfo(JNIEnv* env, jobject, jlong handle) {
    g_last_error.clear();
    auto* session = Session(handle);
    if (!session) {
        g_last_error = "invalid session";
        return nullptr;
    }
    opendepot::RadioInfo info;
    std::string err;
    if (!session->Info(info, err)) {
        g_last_error = err;
        return nullptr;
    }
    std::string out = "{";
    out += "\"banner\":" + JsonEscape(info.banner);
    out += ",\"host\":" + JsonEscape(info.host);
    out += ",\"serial\":" + JsonEscape(info.serial);
    out += ",\"model\":" + JsonEscape(info.model);
    out += "}";
    return ToJava(env, out);
}

jstring NativeReadAll(JNIEnv* env, jobject, jlong handle, jstring db_path, jstring out_path) {
    g_last_error.clear();
    auto* session = Session(handle);
    if (!session) {
        g_last_error = "invalid session";
        return nullptr;
    }
    const std::string db = FromJava(env, db_path);
    const std::string out_file = FromJava(env, out_path);
    if (out_file.empty()) {
        g_last_error = "no output path";
        return nullptr;
    }

    opendepot::RadioInfo info;
    std::string err;
    session->Info(info, err);
    std::string model = info.model;
    if (model.empty()) model = opendepot::BuiltinFirstModel();

    opendepot::Codeplug tpl;
    if (!LoadTemplate(db, model, tpl, err)) {
        g_last_error = err;
        return nullptr;
    }

    std::vector<opendepot::ReadRequest> validated;
    opendepot::ModelReadRequests(model, validated);
    std::vector<opendepot::ReadRequest> reqs = validated;
    if (reqs.empty() && !db.empty())
        opendepot::BuildDepotReadRequests(db, model, reqs, err);
    if (reqs.empty()) opendepot::BuiltinReadRequests(reqs);
    if (validated.empty()) opendepot::AppendTypeScan(reqs);

    std::map<std::pair<uint16_t, uint16_t>, opendepot::RadioReadRequest> type_map;
    for (const auto& r : reqs) {
        const auto key = std::make_pair(r.part, r.tag);
        auto it = type_map.find(key);
        if (it == type_map.end()) {
            type_map.emplace(key, opendepot::RadioReadRequest{r.part, r.tag, r.inst, r.len});
        } else {
            if (r.inst < it->second.inst) it->second.inst = r.inst;
            if (r.len > it->second.len) it->second.len = r.len;
        }
    }
    std::vector<opendepot::RadioReadRequest> types;
    types.reserve(type_map.size());
    for (const auto& kv : type_map) types.push_back(kv.second);

    std::vector<opendepot::Record> recs;
    if (!session->ReadAllTypeInstances(types, recs, err)) {
        g_last_error = err;
        return nullptr;
    }

    opendepot::Codeplug cp = tpl;
    cp.records = std::move(recs);
    std::string serial, read_model;
    if (opendepot::ReadIdentityFromCodeplug(cp, serial, read_model))
        opendepot::UpdateIdentity(cp, serial, read_model);
    if (!opendepot::SaveCodeplug(out_file, cp, err)) {
        g_last_error = err;
        return nullptr;
    }
    std::string verr;
    const bool valid = opendepot::ValidateRadioCodeplug(cp, verr);
    std::string out = "{";
    out += "\"model\":" + JsonEscape(model);
    out += ",\"serial\":" + JsonEscape(serial);
    out += ",\"records\":" + std::to_string(cp.records.size());
    out += ",\"valid\":" + std::string(valid ? "true" : "false");
    out += ",\"message\":" + JsonEscape(verr);
    out += ",\"path\":" + JsonEscape(out_file);
    out += "}";
    return ToJava(env, out);
}

void NativeClose(JNIEnv*, jobject, jlong handle) { delete Session(handle); }

jstring NativeLastError(JNIEnv* env, jobject) { return ToJava(env, g_last_error); }

const JNINativeMethod kMethods[] = {
    {const_cast<char*>("nativeOpen"), const_cast<char*>("()J"), reinterpret_cast<void*>(NativeOpen)},
    {const_cast<char*>("nativeInfo"), const_cast<char*>("(J)Ljava/lang/String;"),
     reinterpret_cast<void*>(NativeInfo)},
    {const_cast<char*>("nativeReadAll"),
     const_cast<char*>("(JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
     reinterpret_cast<void*>(NativeReadAll)},
    {const_cast<char*>("nativeClose"), const_cast<char*>("(J)V"),
     reinterpret_cast<void*>(NativeClose)},
    {const_cast<char*>("nativeLastError"), const_cast<char*>("()Ljava/lang/String;"),
     reinterpret_cast<void*>(NativeLastError)},
};

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
        return JNI_ERR;
    jclass cls = env->FindClass("org/ownyourastro/opendepot_android/UsbSerialBridge");
    if (!cls) return JNI_ERR;
    if (env->RegisterNatives(cls, kMethods, sizeof(kMethods) / sizeof(kMethods[0])) != JNI_OK)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
