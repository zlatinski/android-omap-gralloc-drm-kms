# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


LOCAL_PATH := $(call my-dir)

# HAL module implemenation stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libEGL \
	libdrm \
	libhardware \
	libhwdrm

LOCAL_SRC_FILES := hwcomposer.c
LOCAL_MODULE := hwcomposer.$(TARGET_PRODUCT)
LOCAL_CFLAGS:= -DLOG_TAG=\"hwcomposer\" -Wall -Wno-unused-parameter -O0 -g
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := \
	external/drm \
	external/drm/include/drm \
	hardware/libhardware_drm

include $(BUILD_SHARED_LIBRARY)
