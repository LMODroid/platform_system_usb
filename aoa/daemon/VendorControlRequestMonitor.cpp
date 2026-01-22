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
        if (pipe(mShutdownPipefd) < 0) {
            PLOG(ERROR) << "Failed to create shutdown pipe";
        }
    }

    VendorControlRequestMonitor::~VendorControlRequestMonitor() {
        stop();
        close(mShutdownPipefd[0]);
        close(mShutdownPipefd[1]);
    }

    void VendorControlRequestMonitor::start(android::base::unique_fd monitorFd) {
        mMonitorFd = std::move(monitorFd);
        mThread = std::thread(&VendorControlRequestMonitor::monitorLoop, this);
        LOG(INFO) << "VendorControlRequestMonitor::start monitoring";
    }

    void VendorControlRequestMonitor::setService(UsbAoaService* service) {
        mService = service;
    }

    std::vector<std::string> VendorControlRequestMonitor::getAccessoryStrings() {
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        std::vector<std::string> strings;
        strings.reserve(ACCESSORY_NUM_STRINGS);
        for (int i = 0; i < ACCESSORY_NUM_STRINGS; ++i) {
            strings.push_back(mAccessoryFields.strings[i]);
        }
        return strings;
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

    int VendorControlRequestMonitor::setupEpoll(android::base::unique_fd &epollFd) {
        struct epoll_event ev;

        ev.data.fd = mMonitorFd.get();
        ev.events = EPOLLIN;
        if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, mMonitorFd.get(), &ev) != 0) {
            PLOG(ERROR) << "epoll_ctl failed for ctrl request monitor fd;";
            return errno;
        }

        ev.data.fd = mShutdownPipefd[0];
        ev.events = EPOLLIN;
        if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, mShutdownPipefd[0], &ev) != 0) {
            PLOG(ERROR) << "epoll_ctl failed for ctrl request pipe fd;";
            return errno;
        }

        return 0;
    }

    void VendorControlRequestMonitor::monitorLoop() {
        android::base::unique_fd epollFd(epoll_create(EPOLL_MAX_EVENTS));
        std::vector<struct usb_functionfs_event> ffs_events(FFS_NUM_EVENTS);

        LOG(INFO) << "Monitoring vendor control requests...";

        if (epollFd.get() == -1) {
            PLOG(ERROR) << "Vendor control request monitor epoll_create failed;";
            return;
        }

        if (setupEpoll(epollFd) != 0) {
            LOG(ERROR) << "Vendor control request monitor setupEpoll failed!";
            return;
        }

        struct epoll_event events[EPOLL_MAX_EVENTS];
        int nevents = 0;
        while (true) {
            nevents = epoll_wait(epollFd.get(), events, EPOLL_MAX_EVENTS, -1);

            if (nevents < 0) {
                if (errno != EINTR)
                    PLOG(ERROR) << "Vendor control request monitor epoll_wait failed;";
                continue;
            }

            for (int i = 0; i < nevents; ++i) {
                int fd = events[i].data.fd;
                if (fd == mShutdownPipefd[0]) {
                    LOG(ERROR) << "Vendor control request monitor loop exiting...";
                    return;
                } else if (fd == mMonitorFd.get()) {
                    if (events[i].events & EPOLLIN) {
                        struct usb_functionfs_event *event = ffs_events.data();
                        int nbytes = TEMP_FAILURE_RETRY(
                                ::read(fd, event,
                                       ffs_events.size() * sizeof(usb_functionfs_event)));
                        if (nbytes == -1) {
                            LOG(ERROR) << "error reading Usb control events";
                            continue;
                        }
                        for (size_t n = nbytes / sizeof(*event); n; --n, ++event) {
                            switch (event->type) {
                                case FUNCTIONFS_SETUP:
                                    handleControlRequest(fd, &event->u.setup);
                                    break;
                                case FUNCTIONFS_UNBIND:
                                    teardown();
                                    break;
                                default:
                                    continue;
                            }
                        }
                    }
                }
            }
        }

        return;
    }

    void VendorControlRequestMonitor::stop() {
        if (mThread.joinable()) {
            int c = 'q';
            write(mShutdownPipefd[1], &c, 1);
            mThread.join();
        }
    }

    void VendorControlRequestMonitor::teardown() {
        // Add teardown for vendor control requests being handled.
        LOG(INFO) << "Vendor control request monitor teardown";

        // Teardown for accessory mode.
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        mAccessoryFields.controlState = AccessoryHandshakeState::UNKNOWN;
        for (int i = 0; i < ACCESSORY_NUM_STRINGS; i++) {
            mAccessoryFields.strings[i] = "";
        }
        mAccessoryFields.maxPacketSize = -1;
        // Teardown for HID Devices
        while (!mHidList.empty()) {
            unregisterHid(mHidList.back());
            mHidList.pop_back();
        }
        if (mService) {
            mService->notifyStateChange(AccessoryHandshakeState::UNKNOWN);
        }
    }

    void VendorControlRequestMonitor::handleControlRequest(int fd,
                                                           const struct usb_ctrlrequest *setup) {
        uint8_t type = setup->bRequestType;
        uint8_t code = setup->bRequest;
        uint16_t length = setup->wLength;
        uint16_t index = setup->wIndex;
        uint16_t value = setup->wValue;
        std::vector<char> buf;
        buf.resize(length + 1);
        AccessoryHandshakeState accessoryControlState = AccessoryHandshakeState::UNKNOWN;
        bool updateAccessoryStateRequired = false;

        if ((type & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
            switch (code) {
                case ACCESSORY_GET_PROTOCOL: {
                    if (!handleAccessoryGetProtocol(value, index, length, buf) ||
                        !(type & USB_DIR_IN)) {
                        goto fail;
                    }
                    accessoryControlState = AccessoryHandshakeState::GET_PROTOCOL;
                    break;
                }
                case ACCESSORY_SEND_STRING: {
                    if (!handleAccessorySendString(fd, index, length, buf) || (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    accessoryControlState = AccessoryHandshakeState::SEND_STRING;
                    break;
                }
                case ACCESSORY_START: {
                    if (!handleAccessoryStart(fd, value, index, length, buf) ||
                        (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    accessoryControlState = AccessoryHandshakeState::START;
                    break;
                }
                case ACCESSORY_REGISTER_HID: {
                    if (!handleRegisterHid(value, index) || (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    break;
                }
                case ACCESSORY_UNREGISTER_HID: {
                    if (!handleUnregisterHid(value) || (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    break;
                }
                case ACCESSORY_SET_HID_REPORT_DESC: {
                    if (!handleSetReportHidDescriptor(fd, value, index, length, buf) ||
                        (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    break;
                }
                case ACCESSORY_SEND_HID_EVENT: {
                    if (!handleSendHidEvent(fd, value, length, buf) || (type & USB_DIR_IN)) {
                        goto fail;
                    }
                    break;
                }
                case ACCESSORY_SET_AUDIO_MODE: {
                    LOG(WARNING) << "ACCESSORY_SET_AUDIO_MODE is deprecated and not supported.";
                    break; // Some devices send this. To be ignored as this is not supported.
                }
                default:
                    LOG(ERROR) << "Unrecognized USB vendor request! " << (int)code;
                    goto fail;
            }
        } else {
            LOG(ERROR) << "Unrecognized USB request type " << (int)type;
            goto fail;
        }

        if (type & USB_DIR_IN) {
            if (::write(fd, buf.data(), length) != length) {
                LOG(ERROR) << "Usb error ctrlreq write data" ;
                goto fail;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
            if (mAccessoryFields.controlState != accessoryControlState ||
            accessoryControlState == AccessoryHandshakeState::SEND_STRING) {

                mAccessoryFields.controlState = accessoryControlState;
                updateAccessoryStateRequired = true;
            }
        }
        if (updateAccessoryStateRequired) updateAccessoryState(accessoryControlState);

        return;
        fail:
        // stall control endpoint by applying opposite i/o
        if (type & USB_DIR_IN) {
            if (::read(fd, buf.data(), 0) != -1 || errno != EL2HLT) {
                LOG(ERROR) << "Couldn't halt ep0 on in request";
            }
        } else {
            if (::write(fd, buf.data(), 0) != -1 || errno != EL2HLT) {
                LOG(ERROR) << "Couldn't halt ep0 on out request";
            }
        }

    }

    void VendorControlRequestMonitor::updateAccessoryState(AccessoryHandshakeState newState) {
        std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
        if (mService) {
            mService->notifyStateChange(newState);
        } else {
            LOG(WARNING) << "Service not attached, dropping state: " << static_cast<int>(newState);
        }
    }

    android::base::unique_fd VendorControlRequestMonitor::getUhidFd() {
        return android::base::unique_fd(::open(UHID_PATH, O_RDWR | O_CLOEXEC));
    }

    bool VendorControlRequestMonitor::writeUhidEvent(int fd, struct uhid_event &ev) {
        ssize_t ret = TEMP_FAILURE_RETRY(::write(fd, &ev, sizeof(ev)));
        if (ret != sizeof(ev)) {
            PLOG(ERROR) << "Failed to send event type: " << ev.type;
            return false;
        }
        return true;
    }

    bool VendorControlRequestMonitor::readBuffer(int fd, std::vector<char> &buf, uint16_t length) {
        ssize_t bytesRead = TEMP_FAILURE_RETRY(::read(fd, buf.data(), length));
        if (bytesRead != static_cast<ssize_t>(length)) {
            PLOG(ERROR) << "Could not read buffer (expected " << length << ", got "
                        << bytesRead << ")";
            return false;
        }
        return true;
    }

    void VendorControlRequestMonitor::unregisterHid(int hidId) {
        auto it = mHidDeviceFds.find(hidId);
        if (it != mHidDeviceFds.end() && it->second.ok()) {
            int fd = it->second;
            uhid_event ev = {};
            ev.type = UHID_DESTROY;
            // As per the uhid kernel doc, we can send UHID_DESTROY to unregister the device.
            // If the write fails for any reason we will log it.
            // Also, we are calling the reset() which close() the fd, the device is automatically
            // unregistered and destroyed internally
            // https://docs.kernel.org/hid/uhid.html
            writeUhidEvent(fd, ev);
            it->second.reset();
        }
        mHidDeviceFds.erase(hidId);
        mPendingDescriptors.erase(hidId);
    }

    bool VendorControlRequestMonitor::isHidIdRegistered(uint16_t hidId) {
        return std::find(mHidList.begin(), mHidList.end(), hidId) != mHidList.end();
    }

    bool VendorControlRequestMonitor::validateDescriptorParams(uint16_t hidId, uint16_t index,
                                                               uint16_t length) {
        auto descIt = mPendingDescriptors.find(hidId);
        if (descIt == mPendingDescriptors.end()) {
            LOG(ERROR) << "No pending descriptor found for HID ID: " << hidId;
            return false;
        }
        if (index != descIt->second.descBuf.size()) {
            LOG(ERROR) << "Mismatch in descriptor buffer index for HID ID " << hidId
                       << ": Expected " << descIt->second.descBuf.size()
                       << ", got " << index;
            return false;
        }
        if (index + length > descIt->second.descLength) {
            LOG(ERROR) << "Descriptor chunk for HID ID extends beyond total expected length";
            return false;
        }
        return true;
    }

    bool VendorControlRequestMonitor::registerUhidDevice(uint16_t hidId,
                                                         const hid_descriptor &completeDescriptor) {
        // Try to open a uhid fd
        android::base::unique_fd uhid_fd = getUhidFd();
        if (!uhid_fd.ok()) {
            PLOG(ERROR) << "Failed to open /dev/uhid";
            return false;
        }
        mHidDeviceFds[hidId] = std::move(uhid_fd);
        // Initialise a Uhid Create Event
        uhid_event ev = {};
        ev.type = UHID_CREATE2;
        strlcpy(reinterpret_cast<char *>(ev.u.create2.name), "hidDev",
                sizeof(ev.u.create2.name) - 1);
        strlcpy(reinterpret_cast<char *>(ev.u.create2.uniq), std::to_string(hidId).c_str(),
                sizeof(ev.u.create2.uniq) - 1); // Use HID ID as unique string
        memcpy(ev.u.create2.rd_data, completeDescriptor.descBuf.data(),
               completeDescriptor.descLength);
        ev.u.create2.rd_size = completeDescriptor.descLength;
        ev.u.create2.bus = BUS_USB;
        ev.u.create2.vendor = HID_ANY_ID;
        ev.u.create2.product = HID_ANY_ID;
        ev.u.create2.version = 0;
        ev.u.create2.country = 0;
        // Send event to create a uhid device
        if (!writeUhidEvent(mHidDeviceFds[hidId], ev)) {
            return false;
        }
        // Wait for the uhid fd to actually be started.
        ssize_t ret = TEMP_FAILURE_RETRY(::read(mHidDeviceFds[hidId], &ev, sizeof(ev)));
        if (ret < 0 || ev.type != UHID_START) {
            PLOG(ERROR) << "uhid node failed to start:";
            return false;
        }
        return true;
    }

    bool VendorControlRequestMonitor::handleAccessoryGetProtocol(uint16_t value,
                                                                 uint16_t index, uint16_t length,
                                                                 std::vector<char> &buf) {
        if (value != 0 || index != 0 || length != 2) {
            LOG(WARNING) << "Malformed GET_PROTOCOL: (value=" << value << ", index=" << index
                         << ", length=" << length << ") - exp 0,0,2)";
        }
        if (length >= 2) {
            uint16_t *protocolVersion = reinterpret_cast<uint16_t *>(buf.data());
            protocolVersion[0] = htole16(ACCESSORY_VERSION);
            return true;
        }
        LOG(ERROR) << "Protocol length too short to write version";
        return false;
    }

    bool VendorControlRequestMonitor::handleAccessorySendString(int fd, uint16_t index,
                                                                uint16_t length,
                                                                std::vector<char> &buf) {
        if (index >= ACCESSORY_NUM_STRINGS || length > ACCESSORY_STRING_LENGTH || length == 0) {
            // Why is ACCESSORY_STRING_LENGTH 256 as opposed to 255
            LOG(WARNING) << "Malformed SEND_STRING: (index=" << index << ", length=" << length
                         << ") - exp index<" << ACCESSORY_NUM_STRINGS
                         << ", 0<length<=" << ACCESSORY_STRING_LENGTH;
            if (length == 0) return true;
        }
        if (::read(fd, buf.data(), length) != length) {
            LOG(ERROR) << "Usb error ctrlreq read string " << length;
            return false;
        }
        if (index < ACCESSORY_NUM_STRINGS) {
            buf[length] = '\0';
            std::string str(buf.data());
            LOG(INFO) << "Saved string index=" << index << ": " << str;
            std::lock_guard<std::mutex> lock(mAccessoryFieldsMutex);
            mAccessoryFields.strings[index] = str;
        } else {
            LOG(WARNING) << "String index out of bounds, data read but ignored.";
        }
        return true;
    }

    bool VendorControlRequestMonitor::handleAccessoryStart(int fd, uint16_t value, uint16_t index,
                                                           uint16_t length,
                                                           std::vector<char> &buf) {
        if (value != 0 || index != 0 || length != 0) {
            // Certain headunits have value set to 1.
            // Log error, but do not return false for legacy headunit compatibility.
            LOG(INFO) << "Malformed ACCESSORY_START (value:" << value
                      << ", index:" << index
                      << ", length:" << length << ") - exp 0,0,0";
        }
        ssize_t bytesRead = ::read(fd, buf.data(), length);
        if (bytesRead < 0) {
            PLOG(ERROR) << "Error reading in data in ACCESSORY_START";
            return false;
        } else if (bytesRead > 0) {
            // Log error, but do not return false for legacy headunit compatibility.
            LOG(WARNING) << "Unexpected data in ACCESSORY_START control request. Bytes read: "
                         << bytesRead;
        }
        return true;
    }

    bool VendorControlRequestMonitor::handleRegisterHid(uint16_t hidId, uint16_t index) {
        if (index == 0) {
            LOG(ERROR) << "Descriptor length must be > 0.";
            return false;
        }
        if (isHidIdRegistered(hidId)) {
            unregisterHid(hidId);
        }
        mHidList.push_back(hidId);
        hid_descriptor emptyDescriptor = {{}, index};
        mPendingDescriptors[hidId] = std::move(emptyDescriptor);
        return true;
    }

    bool VendorControlRequestMonitor::handleUnregisterHid(uint16_t hidId) {
        auto it = std::find(mHidList.begin(), mHidList.end(), hidId);
        if (it != mHidList.end()) {
            unregisterHid(hidId);
            mHidList.erase(it);
        } else {
            LOG(WARNING) << "handleUnregisterHid: hidId=%u not found in mHidList" << hidId;
        }
        return true;
    }

    bool VendorControlRequestMonitor::handleSetReportHidDescriptor(int fd, uint16_t hidId,
                                                                   uint16_t index, uint16_t length,
                                                                   std::vector<char> &buf) {
        if (!isHidIdRegistered(hidId)) {
            LOG(ERROR) << "Hid ID %u not registered." << hidId;
            return false;
        }

        if (!validateDescriptorParams(hidId, index, length)) {
            return false;
        }

        if (length > UHID_DATA_MAX) {
            LOG(ERROR) << "Descriptor length [" << length << "] > max len [" << UHID_DATA_MAX
                       << "] for UHID_CREATE2.";
            return false;
        }

        if (!readBuffer(fd, buf, length)) {
            return false;
        }

        if (buf.size() == length + 1 && buf.back() == '\0') {
            buf.pop_back();
        }

        auto descIt = mPendingDescriptors.find(hidId);
        if (descIt == mPendingDescriptors.end()) {
            LOG(ERROR) << "No pending descriptor found for HID ID " << hidId;
            return false;
        }
        descIt->second.descBuf.insert(descIt->second.descBuf.end(), buf.begin(), buf.end());

        // Wait for complete descriptor before registering
        if (descIt->second.descBuf.size() != descIt->second.descLength) {
            return true;
        }

        if (!registerUhidDevice(hidId, descIt->second)) {
            mPendingDescriptors.erase(hidId);
            mHidDeviceFds.erase(hidId);
            return false;
        }
        mPendingDescriptors.erase(hidId);

        return true;
    }

    bool VendorControlRequestMonitor::handleSendHidEvent(int fd, uint16_t hidId, uint16_t length,
                                                         std::vector<char> &buf) {
        if (!isHidIdRegistered(hidId)) {
            LOG(ERROR) << "Hid ID not registered: " << hidId;
            return false;
        }

        auto it = mHidDeviceFds.find(hidId);
        if (it == mHidDeviceFds.end()) {
            LOG(ERROR) << "Cannot send HID event, UHID fd not found for ID: " << hidId;
            return false;
        }

        if (length > UHID_DATA_MAX) {
            LOG(ERROR) << "event length [" << length <<"] > max length [" << UHID_DATA_MAX
                       << "] for UHID_INPUT2.";
            return false;
        }

        if (!readBuffer(fd, buf, length)) {
            return false;
        }

        struct uhid_event ev = {};
        ev.type = UHID_INPUT2;
        ev.u.input2.size = length;
        memcpy(&ev.u.input2.data, buf.data(), length * sizeof(ev.u.input2.data[0]));

        return writeUhidEvent(it->second, ev);
    }

} // namespace android
