// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "radio/android_serial_link.h"

#include "opendepot/core/serial_link.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace opendepot {
namespace {

class AndroidSerialLink final : public SerialLink {
public:
    AndroidSerialLink(JavaVM* vm, jobject bridge) : vm_(vm) {
        JNIEnv* env = Env();
        if (!env || !bridge) return;
        jclass cls = env->GetObjectClass(bridge);
        open_ = env->GetMethodID(cls, "open", "(I)Z");
        reconfigure_ = env->GetMethodID(cls, "reconfigure", "(I)Z");
        purge_ = env->GetMethodID(cls, "purge", "()V");
        read_ = env->GetMethodID(cls, "read", "([BI)I");
        write_ = env->GetMethodID(cls, "write", "([B)Z");
        close_ = env->GetMethodID(cls, "close", "()V");
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        bridge_ = env->NewGlobalRef(bridge);
        valid_ = bridge_ && open_ && reconfigure_ && purge_ && read_ && write_ && close_;
    }

    ~AndroidSerialLink() override {
        Close();
        JNIEnv* env = Env();
        if (env && bridge_) env->DeleteGlobalRef(bridge_);
    }

    bool Open(uint32_t baud, std::string& err) override {
        if (!valid_) {
            err = "USB serial bridge unavailable";
            return false;
        }
        JNIEnv* env = Env();
        if (!env) {
            err = "cannot attach JNI thread";
            return false;
        }
        const jboolean ok = env->CallBooleanMethod(bridge_, open_, jint(baud));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            err = "USB serial open failed";
            return false;
        }
        opened_ = ok == JNI_TRUE;
        if (!opened_) err = "USB serial open returned false";
        return opened_;
    }

    bool Reconfigure(uint32_t baud, std::string& err) override {
        if (!opened_) return Open(baud, err);
        JNIEnv* env = Env();
        if (!env) {
            err = "cannot attach JNI thread";
            return false;
        }
        const jboolean ok = env->CallBooleanMethod(bridge_, reconfigure_, jint(baud));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            err = "USB serial reconfigure failed";
            return false;
        }
        if (!ok) {
            err = "USB serial reconfigure returned false";
            opened_ = false;
        }
        return ok == JNI_TRUE;
    }

    void Purge() override {
        if (!valid_ || !opened_) return;
        JNIEnv* env = Env();
        if (!env) return;
        env->CallVoidMethod(bridge_, purge_);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    size_t Read(uint8_t* data, size_t n, int timeout_ms) override {
        if (!valid_ || !opened_ || !n) return 0;
        JNIEnv* env = Env();
        if (!env) return 0;
        jbyteArray arr = env->NewByteArray(jsize(n));
        if (!arr) return 0;
        const jint got = env->CallIntMethod(bridge_, read_, arr, jint(timeout_ms));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(arr);
            return 0;
        }
        size_t count = got > 0 ? size_t(got) : 0;
        if (count > n) count = n;
        if (count) env->GetByteArrayRegion(arr, 0, jsize(count), reinterpret_cast<jbyte*>(data));
        env->DeleteLocalRef(arr);
        return count;
    }

    bool Write(const uint8_t* data, size_t n) override {
        if (!valid_ || !opened_ || !n) return valid_ && opened_;
        JNIEnv* env = Env();
        if (!env) return false;
        jbyteArray arr = env->NewByteArray(jsize(n));
        if (!arr) return false;
        env->SetByteArrayRegion(arr, 0, jsize(n), reinterpret_cast<const jbyte*>(data));
        const jboolean ok = env->CallBooleanMethod(bridge_, write_, arr);
        env->DeleteLocalRef(arr);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return ok == JNI_TRUE;
    }

    void Close() override {
        if (!valid_ || !opened_) {
            opened_ = false;
            return;
        }
        JNIEnv* env = Env();
        if (env) {
            env->CallVoidMethod(bridge_, close_);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        opened_ = false;
    }

private:
    JNIEnv* Env() {
        if (!vm_) return nullptr;
        JNIEnv* env = nullptr;
        const jint r = vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (r == JNI_EDETACHED) {
            if (vm_->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) return nullptr;
        } else if (r != JNI_OK) {
            return nullptr;
        }
        return env;
    }

    JavaVM* vm_ = nullptr;
    jobject bridge_ = nullptr;
    jmethodID open_ = nullptr;
    jmethodID reconfigure_ = nullptr;
    jmethodID purge_ = nullptr;
    jmethodID read_ = nullptr;
    jmethodID write_ = nullptr;
    jmethodID close_ = nullptr;
    bool valid_ = false;
    bool opened_ = false;
};

}  // namespace

std::unique_ptr<SerialLink> CreateAndroidSerialLink(JavaVM* vm, jobject bridge) {
    return std::unique_ptr<SerialLink>(new AndroidSerialLink(vm, bridge));
}

}  // namespace opendepot
