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

#define LOG_TAG "usbaoad"

#include "UsbAoaService.h"
#include <android-base/logging.h>
#include <linux/magic.h>
#include "AccessoryLegacyBridgeThread.h"
#include <sys/socket.h>
#include <linux/usb/functionfs.h>
#include <sys/ioctl.h>
#include <statslog.h>
#include <selinux/selinux.h>
#include <selinux/android.h>
#include <fcntl.h>
#include <optional>

namespace android {

    static int get_max_packet_size(int ffs_fd) {
        struct usb_endpoint_descriptor desc{};
        if (ioctl(ffs_fd, FUNCTIONFS_ENDPOINT_DESC, reinterpret_cast<unsigned long>(&desc))) {
            LOG(ERROR) << "Could not get FFS bulk-in descriptor";
            return 512;
        } else {
            LOG(INFO) <<"Got FFS max packet size: %d" << desc.wMaxPacketSize;
            return desc.wMaxPacketSize;
        }
    }

    UsbAoaService::UsbAoaService(std::unique_ptr<VendorControlRequestMonitor> monitor)
            : mMonitor(std::move(monitor)) {
        if (mMonitor) {
            mMonitor->setService(this);
        }
        initialize();
    }

    void UsbAoaService::initialize() {
        LOG(INFO) << "UsbAoaService initializing...";

        mStatus.isFfsDirectoryPresent = false;
        mStatus.openControlResult = static_cast<int>(
            android::util::USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__UNSPECIFIED);

        if (checkAccessoryFfsDirectories()) {
            mStatus.isFfsDirectoryPresent = true;
        } else {
            LOG(ERROR) << "init aborted: FFS directories missing.";
            return;
        }

        int result = openAccessoryControl();
        mStatus.openControlResult = result;

        // 4. Start Monitor (only if Open succeeded)
        if (result == static_cast<int>(
                android::util::USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__SUCCESS)) {
            LOG(INFO) << "Initialization success. Starting monitor thread.";
            startVendorControlRequestMonitor();
        } else {
            LOG(ERROR) << "init aborted:: Open control returned " << result;
        }
    }

    binder::Status UsbAoaService::getInitializationStatus(
            android::hardware::usb::aoa::AoaInitializationStatus* _aidl_return) {
        *_aidl_return = mStatus;
        return binder::Status::ok();
    }

    static std::optional<android::String16> toOptionalString16(
            const std::vector<std::string>& list, size_t index) {
        if (index < list.size() && !list[index].empty()) {
            return std::make_optional<android::String16>(list[index].c_str());
        }
        return std::nullopt;
    }

    binder::Status UsbAoaService::getAccessoryStrings(
            android::hardware::usb::aoa::AccessoryMetadata* _aidl_return) {
        if (mMonitor) {
            std::vector<std::string> rawStrings = mMonitor->getAccessoryStrings();

            _aidl_return->manufacturer = toOptionalString16(rawStrings, 0);
            _aidl_return->model        = toOptionalString16(rawStrings, 1);
            _aidl_return->description  = toOptionalString16(rawStrings, 2);
            _aidl_return->version      = toOptionalString16(rawStrings, 3);
            _aidl_return->uri          = toOptionalString16(rawStrings, 4);
            _aidl_return->serial       = toOptionalString16(rawStrings, 5);

        } else {
            LOG(WARNING) << "Cannot get accessory strings: mMonitor is null";
        }

        return binder::Status::ok();
    }

    binder::Status UsbAoaService::isStartRequested(bool* _aidl_return) {
        if (mMonitor) {
            *_aidl_return = mMonitor->isStartRequested();
        } else {
            *_aidl_return = false;
        }
        return binder::Status::ok();
    }

    binder::Status UsbAoaService::setCallback(const sp<IUsbAoaCallback>& callback) {
        std::lock_guard<std::mutex> lock(mLock);
        mCallback = callback;
        LOG(INFO) << "SystemServer registered AOA callback.";
        return binder::Status::ok();
    }

    int UsbAoaService::openAccessoryControl() {
        LOG(INFO) << "Writing descriptors to USB Accessory...";

        int fd = TEMP_FAILURE_RETRY(open(FFS_ACCESSORY_EP0, O_RDWR));
        if (fd < 0) {
            PLOG(ERROR) << "Opening accessory ep0 failed: " << fd;
            return static_cast<int>(
                    android::util::
                    USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__OPEN_ACCESSORY_FAILED);
        }
        ssize_t ret = TEMP_FAILURE_RETRY(write(fd, &acc_desc, sizeof(acc_desc)));
        if (ret < 0) {
            PLOG(ERROR) << "Writing accessory desc failed: " << fd;
            close(fd);
            return static_cast<int>(
                    android::util::
                    USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__WRITE_DESC_FAILED);
        }
        ret = TEMP_FAILURE_RETRY(write(fd, &acc_strings, sizeof(acc_strings)));
        if (ret < 0) {
            PLOG(ERROR) << "Writing accessory strings failed: " << fd;
            close(fd);
            return static_cast<int>(
                    android::util::
                    USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__WRITE_STRING_FAILED);
        }

        // To enable testing of the userspace AOA prior to the necessary kernel change
        // landing on test devices, we allow this this check to continue if selinux is
        // in permissive mode. This patch will be reverted once the kernel patch lands
        // in the GKI kernels and populates to the necessary device kernel prebuilts.
        int sel_status = security_getenforce();
        // security_getenforce() returns 0 if permissive, 1 if enforcing, or a negative
        // value if an error has occurred. In the error case, assume selinux is
        // enforcing.
        bool selinux_en = (sel_status != 0);
        int err = selinux_android_restorecon(FFS_ACCESSORY_EP1, 0);
        if (err < 0) {
            LOG(ERROR) << "Applying SELINUX labels to accessory EP1 failed: " << err;
            if (selinux_en) {
                LOG(ERROR) << "Selinux is enforcing, failed to apply label to accessory ep1, "
                << "closing";
                close(fd);
                return static_cast<int>(
                        android::util::
                        USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__SELINUX_PERMISSION_FAILED);
            } else {
                LOG(WARNING) <<"SELinux is permissive, continuing for testing purposes";
            }
        }
        err = selinux_android_restorecon(FFS_ACCESSORY_EP2, 0);
        if (err < 0) {
            LOG(ERROR) << "Applying SELINUX labels to accessory EP2 failed: " << err;
            if (selinux_en) {
                LOG(ERROR) <<"Selinux is enforcing, failed to apply label to accessory ep2, "
                << "closing";
                close(fd);
                return static_cast<int>(
                        android::util::
                        USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__SELINUX_PERMISSION_FAILED);
            } else {
                LOG(WARNING) << "SELinux is permissive, continuing for testing purposes";
            }
        }

        LOG(INFO) << "USB Accessory Descriptors written successfully";
        return static_cast<int>(
                android::util::USB_USERSPACE_AOA_ENABLED__OPEN_CONTROL_RESULT__SUCCESS);
    }

    // Function to check if a given path is a functionfs mount point
    bool is_path_mounted_as_functionfs(const std::string& path) {
        struct statfs buf;
        if (statfs(path.c_str(), &buf) != 0) {
            PLOG(ERROR) << "statfs failed for " << path;
            return false;
        }
        if (buf.f_type != FUNCTIONFS_MAGIC) {
            LOG(WARNING) << path << " is not FunctionFS (magic: " << std::hex << buf.f_type << ")";
            return false;
        }
        return true;
    }

    std::string get_parent_directory(const std::string& path) {
        std::string parent_dir = path;
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            parent_dir = path.substr(0, pos);
        }
        return parent_dir;
    }

    bool UsbAoaService::checkAccessoryFfsDirectories() {
        if (access(FFS_VENDOR_CTRL_REQUEST_EP0, F_OK) != 0) {
            LOG(ERROR) << "Cannot access " << FFS_VENDOR_CTRL_REQUEST_EP0;
            return false;
        }

        if (!is_path_mounted_as_functionfs(get_parent_directory(FFS_VENDOR_CTRL_REQUEST_EP0))) {
            return false;
        }

        if (access(FFS_ACCESSORY_EP0, F_OK) != 0) {
            LOG(ERROR) << "Cannot access " << FFS_ACCESSORY_EP0;
            return false;
        }

        if (!is_path_mounted_as_functionfs(get_parent_directory(FFS_ACCESSORY_EP0))) {
            return false;
        }

        LOG(INFO) << "USB Accessory FFS directory check success";
        return true;
    }

    bool UsbAoaService::startVendorControlRequestMonitor() {
        android::base::unique_fd ufd(open(FFS_VENDOR_CTRL_REQUEST_EP0, O_RDWR));

        int fd = ufd.get();

        if (fd < 0) {
            PLOG(ERROR) << "Cannot open " << FFS_VENDOR_CTRL_REQUEST_EP0;
            return false;
        }

        ssize_t ret = TEMP_FAILURE_RETRY(write(fd, &ctrl_desc, sizeof(ctrl_desc)));
        if (ret != sizeof(ctrl_desc)) {
            PLOG(ERROR) << "Writing ctrl desc failed: " << ret;
            ufd.reset(-1);
            return false;
        }

        ret = TEMP_FAILURE_RETRY(write(fd, &ctrl_strings, sizeof(ctrl_strings)));
        if (ret != sizeof(ctrl_strings)) {
            PLOG(ERROR) << "Writing ctrl strings failed: " << ret;
            ufd.reset(-1);
            return false;
        }
        if (mMonitor) {
            LOG(INFO) << "Starting Vendor Control Monitor thread...";
            mMonitor->start(std::move(ufd));
            return true;
        } else {
            LOG(ERROR) << "Cannot start monitor: mMonitor is null";
            return false;
        }
    }

    void UsbAoaService::notifyStateChange(AccessoryHandshakeState state) {
        if (state == AccessoryHandshakeState::UNKNOWN) {
            std::lock_guard<std::mutex> bridgeLock(mAccessoryLegacyBridgeThreadMutex);
            if (mAccessoryLegacyBridgeThread) {
                LOG(INFO) << "Resetting legacy bridge thread";
                mAccessoryLegacyBridgeThread.reset();
            }
        }

        std::lock_guard<std::mutex> lock(mLock);
        if (mCallback != nullptr) {
            // Fires onAccessoryStateChanged() in Java
            auto ret = mCallback->onAccessoryStateChanged(state);
            if (!ret.isOk()) {
                LOG(ERROR) << "Failed to notify SystemServer of state change: "
                << static_cast<int>(state);
                // If binder dies, we simply wait for re-registration
                mCallback = nullptr;
            }
        } else {
            LOG(WARNING) << "Dropped AOA state notification (no callback registered): "
            << static_cast<int>(state);
        }
    }


    binder::Status UsbAoaService::openAccessory(::android::os::ParcelFileDescriptor* _aidl_return) {
        // 1. Check if the Monitor is running
        if (!mMonitor) {
            LOG(ERROR) << "Vendor control monitor is not running";
            return binder::Status::fromServiceSpecificError(ENODEV);
        }

        LOG(INFO) << "Opening userspace accessory bridge";
        // 2. Create Socket Pair (Bridge <-> App)
        android::base::unique_fd bridgeFd;
        android::base::unique_fd appFd;
        {
            int fds[2];
            if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) == -1) {
                PLOG(ERROR) << "could not create socket pair";
                return binder::Status::fromServiceSpecificError(errno);
            }
            bridgeFd.reset(fds[0]);
            appFd.reset(fds[1]);
        }

        // 3. Open the FunctionFS Endpoints
        android::base::unique_fd readFd(open(FFS_ACCESSORY_EP1, O_RDONLY));
        if (readFd.get() < 0) {
            PLOG(ERROR) << "could not open " << FFS_ACCESSORY_EP1;
            return binder::Status::fromServiceSpecificError(errno);
        }
        android::base::unique_fd writeFd(open(FFS_ACCESSORY_EP2, O_WRONLY));
        if (writeFd.get() < 0) {
            PLOG(ERROR) << "could not open "<< FFS_ACCESSORY_EP2;
            return binder::Status::fromServiceSpecificError(errno);
        }

        // 4. Get and Set Max Packet Size
        int maxPacketSize = get_max_packet_size(writeFd.get());

        mMonitor->setMaxPacketSize(maxPacketSize);

        // 5. Start the Bridge Thread
        {
            std::lock_guard<std::mutex> lock(mAccessoryLegacyBridgeThreadMutex);
            mAccessoryLegacyBridgeThread = std::make_unique<AccessoryLegacyBridgeThread>(
                    std::move(readFd),
                    std::move(writeFd),
                    std::move(bridgeFd),
                    maxPacketSize
            );
        }
        LOG(INFO) << "Successfully created userspace accessory bridge";

        // .release() transfers ownership of the FD to the ParcelFileDescriptor (JAVA)
        _aidl_return->reset(android::base::unique_fd(appFd.release()));

        return binder::Status::ok();
    }

    binder::Status UsbAoaService::openAccessoryForInputStream(
            ::android::os::ParcelFileDescriptor* _aidl_return) {
        // 1. Lock and tear down bridge
        {
            std::lock_guard<std::mutex> lock(mAccessoryLegacyBridgeThreadMutex);
            if (mAccessoryLegacyBridgeThread) {
                LOG(INFO) << "openAccessoryForInputStream: tearing down legacy bridge";
                mAccessoryLegacyBridgeThread.reset();
            }
        }

        // 2. Open the Input Endpoint (EP1)
        int readFd = open(FFS_ACCESSORY_EP1, O_RDONLY);
        if (readFd < 0) {
            PLOG(ERROR) << "could not open " << FFS_ACCESSORY_EP1;
            return binder::Status::fromServiceSpecificError(errno);
        }

        // 3. Return the readFd to Java
        _aidl_return->reset(android::base::unique_fd(readFd));
        return binder::Status::ok();
    }

    binder::Status UsbAoaService::openAccessoryForOutputStream(
            ::android::os::ParcelFileDescriptor* _aidl_return) {
        // 1. Lock and tear down bridge
        {
            std::lock_guard<std::mutex> lock(mAccessoryLegacyBridgeThreadMutex);
            if (mAccessoryLegacyBridgeThread) {
                LOG(INFO) << "openAccessoryForOutputStream: tearing down legacy bridge";
                mAccessoryLegacyBridgeThread.reset();
            }
        }

        // 2. Open the Output Endpoint (EP2)
        int writeFd = open(FFS_ACCESSORY_EP2, O_WRONLY);
        if (writeFd < 0) {
            PLOG(ERROR) << "could not open " << FFS_ACCESSORY_EP2;
            return binder::Status::fromServiceSpecificError(errno);
        }

        // 3. Get Max Packet Size from the kernel
        int maxPacketSize = get_max_packet_size(writeFd);

        // 4. Update the Monitor (if running)
        if (mMonitor) {
            mMonitor->setMaxPacketSize(maxPacketSize);
        } else {
            // Optional: Warn if monitor isn't running, but don't fail the stream open
            LOG(WARNING) << "Vendor monitor not running during openAccessoryForOutputStream";
        }

        // 5. Return the writeFd to Java
        _aidl_return->reset(android::base::unique_fd(writeFd));
        return binder::Status::ok();
    }

    binder::Status UsbAoaService::getMaxPacketSize(int* _aidl_return) {
        if (!mMonitor) {
            *_aidl_return = -1;
            return binder::Status::fromServiceSpecificError(1, "Monitor not initialized");
        }

        *_aidl_return = mMonitor->getMaxPacketSize();
        return binder::Status::ok();
    }

} // namespace android
