/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "aoad"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include "VendorControlRequestMonitor.h"
#include "UsbAoaService.h"

using android::sp;
using android::ProcessState;
using android::IPCThreadState;
using android::defaultServiceManager;
using android::String16;
using android::VendorControlRequestMonitor;
using android::UsbAoaService;

int main(int /*argc*/, char** argv) {
    android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));

    LOG(INFO) << "aoa daemon starting up...";

    sp<ProcessState> ps(ProcessState::self());
    ps->startThreadPool();

    auto monitor = std::make_unique<VendorControlRequestMonitor>();
    sp<UsbAoaService> service = new UsbAoaService(std::move(monitor));

    android::status_t status = defaultServiceManager()->addService(
            String16("aoad"), service);

    if (status != android::OK) {
        LOG(INFO) << "Failed to register aoad service: " << status;
        return -1;
    }

    LOG(INFO) << "aoad service registered. Waiting for commands...";

    IPCThreadState::self()->joinThreadPool();

    LOG(FATAL) << "aoad exiting unexpectedly";

    return 0;
}
