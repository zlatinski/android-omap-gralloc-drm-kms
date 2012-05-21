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

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>

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

static void dump_bo(buffer_handle_t handle)
{
	struct gralloc_drm_bo_t *bo;
	struct gralloc_drm_handle_t *info;

	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return;

	LOGI("bo %p: GEM 0x%08X, FB 0x%08X\n",
	     handle, bo->fb_handle, bo->fb_id);

	info = bo->handle;
	LOGI("\t%4dx%4d (%4dx%4d), format %d, usage 0x%X\n",
	     info->width, info->height, info->stride, info->height,
	     info->format, info->usage);
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list)
{
	size_t i;

	if (!list)
		return 0;

	if (list->flags & HWC_GEOMETRY_CHANGED) {
		LOGI("%s:\n", __func__);

		for (i = 0; i < list->numHwLayers; i++) {
			dump_layer(&list->hwLayers[i]);
			dump_bo(list->hwLayers[i].handle);
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
	size_t i;

	LOGI("%s:\n", __func__);

	for (i = 0; i < list->numHwLayers; i++) {
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
drm_open(struct hwc_context_t *ctx)
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

	ctx->drm_fd = module->drm->fd;

	version = drmGetVersion(ctx->drm_fd);
	if (!version) {
		LOGE("Failed to get DRM Version\n");
		return -errno;
	}

	LOGI("Using DRM %s %s %s\n", version->name, version->date,
	     version->desc);
	drmFree(version);

	return 0;
}

static void
kms_plane_print(int fd, uint32_t id)
{
	drmModePlanePtr plane = drmModeGetPlane(fd, id);
	char buffer[1024];
	int i;

	if (!plane) {
		LOGE("Failed to get Plane %d: %s\n", id,
		     strerror(errno));
		return;
	}

	LOGI("\t\t%02d: FB %02d (%4dx%4d), CRTC %02d (%4dx%4d),"
	     " Possible CRTCs 0x%02X\n", plane->plane_id, plane->fb_id,
	     plane->crtc_x, plane->crtc_y, plane->crtc_id, plane->x, plane->y,
	     plane->possible_crtcs);
	for (i = 0; i < (int) plane->count_formats; i++)
		sprintf(buffer + 6 * i, " %c%c%c%c,", plane->formats[i] & 0xFF,
			(plane->formats[i] >> 8) & 0xFF,
			(plane->formats[i] >> 16) & 0xFF,
			(plane->formats[i] >> 24) & 0xFF);
	LOGI("\t\t   Supported Formats:%s", buffer);

	drmModeFreePlane(plane);
}

static int
drm_list_kms(struct hwc_context_t *ctx)
{
	drmModeResPtr resources;
	drmModePlaneResPtr planes;
	int i;

	resources = drmModeGetResources(ctx->drm_fd);
	if (!resources) {
		LOGE("Failed to get KMS resources\n");
		return -EINVAL;
	}

	planes = drmModeGetPlaneResources(ctx->drm_fd);
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
		kms_plane_print(ctx->drm_fd, planes->planes[i]);

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
	struct hwc_context_t *ctx;
	int err = 0;

	if (strcmp(name, HWC_HARDWARE_COMPOSER))
		return -EINVAL;

	ctx = calloc(1, sizeof(*ctx));

	err = drm_open(ctx);
	if (err) {
		free(ctx);
		return err;
	}

	drm_list_kms(ctx);

        /* initialize the procs */
        ctx->device.common.tag = HARDWARE_DEVICE_TAG;
        ctx->device.common.version = 0;
        ctx->device.common.module = (struct hw_module_t *) module;
        ctx->device.common.close = hwc_device_close;

        ctx->device.prepare = hwc_prepare;
        ctx->device.set = hwc_set;

        *device = &ctx->device.common;

	return 0;
}
