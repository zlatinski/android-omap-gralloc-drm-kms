# Copyright 2006 The Android Open Source Project

# Setting LOCAL_PATH will mess up all-subdir-makefiles, so do it beforehand.
SUBDIR_MAKEFILES := $(call all-named-subdir-makefiles,modules)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libhwdrm \
	libGLESv1_CM # for glFlush/glFinish

LOCAL_SRC_FILES += \
	gralloc.c

LOCAL_MODULE := gralloc.$(TARGET_PRODUCT)

LOCAL_CFLAGS:= -DLOG_TAG=\"gralloc\" -Wall

LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := \
	external/drm \
	external/drm/include/drm \
	hardware/libhardware_drm

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

include $(BUILD_SHARED_LIBRARY)

include $(SUBDIR_MAKEFILES)
