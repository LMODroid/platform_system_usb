/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions an
 * limitations under the License.
 */

#include <fuzzer/FuzzedDataProvider.h>
#include <fuzzbinder/libbinder_driver.h>
#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <memory>

#include "UsbAoaService.h"
#include "VendorControlRequestMonitor.h"

using android::sp;
using android::UsbAoaService;
using android::VendorControlRequestMonitor;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto monitor = std::make_unique<VendorControlRequestMonitor>();

    sp<UsbAoaService> service = new UsbAoaService(std::move(monitor));

    android::fuzzService(service, FuzzedDataProvider(data, size));

    return 0;
}
