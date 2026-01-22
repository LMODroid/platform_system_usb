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

#include "AccessoryLegacyBridgeThread.h"
#include <unistd.h>

namespace android {

    AccessoryLegacyBridgeThread::AccessoryLegacyBridgeThread(
            android::base::unique_fd ffs_read_fd,
            android::base::unique_fd ffs_write_fd,
            android::base::unique_fd app_socket_fd,
            int max_packet_size)
            : mFfsReadFd(std::move(ffs_read_fd)),
              mFfsWriteFd(std::move(ffs_write_fd)),
              mAppSocketFd(std::move(app_socket_fd)),
              mMaxPacketSize(max_packet_size) {

    }

    AccessoryLegacyBridgeThread::~AccessoryLegacyBridgeThread() {

    }

} // namespace android
