/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.usb.tests.hostside;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.RunUtil;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

@RunWith(DeviceJUnit4ClassRunner.class)
public class UsbHostsideTest extends BaseHostJUnit4Test {
    private static final long CONN_TIMEOUT_MS = 15000;
    private static final long STABILITY_SLEEP_MS = 1500;
    private static final long STABILITY_ITERATIONS = 3;

    private ITestDevice mDevice;

    @Before
    public void setUp() throws Exception {
        mDevice = getDevice();
        mDevice.waitForDeviceAvailable(CONN_TIMEOUT_MS);
    }

    @After
    public void tearDown() throws Exception {
        if (mDevice != null) {
            CLog.i("Tearing down: ensuring framework is started.");
            mDevice.executeShellCommand("start");
            mDevice.waitForDeviceAvailable(CONN_TIMEOUT_MS);
        }
    }

    @Test
    public void testUsbStability() throws Exception {
        clearLogCat();
        CLog.i("Switching to root.");
        mDevice.enableAdbRoot();

        assumeTrue("Could not switch to root, Skipping Test.", mDevice.isAdbRoot());

        // We run multiple iterations as sometimes USB stack set up after adb root takes time
        for (int i = 0; i < STABILITY_ITERATIONS; i++) {
            CLog.i("Stop/Start loop iteration: " + i);

            CLog.i("Running Stop");
            String output = mDevice.executeShellCommand("stop");
            assertTrue("Unexpected output from stop command: [" + output + "]",
                    output == null || output.trim().isEmpty());

            mDevice.waitForDeviceOnline(CONN_TIMEOUT_MS);

            CLog.i("Sleeping.");
            RunUtil.getDefault().sleep(STABILITY_SLEEP_MS);

            CLog.i("Running Start");
            output = mDevice.executeShellCommand("start");
            CLog.i("Output: [" + output + "]");
            assertTrue("Unexpected output from start command: [" + output + "]",
                    output == null || output.trim().isEmpty());

            CLog.i("Sleeping.");
            RunUtil.getDefault().sleep(STABILITY_SLEEP_MS);
        }
    }

    private void clearLogCat() throws DeviceNotAvailableException {
        mDevice.executeAdbCommand("logcat", "-c");
    }
}
