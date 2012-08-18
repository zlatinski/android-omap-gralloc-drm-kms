/*
 * Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2010-2011 LunarG Inc.
 *
 * drm_gem_intel_copy is based on xorg-driver-intel, which has
 *
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * Copyright (c) 2005 Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define LOG_TAG "HWDRM-OMAP"

#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <drm.h>
#include <omap_drmif.h>

#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"

struct omap_info {
	struct gralloc_drm_drv_t base;
	int fd;
	struct omap_device *dev;
};

struct omap_buffer {
	struct gralloc_drm_bo_t base;
	struct omap_bo *bo;
};

static void omap_copy(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *dst,
		struct gralloc_drm_bo_t *src,
		short x1, short y1, short x2, short y2)
{
	struct omap_info *info = (struct omap_info *) drv;

	LOGE("%s needs implementation %p",
			__func__, info->dev);
}

static struct gralloc_drm_bo_t *omap_alloc(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_handle_t *handle)
{
	struct omap_info *info = (struct omap_info *) drv;
	struct omap_buffer *bo;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return NULL;

	if (handle->name) {

		bo->bo = omap_bo_from_name(info->dev, handle->name);
		if (!bo->bo) {
			LOGE("failed to create bo from name %u",
					handle->name);
			free(bo);
			return NULL;
		}
	}
	else {
		unsigned long stride = 0;

		bo->bo = omap_bo_new(info->dev, 0, 0);
		if (!bo->bo) {
			LOGE("failed to allocate bo %dx%d (format %d)",
					handle->width,
					handle->height,
					handle->format);
			free(bo);
			return NULL;
		}

		handle->stride = stride;
	}

	if (handle->usage & GRALLOC_USAGE_HW_FB)
		bo->base.fb_handle = omap_bo_handle(bo->bo);

	bo->base.handle = handle;

	return &bo->base;
}

static void omap_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct omap_buffer *omap_bo = (struct omap_buffer *) bo;

	omap_bo_del(omap_bo->bo);
	free(omap_bo);
}

static int omap_map(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo,
		int x, int y, int w, int h,
		int enable_write, void **addr)
{
	struct omap_buffer *omap_bo = (struct omap_buffer *) bo;
	int err = 0;

	*addr = omap_bo_map(omap_bo->bo);

	if(!*addr)
		err = -1;

	return err;
}

static void omap_unmap(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct omap_buffer *omap_bo = (struct omap_buffer *) bo;

	LOGE("%s needs implementation %p",
			__func__, omap_bo->bo);
}

static void omap_init_kms_features(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_t *drm)
{
	struct omap_info *info = (struct omap_info *) drv;

	switch (drm->fb_format) {
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_RGB_565:
		break;
	default:
		drm->fb_format = HAL_PIXEL_FORMAT_BGRA_8888;
		break;
	}

	drm->mode_sync_flip = 1;

	drm->swap_mode = DRM_SWAP_FLIP;

	drm->swap_interval = 0;

	drm->vblank_secondary = 0;

	LOGW("%s needs implementation %p",
			__func__, info->dev);

}

static void omap_destroy(struct gralloc_drm_drv_t *drv)
{
	struct omap_info *info = (struct omap_info *) drv;

	free(info);
}

struct gralloc_drm_drv_t *gralloc_drm_drv_create_for_omap(int fd)
{
	struct omap_info *info;

	info = calloc(1, sizeof(*info));
	if (!info) {
		LOGE("failed to allocate driver info for omap");
		return NULL;
	}

	info->fd = fd;

	info->base.destroy = omap_destroy;
	info->base.init_kms_features = omap_init_kms_features;
	info->base.alloc = omap_alloc;
	info->base.free = omap_free;
	info->base.map = omap_map;
	info->base.unmap = omap_unmap;
	info->base.copy = omap_copy;

	return &info->base;
}
