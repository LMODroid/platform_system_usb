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

/*
 * VendorControlRequestMonitor.h
 * Monitors the FunctionFS control endpoint for AOA requests.
 */

#pragma once

#include <android/hardware/usb/aoa/AccessoryHandshakeState.h>
#include <android-base/unique_fd.h>
#include <android-base/logging.h>
#include <linux/uhid.h>
#include <linux/usb/functionfs.h>
#include <mutex>
#include <string>
#include <sys/epoll.h>
#include <thread>
#include <vector>
#include <map>

#ifndef HID_ANY_ID
#define HID_ANY_ID (~0)
#endif


using android::hardware::usb::aoa::AccessoryHandshakeState;

namespace android {
    class UsbAoaService;

    class VendorControlRequestMonitor {
    public:
        explicit VendorControlRequestMonitor();
        ~VendorControlRequestMonitor();

        void start(android::base::unique_fd monitorFd);
        void setService(UsbAoaService* service);

        std::vector<std::string> getAccessoryStrings();
        int getMaxPacketSize();
        void setMaxPacketSize(int maxPacketSize);
        bool isStartRequested();

    private:
        // Constants for accessory mode.
        static constexpr int ACCESSORY_NUM_STRINGS = 6;

        android::base::unique_fd mMonitorFd;
        std::thread mThread;

        UsbAoaService* mService;

        std::mutex mAccessoryFieldsMutex;
        struct accessory_fields {
            AccessoryHandshakeState controlState;
            std::string strings[ACCESSORY_NUM_STRINGS];
            int maxPacketSize;
            bool startRequested;
        } mAccessoryFields;

        DISALLOW_COPY_AND_ASSIGN(VendorControlRequestMonitor);
    };

} // namespace android
