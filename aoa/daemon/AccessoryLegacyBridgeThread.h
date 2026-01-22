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
 * AccessoryLegacyBridgeThread.h
 * Handles high-speed data transfer between USB and the App Socket.
 */

#pragma once

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/unique_fd.h>
#include <asyncio/AsyncIO.h>
#include <thread>
#include <vector>
#include <mutex>
#include <linux/aio_abi.h>
#include <sys/eventfd.h>
#include <sys/poll.h>

namespace android {

    class AccessoryLegacyBridgeThread {
    public:
        explicit AccessoryLegacyBridgeThread(android::base::unique_fd ffs_read_fd,
                                             android::base::unique_fd ffs_write_fd,
                                             android::base::unique_fd app_socket_fd,
                                             int max_packet_size);

        ~AccessoryLegacyBridgeThread();
        void stop();

    private:
        void monitorLoop();
        int iobufSubmit(aio_context_t ctx, struct iocb **iocbs, int fd, char *buf, int length,
                int evfd, bool read, int maxPacketSize);
        bool handleReadEvents();
        bool handleSocketInput();

        // File Descriptors
        android::base::unique_fd mFfsReadFd;
        android::base::unique_fd mFfsWriteFd;
        android::base::unique_fd mAppSocketFd;

        // Control
        [[maybe_unused]] int mMaxPacketSize;
        [[maybe_unused]] int mShutdownPipeFd[2];
        std::thread mThread;

        // AIO Context & Buffers
        std::vector<struct iocb> mIocb;
        std::vector<struct iocb*> mIocbs;
        std::vector<char> mData;
        std::vector<struct io_event> mEvents;
        android::base::unique_fd mReadEventFd;
        [[maybe_unused]] aio_context_t mReadCtx = 0;
        [[maybe_unused]] aio_context_t mWriteCtx = 0;

        // Constants
        static constexpr int USB_FFS_NUM_BUFS = 16;
        static constexpr int USB_FFS_BUF_SIZE = 16384;
        static constexpr int USB_FFS_ALL_BUFS_SIZE = USB_FFS_NUM_BUFS * USB_FFS_BUF_SIZE;
        static constexpr int USB_FFS_BUF_WRITE_OFFSET = USB_FFS_ALL_BUFS_SIZE;
        [[maybe_unused]] struct timespec ZERO_TIMEOUT = {0, 0};

        DISALLOW_COPY_AND_ASSIGN(AccessoryLegacyBridgeThread);
    };

} // namespace android
