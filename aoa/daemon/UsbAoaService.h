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
 * UsbAoaService.h
 * Binder interface implementation for the aoa daemon.
 */

#pragma once

#include <android/hardware/usb/aoa/AccessoryMetadata.h>
#include <android/hardware/usb/aoa/AoaInitializationStatus.h>
#include <android/hardware/usb/aoa/BnUsbAoa.h>
#include <android/hardware/usb/aoa/IUsbAoaCallback.h>
#include <binder/BinderService.h>
#include <binder/Status.h>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <utils/String16.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#ifndef FUNCTIONFS_MAGIC
#define FUNCTIONFS_MAGIC 0xa647361
#endif

#include "VendorControlRequestMonitor.h"
#include "AoaDescriptors.h"
#include "AccessoryLegacyBridgeThread.h"

using namespace android::hardware::usb::aoa;

namespace android {

    class UsbAoaService : public BinderService<UsbAoaService>, public BnUsbAoa {
    public:
        static char const* getServiceName() { return "aoad"; }
        AoaInitializationStatus mStatus;

        explicit UsbAoaService(std::unique_ptr<VendorControlRequestMonitor> monitor);

        // --- IUsbAoa AIDL Implementation ---

        binder::Status getAccessoryStrings(
                android::hardware::usb::aoa::AccessoryMetadata* _aidl_return) override;

        binder::Status isStartRequested(bool* _aidl_return) override;
        binder::Status getInitializationStatus(AoaInitializationStatus* _aidl_return) override;

        binder::Status openAccessory(::android::os::ParcelFileDescriptor* _aidl_return) override;
        binder::Status openAccessoryForInputStream(
                ::android::os::ParcelFileDescriptor* _aidl_return) override;
        binder::Status openAccessoryForOutputStream(
                ::android::os::ParcelFileDescriptor* _aidl_return) override;
        binder::Status getMaxPacketSize(int* _aidl_return) override;

        binder::Status setCallback(const sp<IUsbAoaCallback>& callback) override;

        // Called by VendorControlRequestMonitor when it receives ACCESSORY_START, etc.
        void notifyStateChange(android::hardware::usb::aoa::AccessoryHandshakeState state);

    private:
        std::mutex mLock;
        sp<IUsbAoaCallback> mCallback;
        std::unique_ptr<VendorControlRequestMonitor> mMonitor;

        std::mutex mAccessoryLegacyBridgeThreadMutex;

        std::unique_ptr<AccessoryLegacyBridgeThread> mAccessoryLegacyBridgeThread;

        void initialize();
        bool checkAccessoryFfsDirectories();
        int openAccessoryControl();
        bool startVendorControlRequestMonitor();
    };

} // namespace android
