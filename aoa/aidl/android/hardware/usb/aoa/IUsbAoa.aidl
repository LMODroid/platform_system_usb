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
import android.hardware.usb.aoa.IUsbAoaCallback;
import android.hardware.usb.aoa.AccessoryMetadata;
import android.hardware.usb.aoa.AoaInitializationStatus;

/**
 * Interface for the AOA daemon (aoad).
 * This daemon manages the userspace implementation of the Android Open Accessory (AOA) protocol.
 */
interface IUsbAoa {
    /**
     * Registers a callback to receive AOA events from the daemon.
     *
     * @param callback The callback interface to receive events like ACCESSORY_START.
     */
    void setCallback(in IUsbAoaCallback callback);

    /**
     * Retrieves the result of the daemon's boot-time self-initialization.
     *
     * When the `aoad` daemon starts, it automatically attempts to:
     *
     * Verify that the required FunctionFS (FFS) directories are present.
     * Open the accessory control endpoint (ep0) and write the necessary USB descriptors.
     * Start the monitoring loop for vendor-specific control requests.
     *
     * This method returns the cached outcome of those operations to allow clients
     * (like UsbDeviceManager) to determine if the userspace AOA implementation is
     * healthy and ready to serve requests.
     *
     * @return A AoaInitializationStatus containing the presence of FFS directories
     * and the specific result code of the control endpoint open attempt.
     */
    AoaInitializationStatus getInitializationStatus();

    /**
     * Opens the primary accessory data connection.
     * Returns a file descriptor for the bidirectional data transfer.
     * @return A ParcelFileDescriptor pointing to the accessory data endpoint.
     */
    ParcelFileDescriptor openAccessory();

    /**
     * Opens the accessory data endpoint specifically for reading data.
     *
     * @return A ParcelFileDescriptor for the input stream.
     */
    ParcelFileDescriptor openAccessoryForInputStream();

    /**
     * Opens the accessory data endpoint specifically for writing data (BULK OUT).
     *
     * @return A ParcelFileDescriptor for the output stream.
     */
    ParcelFileDescriptor openAccessoryForOutputStream();

    /**
     * Retrieves the maximum packet size negotiated for the AOA connection.
     *
     * @return The maximum packet size in bytes.
     */
    int getMaxPacketSize();

    /**
     * Retrieves the accessory information strings (Manufacturer, Model, etc.)
     * stored by the daemon during the handshake.
     *
     * @return A list of strings corresponding to the standard AOA string identifiers.
     */
    AccessoryMetadata getAccessoryStrings();

    /**
     * Checks if the ACCESSORY_START request has been received from the host.
     *
     * @return true if the start request has been received, false otherwise.
     */
    boolean isStartRequested();
}
