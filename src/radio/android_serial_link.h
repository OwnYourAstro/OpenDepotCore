// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <jni.h>

#include <memory>

namespace opendepot {

class SerialLink;

// Wraps the Kotlin UsbSerialBridge object. The bridge must expose:
//   boolean open(int baud)
//   boolean reconfigure(int baud)
//   void purge()
//   int read(byte[] buffer, int timeoutMs)   // returns bytes, <= 0 on timeout
//   boolean write(byte[] buffer)
//   void close()
std::unique_ptr<SerialLink> CreateAndroidSerialLink(JavaVM* vm, jobject bridge);

}  // namespace opendepot
