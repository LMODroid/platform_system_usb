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
import android.hardware.usb.aoa.AccessoryHandshakeState;

/**
 * Callback interface for clients (UsbDeviceManager) to receive AOA events from the daemon.
 */
oneway interface IUsbAoaCallback {
    /**
     * Called when the AOA state changes.
     *
     * @param state The new AccessoryState from the protocol handshake.
     */
    void onAccessoryStateChanged(in AccessoryHandshakeState state);
}
