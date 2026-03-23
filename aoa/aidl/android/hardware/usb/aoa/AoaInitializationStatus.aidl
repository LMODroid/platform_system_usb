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

package android.hardware.usb.aoa;

/**
 * Status of the AOA Daemon initialization.
 * Used by UsbDeviceManager to determine if the userspace implementation
 * is healthy and ready to serve requests.
 */
parcelable AoaInitializationStatus {
    /**
     * True if the FunctionFS directory exists.
     */
    boolean isFfsDirectoryPresent;

    /**
     * The result of opening the accessory control endpoint.
     *
     * Values map to the OpenAccessoryControlResult enum in atoms.proto:
     * 0 = UNSPECIFIED
     * 1 = OPEN_ACCESSORY_FAILED
     * 2 = WRITE_DESC_FAILED
     * 3 = WRITE_STRING_FAILED
     * 4 = SELINUX_PERMISSION_FAILED
     * 5 = SUCCESS
     */
    int openControlResult;
}
