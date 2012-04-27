/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>

extern "C" {
#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>
}

/*****************************************************************************/

struct hwc_context_t {
	hwc_composer_device_t device;
	/* our private state goes below here */

	struct drm_module_t *drm_module;

	int drm_fd;
};

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "DRM/KMS hwcomposer module",
        author: "",
        methods: &hwc_module_methods,
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGI("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list)
{
	if (list && (list->flags & HWC_GEOMETRY_CHANGED)) {
		LOGI("%s:\n", __func__);

		for (size_t i=0 ; i<list->numHwLayers ; i++) {
			dump_layer(&list->hwLayers[i]);
			list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
		}
	}

	return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
	LOGI("%s:\n", __func__);

	for (size_t i=0 ; i<list->numHwLayers ; i++) {
		dump_layer(&list->hwLayers[i]);
	}

	EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
	if (!sucess) {
		return HWC_EGL_ERROR;
	}

	return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/

static int
drm_open(struct hwc_context_t *dev)
{
	struct drm_module_t *module;
	drmVersionPtr version;
	int ret;

	ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
			    (const hw_module_t **) &module);
	if (ret) {
		LOGE("Failed to get gralloc module: %s\n", strerror(-ret));
		return ret;
	}

	if (strcmp(module->base.common.name, "DRM Memory Allocator")) {
		LOGE("Wrong gralloc module: %s\n", module->base.common.name);
		return -EINVAL;
	}

	LOGI("Using gralloc module \"%s\" version %d.%d\n",
	     module->base.common.name, module->base.common.version_major,
	     module->base.common.version_minor);

	dev->drm_fd = module->drm->fd;

	version = drmGetVersion(dev->drm_fd);
	if (!version) {
		LOGE("Failed to get DRM Version\n");
		return -errno;
	}

	LOGI("Using DRM %s %s %s\n", version->name, version->date,
	     version->desc);
	drmFree(version);

	return 0;
}

static int
drm_list_kms(struct hwc_context_t *dev)
{
	drmModeResPtr resources;
	drmModePlaneResPtr planes;
	int i;

	resources = drmModeGetResources(dev->drm_fd);
	if (!resources) {
		LOGE("Failed to get KMS resources\n");
		return -EINVAL;
	}

	planes = drmModeGetPlaneResources(dev->drm_fd);
	if (!planes) {
		LOGE("Failed to get KMS resources\n");
		drmModeFreeResources(resources);
		return -EINVAL;
	}

	LOGI("KMS resources:\n");
	LOGI("\tDimensions: (%d, %d) -> (%d, %d)\n",
	     resources->min_width, resources->min_height,
	     resources->max_width, resources->max_height);

	LOGI("\tFBs:\n");
	for (i = 0; i < resources->count_fbs; i++)
		LOGI("\t\t%d\n", resources->fbs[i]);

	LOGI("\tPlanes:\n");
	for (i = 0; i < (int) planes->count_planes; i++)
		LOGI("\t\t%d\n", planes->planes[i]);

	LOGI("\tCRTCs:\n");
	for (i = 0; i < resources->count_crtcs; i++)
		LOGI("\t\t%d\n", resources->crtcs[i]);

	LOGI("\tEncoders:\n");
	for (i = 0; i < resources->count_encoders; i++)
		LOGI("\t\t%d\n", resources->encoders[i]);

	LOGI("\tConnectors:\n");
	for (i = 0; i < resources->count_connectors; i++)
		LOGI("\t\t%d\n", resources->connectors[i]);

	drmModeFreeResources(resources);
	drmModeFreePlaneResources(planes);

	return 0;
}

static int
hwc_device_open(const struct hw_module_t* module, const char* name,
		struct hw_device_t** device)
{
	struct hwc_context_t *dev;
	int err = 0;

	if (strcmp(name, HWC_HARDWARE_COMPOSER))
		return -EINVAL;

	dev = (hwc_context_t*)calloc(1, sizeof(*dev));

	err = drm_open(dev);
	if (err) {
		free(dev);
		return err;
	}

	drm_list_kms(dev);

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;

        *device = &dev->device.common;

	return 0;
}
