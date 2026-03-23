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
 * AoaDescriptors.h
 * Definitions for AOA and Control FunctionFS descriptors.
 */

#pragma once

#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <sys/types.h>
#include <endian.h>

#define EPOLL_MAX_EVENTS 4
#define FFS_NUM_EVENTS 5
#define FFS_VENDOR_CTRL_REQUEST_EP0 "/dev/usb-ffs/ctrl/ep0"
#define CTRL_INTERFACE_PROTOCOL 0xff

#define FFS_ACCESSORY_EP0 "/dev/usb-ffs/aoa/ep0"
#define FFS_ACCESSORY_EP1 "/dev/usb-ffs/aoa/ep1"
#define FFS_ACCESSORY_EP2 "/dev/usb-ffs/aoa/ep2"

namespace {
/*****************************
 * Accessory Descriptors
 *****************************/
    struct func_desc {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_endpoint_descriptor_no_audio sink;
    } __attribute__((packed));

    struct func_desc_ss {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_ss_ep_comp_descriptor source_comp;
        struct usb_endpoint_descriptor_no_audio sink;
        struct usb_ss_ep_comp_descriptor sink_comp;
    } __attribute__((packed));

    struct desc_v2 {
        struct usb_functionfs_descs_head_v2 header;
        // The rest of the structure depends on the flags in the header.
        __le32 fs_count;
        __le32 hs_count;
        __le32 ss_count;
        struct func_desc fs_descs, hs_descs;
        struct func_desc_ss ss_descs;
    } __attribute__((packed));

    const struct usb_interface_descriptor interface_desc = {
            .bLength = USB_DT_INTERFACE_SIZE,
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
            .bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
            .bInterfaceProtocol = 0,
            .iInterface = 1,
    };

    const struct usb_endpoint_descriptor_no_audio fs_sink = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(64),
    };

    const struct usb_endpoint_descriptor_no_audio fs_source = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(64),
    };

    const struct usb_endpoint_descriptor_no_audio hs_sink = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(512),
    };

    const struct usb_endpoint_descriptor_no_audio hs_source = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(512),
    };

    const struct usb_endpoint_descriptor_no_audio ss_sink = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 1 | USB_DIR_IN,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(1024),
    };

    const struct usb_endpoint_descriptor_no_audio ss_source = {
            .bLength = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = 2 | USB_DIR_OUT,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = htole16(1024),
    };

    const struct usb_ss_ep_comp_descriptor ss_sink_comp = {
            .bLength = sizeof(ss_sink_comp),
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
            .bMaxBurst = 6,
    };

    const struct usb_ss_ep_comp_descriptor ss_source_comp = {
            .bLength = sizeof(ss_source_comp),
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
            .bMaxBurst = 6,
    };

    const struct func_desc fs_descriptors = {
            .intf = interface_desc,
            .source = fs_source,
            .sink = fs_sink,
    };

    const struct func_desc hs_descriptors = {
            .intf = interface_desc,
            .source = hs_source,
            .sink = hs_sink,
    };

    const struct func_desc_ss ss_descriptors = {
            .intf = interface_desc,
            .source = ss_source,
            .source_comp = ss_source_comp,
            .sink = ss_sink,
            .sink_comp = ss_sink_comp,
    };

    const struct desc_v2 acc_desc = {
            .header =
                    {
                            .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
                            .length = htole32(sizeof(acc_desc)),
                            .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
                                             FUNCTIONFS_HAS_SS_DESC),
                    },
            .fs_count = htole32(3),
            .hs_count = htole32(3),
            .ss_count = htole32(5),
            .fs_descs = fs_descriptors,
            .hs_descs = hs_descriptors,
            .ss_descs = ss_descriptors,
    };

#define ACC_INTERFACE_STR "Android Accessory Interface"
    struct acc_functionfs_lang {
        __le16 code;
        char str1[sizeof(ACC_INTERFACE_STR)];
    } __attribute__((packed));

    struct acc_functionfs_strings {
        struct usb_functionfs_strings_head header;
        struct acc_functionfs_lang lang0;
    } __attribute__((packed));

    const struct acc_functionfs_strings acc_strings = {
            .header =
                    {
                            .magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
                            .length = htole32(sizeof(acc_strings)),
                            .str_count = htole32(1),
                            .lang_count = htole32(1),
                    },
            .lang0 =
                    {
                            .code = htole16(0x0409),
                            .str1 = ACC_INTERFACE_STR,
                    },
    };

/*****************************
 * Control Descriptors
 *****************************/
    const struct usb_interface_descriptor ctrl_interface_desc = {
            .bLength = USB_DT_INTERFACE_SIZE,
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
            .bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
            .bInterfaceProtocol = CTRL_INTERFACE_PROTOCOL,
            .iInterface = 1,
    };

    const struct func_desc ctrl_fs_descriptors = {
            .intf = ctrl_interface_desc,
            .source = fs_source,
            .sink = fs_sink,
    };

    const struct func_desc ctrl_hs_descriptors = {
            .intf = ctrl_interface_desc,
            .source = hs_source,
            .sink = hs_sink,
    };

    const struct func_desc_ss ctrl_ss_descriptors = {
            .intf = ctrl_interface_desc,
            .source = ss_source,
            .source_comp = ss_source_comp,
            .sink = ss_sink,
            .sink_comp = ss_sink_comp,
    };

    const struct desc_v2 ctrl_desc = {
            .header =
                    {
                            .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
                            .length = htole32(sizeof(ctrl_desc)),
                            .flags = htole32(FUNCTIONFS_ALL_CTRL_RECIP | FUNCTIONFS_CONFIG0_SETUP |
                                             FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
                                             FUNCTIONFS_HAS_SS_DESC),
                    },
            .fs_count = htole32(3),
            .hs_count = htole32(3),
            .ss_count = htole32(5),
            .fs_descs = ctrl_fs_descriptors,
            .hs_descs = ctrl_hs_descriptors,
            .ss_descs = ctrl_ss_descriptors,
    };

#define CTRL_INTERFACE_STR "Android Control Interface"
    struct ctrl_functionfs_lang {
        __le16 code;
        char str1[sizeof(CTRL_INTERFACE_STR)];
    } __attribute__((packed));

    struct ctrl_functionfs_strings {
        struct usb_functionfs_strings_head header;
        struct ctrl_functionfs_lang lang0;
    } __attribute__((packed));

    const struct ctrl_functionfs_strings ctrl_strings = {
            .header =
                    {
                            .magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
                            .length = htole32(sizeof(ctrl_strings)),
                            .str_count = htole32(1),
                            .lang_count = htole32(1),
                    },
            .lang0 =
                    {
                            .code = htole16(0x0409),
                            .str1 = CTRL_INTERFACE_STR,
                    },
    };

} // namespace
