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

#include "VendorControlRequestMonitor.h"
#include "UsbAoaService.h"

namespace android {

    VendorControlRequestMonitor::VendorControlRequestMonitor()
            : mService(nullptr),
              mAccessoryFields({.controlState = AccessoryHandshakeState::UNKNOWN,
                                       .strings = {"", "", "", "", "", ""},
                                       .maxPacketSize = -1,
                                       .startRequested = false}) {

    }

    VendorControlRequestMonitor::~VendorControlRequestMonitor() {

    }

    void VendorControlRequestMonitor::start(android::base::unique_fd monitorFd) {
        mMonitorFd = std::move(monitorFd);
    }

    void VendorControlRequestMonitor::setService(UsbAoaService* service) {
        mService = service;
    }

    std::vector<std::string> VendorControlRequestMonitor::getAccessoryStrings() {
        return {};
    }

    int VendorControlRequestMonitor::getMaxPacketSize() {
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        return mAccessoryFields.maxPacketSize;
    }

    void VendorControlRequestMonitor::setMaxPacketSize(int maxPacketSize) {
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        mAccessoryFields.maxPacketSize = maxPacketSize;
    }

    bool VendorControlRequestMonitor::isStartRequested() {
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        return mAccessoryFields.startRequested;
    }

} // namespace android
