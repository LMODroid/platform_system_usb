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

        mData.resize(USB_FFS_ALL_BUFS_SIZE * 2);
        mIocb.resize(USB_FFS_NUM_BUFS * 2);
        mIocbs.resize(USB_FFS_NUM_BUFS * 2);
        mEvents.resize(USB_FFS_NUM_BUFS);
        for (unsigned i = 0; i < USB_FFS_NUM_BUFS * 2; i++) {
            mIocbs[i] = &mIocb[i];
        }

        if (io_setup(USB_FFS_NUM_BUFS, &mReadCtx) < 0 ||
            io_setup(USB_FFS_NUM_BUFS, &mWriteCtx) < 0) {
            LOG(ERROR) << "unable to setup aio";
            return;
        }

        mReadEventFd.reset(eventfd(0, 0));

        pipe(mShutdownPipeFd);
        mThread = std::thread(&AccessoryLegacyBridgeThread::monitorLoop, this);
    }

    AccessoryLegacyBridgeThread::~AccessoryLegacyBridgeThread() {
        LOG(DEBUG) << "tearing down NativeAccessoryLegacyBridgeThread...";
        stop();
        close(mShutdownPipeFd[0]);
        close(mShutdownPipeFd[1]);
        if (mReadCtx != 0) {
            if (io_destroy(mReadCtx) < 0) LOG(ERROR) << "io_destroy read_ctx failed";
            mReadCtx = 0;
        }
        if (mWriteCtx != 0) {
            if (io_destroy(mWriteCtx) < 0) LOG(ERROR) << "io_destroy write_ctx failed";
            mWriteCtx = 0;
        }
    }

    void AccessoryLegacyBridgeThread::stop() {
        if (mThread.joinable()) {
            int c = 'q';
            write(mShutdownPipeFd[1], &c, 1);
            mThread.join();
        }
    }

    void AccessoryLegacyBridgeThread::monitorLoop() {
        struct pollfd pollFds[3];

        pollFds[0].fd = mReadEventFd.get();
        pollFds[0].events = POLLIN;
        pollFds[1].fd = mAppSocketFd.get();
        pollFds[1].events = POLLIN | POLLHUP | POLLRDHUP | POLLERR;
        pollFds[2].fd = mShutdownPipeFd[0];
        pollFds[2].events = POLLIN;

        struct iocb** read_iocbs = mIocbs.data();
        char* read_buf = mData.data();
        int ret;

        ret = iobufSubmit(mReadCtx, read_iocbs, mFfsReadFd.get(), read_buf, USB_FFS_ALL_BUFS_SIZE,
                          mReadEventFd.get(), true, mMaxPacketSize);
        if (ret < 0) {
            LOG(ERROR) << "accessory legacy bridge initial read iobufSubmit failed";
            return;
        }
        LOG(INFO) << "accessory legacy bridge thread loop starting...";

        while (true) {
            if (poll(pollFds, 3, -1) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                PLOG(ERROR) << "Error during poll";
                break;
            }

            if (pollFds[0].revents & POLLIN) {
                pollFds[0].revents = 0;
                if (!handleReadEvents()) {
                    goto exit;
                }
            }

            if (pollFds[1].revents & POLLIN) {
                pollFds[1].revents = 0;
                if (!handleSocketInput()) {
                    goto exit;
                }
            }

            if (pollFds[1].revents & (POLLHUP | POLLERR | POLLRDHUP)) {
                pollFds[1].revents = 0;
                PLOG(ERROR) << "accessory socket shut down";
                goto exit;
            }

            if (pollFds[2].revents & POLLIN) {
                LOG(INFO) << "Shutdown signaled for accessory legacy bridge thread.";
                goto exit;
            }
        }
        exit:
        LOG(INFO) << "accessory socket thread exiting using readFd " << mFfsReadFd.get()
                  << " and writeFd " << mFfsWriteFd.get();
        mFfsReadFd.reset();
        mFfsWriteFd.reset();
        if (mReadCtx != 0) {
            io_destroy(mReadCtx);
            mReadCtx = 0;
        }
        if (mWriteCtx != 0) {
            io_destroy(mWriteCtx);
            mWriteCtx = 0;
        }
    }

    int AccessoryLegacyBridgeThread::iobufSubmit(aio_context_t ctx, struct iocb **iocbs, int fd,
                                                 char *buf, int length, int evfd, bool read,
                                                 int maxPacketSize) {
        int prepared_data_iocb_count = 0;
        size_t bytes_assigned = 0;

        // Determine if a ZLP might be needed after this data transfer (only for writes)
        bool zlp_conditionally_needed =
                (!read && length > 0 && (length % static_cast<size_t>(maxPacketSize) == 0));

        int max_iocbs_for_data_payload = USB_FFS_NUM_BUFS;
        if (zlp_conditionally_needed) {
            max_iocbs_for_data_payload = USB_FFS_NUM_BUFS - 1; // Reserve one slot for ZLP
        }

        for (int j = 0; j < max_iocbs_for_data_payload; j++) {
            if (bytes_assigned >= length) {
                break;
            }
            size_t current_chunk_size =
                    std::min(static_cast<size_t>(USB_FFS_BUF_SIZE), length - bytes_assigned);

            io_prep(iocbs[j], fd, buf + bytes_assigned, current_chunk_size, /* offset= */ 0, read);
            iocbs[j]->aio_data = reinterpret_cast<uint64_t>(iocbs[j]);

            // TODO: b/440765338 - Improve use of eventfd path
            if (evfd != -1) { // ensure eventfd is set for current IOCB
                iocbs[j]->aio_flags |= IOCB_FLAG_RESFD;
                iocbs[j]->aio_resfd = evfd;
            }

            bytes_assigned += current_chunk_size;
            prepared_data_iocb_count++;
        }

        int total_iocbs_to_submit_in_batch = prepared_data_iocb_count;
        bool zlp_actually_appended = false;

        // TODO: b/440767232 - Implement rescheduled ZLP
        if (prepared_data_iocb_count > 0 && zlp_conditionally_needed) {
            if (prepared_data_iocb_count < USB_FFS_NUM_BUFS) { // Space for ZLP IOCB
                io_prep_pwrite(iocbs[prepared_data_iocb_count], fd, nullptr, 0, 0); // ZLP
                iocbs[prepared_data_iocb_count]->aio_data =
                        reinterpret_cast<uint64_t>(iocbs[prepared_data_iocb_count]);

                // TODO: b/440765338 - Improve use of eventfd path
                if (evfd != -1) {
                    iocbs[prepared_data_iocb_count]->aio_flags |= IOCB_FLAG_RESFD;
                    iocbs[prepared_data_iocb_count]->aio_resfd = evfd;
                }
                total_iocbs_to_submit_in_batch++;
                zlp_actually_appended = true;
            }
        }

        int submitted_count = io_submit(ctx, total_iocbs_to_submit_in_batch, iocbs);

        if (submitted_count < 0) {
            PLOG(ERROR) << "io_submit for " << (read ? "read" : "write")
                        << " failed (prepared " << total_iocbs_to_submit_in_batch
                        << "): " << submitted_count;

            return submitted_count;
        }

        if (submitted_count != total_iocbs_to_submit_in_batch) {
            LOG(WARNING) << "io_submit only enqueued " << submitted_count
                         << " of " << total_iocbs_to_submit_in_batch << " prepared IOCBs";
            if (zlp_actually_appended) {
                // This implies ZLP might not have been submitted.
                LOG(ERROR) << "ZLP was prepared but might not have been submitted due to "
                           << "short submit!";
            }
        }
        return submitted_count;
    }

    bool AccessoryLegacyBridgeThread::handleReadEvents() {
        uint64_t ev_cnt = 0;
        if (::read(mReadEventFd, &ev_cnt, sizeof(ev_cnt)) == -1) {
            PLOG(ERROR) << "unable to read eventfd";
            return false;
        }

        int num_events = TEMP_FAILURE_RETRY(
                io_getevents(mReadCtx, 0, USB_FFS_NUM_BUFS, mEvents.data(), &ZERO_TIMEOUT));
        if (num_events < 0) {
            PLOG(ERROR) << "error getting events";
            return false;
        }

        // Process all completed events
        for (int i = 0; i < num_events; i++) {
            struct iocb* completed_iocb = reinterpret_cast<struct iocb*>(mEvents[i].obj);
            long result = mEvents[i].res;

            if (result < 0) {
                LOG(ERROR) << "got error event on read: " << strerror(-result);
                return false;
            }

            if (!completed_iocb) {
                LOG(ERROR) << "mEvents[" << i << "].obj is null! Skipping this event.";
                continue;
            }

            // Write completed data to the socket
            int ret = write(mAppSocketFd.get(), reinterpret_cast<void *>(completed_iocb->aio_buf),
                            result);
            if (ret < 0) {
                PLOG(ERROR) << "error writing to socket";
                return false;
            }

            // Re-submit the iocb for another read
            io_prep_pread(completed_iocb, mFfsReadFd.get(),
                          reinterpret_cast<char*>(completed_iocb->aio_buf), USB_FFS_BUF_SIZE, 0);
            completed_iocb->aio_data = reinterpret_cast<uint64_t>(completed_iocb);
            completed_iocb->aio_flags |= IOCB_FLAG_RESFD;
            completed_iocb->aio_resfd = mReadEventFd.get();

            struct iocb* iocbs_to_submit[] = {completed_iocb};
            int submitted_count = io_submit(mReadCtx, 1, iocbs_to_submit);
            if (submitted_count < 0) {
                PLOG(ERROR) << "Error re-submitting read iocb: " << submitted_count;
                return false;
            }
            if (submitted_count != 1) {
                LOG(WARNING) << "Could not re-submit read iocb";
            }
        }
        return true;
    }

    bool AccessoryLegacyBridgeThread::handleSocketInput() {
        char* write_buf = mData.data() + USB_FFS_BUF_WRITE_OFFSET;
        int ret = read(mAppSocketFd.get(), write_buf, USB_FFS_ALL_BUFS_SIZE);

        if (ret == 0) {
            LOG(ERROR) << "accessory socket closed by client";
            return false;
        }
        if (ret < 0) {
            PLOG(ERROR) << "accessory socket read failed";
            return false;
        }

        struct iocb** write_iocbs = mIocbs.data() + USB_FFS_NUM_BUFS;
        ret = iobufSubmit(mWriteCtx, write_iocbs, mFfsWriteFd.get(), write_buf, ret, -1, false,
                          mMaxPacketSize);
        if (ret < 0) {
            return false;
        }

        int this_events =
                TEMP_FAILURE_RETRY(io_getevents(mWriteCtx, ret, ret, mEvents.data(), nullptr));
        if (this_events < ret) {
            LOG(ERROR) << "got error waiting for write to complete, expected " << ret
                       << " got " << this_events;
            return false;
        }
        for (int i = 0; i < ret; i++) {
            if (mEvents[i].res < 0) {
                LOG(ERROR) << "got error event on " << i << " of " << ret
                           << " write events: " << strerror(-mEvents[i].res);
                return false;
            }
        }
        return true;
    }

} // namespace android
