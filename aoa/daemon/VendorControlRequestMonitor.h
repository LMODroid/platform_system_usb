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
        bool isRunning() const {
            return mThread.joinable();
        }

    private:
        static constexpr int ACCESSORY_GET_PROTOCOL = 51;
        static constexpr int ACCESSORY_SEND_STRING = 52;
        static constexpr int ACCESSORY_START = 53;
        static constexpr int ACCESSORY_REGISTER_HID = 54;
        static constexpr int ACCESSORY_UNREGISTER_HID = 55;
        static constexpr int ACCESSORY_SET_HID_REPORT_DESC = 56;
        static constexpr int ACCESSORY_SEND_HID_EVENT = 57;
        static constexpr int ACCESSORY_SET_AUDIO_MODE = 58;
        static constexpr int EPOLL_MAX_EVENTS = 4;
        static constexpr int FFS_NUM_EVENTS = 5;

        // Constants for accessory mode.
        static constexpr int ACCESSORY_VERSION = 2;
        static constexpr int ACCESSORY_STRING_LENGTH = 256;
        static constexpr int ACCESSORY_NUM_STRINGS = 6;
        static constexpr char UHID_PATH[] = "/dev/uhid";

        android::base::unique_fd mMonitorFd;
        int mShutdownPipefd[2];
        std::thread mThread;

        UsbAoaService* mService;

        std::mutex mAccessoryFieldsMutex;
        struct accessory_fields {
            AccessoryHandshakeState controlState;
            std::string strings[ACCESSORY_NUM_STRINGS];
            int maxPacketSize;
            bool startRequested;
        } mAccessoryFields;

        // Variables for HID
        struct hid_descriptor {
            std::vector<char> descBuf;
            uint16_t descLength;
        };
        std::map<uint16_t, android::base::unique_fd> mHidDeviceFds;
        std::map<uint16_t, hid_descriptor> mPendingDescriptors;
        std::vector<uint16_t> mHidList;

        android::base::unique_fd getUhidFd();
        bool writeUhidEvent(int fd, struct uhid_event &ev);
        void unregisterHid(int hidId);
        bool isHidIdRegistered(uint16_t hidId);
        bool validateDescriptorParams(uint16_t hidId, uint16_t index, uint16_t length);
        bool registerUhidDevice(uint16_t hidId, const hid_descriptor &completeDescriptor);

        bool readBuffer(int fd, std::vector<char> &buf, uint16_t length);

        // Protocol Handlers
        bool handleAccessoryGetProtocol(uint16_t value, uint16_t index, uint16_t length,
                                        std::vector<char> &buf);
        bool handleAccessorySendString(int fd, uint16_t index, uint16_t length,
                                       std::vector<char> &buf);
        bool handleAccessoryStart(int fd, uint16_t value, uint16_t index, uint16_t length,
                                  std::vector<char> &buf);

        // HID Protocol Handlers
        bool handleRegisterHid(uint16_t hidId, uint16_t index);
        bool handleUnregisterHid(uint16_t hidId);
        bool handleSetReportHidDescriptor(int fd, uint16_t hidId, uint16_t index, uint16_t length,
                                          std::vector<char> &buf);
        bool handleSendHidEvent(int fd, uint16_t hidId, uint16_t length, std::vector<char> &buf);

        void handleControlRequest(int fd, const struct usb_ctrlrequest *setup);
        void updateAccessoryState(AccessoryHandshakeState);
        void teardown();
        int setupEpoll(android::base::unique_fd &epollFd);
        void monitorLoop();
        void stop();

        DISALLOW_COPY_AND_ASSIGN(VendorControlRequestMonitor);
    };

} // namespace android
