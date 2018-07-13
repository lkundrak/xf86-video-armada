/*
 * Marvell Armada DRM-based driver
 *
 * Written by Russell King, 2012, derived in part from the
 * Intel xorg X server driver.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "xf86.h"

#include "compat-drm.h"
#include "common_drm.h"

static drmModePropertyPtr plane_hash_property(struct common_drm_info *drm,
	uint32_t prop_id, Bool create)
{
	void *property;

	if (drmHashLookup(drm->plane_property_hash, prop_id, &property)) {
		property = drmModeGetProperty(drm->fd, prop_id);
		if (!property)
			return NULL;

		drmHashInsert(drm->plane_property_hash, prop_id, property);
	}

	return property;
}

static Bool property_enum_val(drmModePropertyPtr prop, const char *name,
	uint64_t *val)
{
	unsigned int i;

	for (i = 0; i < prop->count_enums; i++)
		if (strcmp(prop->enums[i].name, name) == 0) {
			*val = prop->enums[i].value;
			return TRUE;
		}

	*val = ~0ULL;

	return FALSE;
}

static Bool plane_get_property_val(drmModeObjectPropertiesPtr mode_props,
	const drmModePropertyPtr property, uint64_t *val)
{
	unsigned int i;

	for (i = 0; i < mode_props->count_props; i++)
		if (mode_props->props[i] == property->prop_id) {
			*val = mode_props->prop_values[i];
			return TRUE;
		}

	*val = ~0ULL;

	return FALSE;
}

static drmModePropertyPtr plane_find_property(struct common_drm_info *drm,
	const char *name)
{
	drmModePropertyPtr property;
	unsigned long key;
	void *p;

	if (drmHashFirst(drm->plane_property_hash, &key, &p)) do {
		property = p;
		if (strcmp(property->name, name) == 0)
			return property;
	} while (drmHashNext(drm->plane_property_hash, &key, &p));

	return NULL;
}

static void plane_free_all(struct common_drm_plane *planes, unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (planes[i].mode_plane)
			drmModeFreePlane(planes[i].mode_plane);
		if (planes[i].mode_props)
			drmModeFreeObjectProperties(planes[i].mode_props);
	}

	free(planes);
}

static xf86CrtcPtr plane_get_crtc(ScrnInfoPtr pScrn, drmModePlanePtr mode_plane)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	struct common_crtc_info *drmc;
	unsigned int i;
	uint32_t crtcs = mode_plane->possible_crtcs;

	/* Must be a power of two */
	if (!crtcs || (crtcs & (crtcs - 1)) != 0)
		return NULL;

	for (i = 0; i < xf86_config->num_crtc; i++) {
		drmc = common_crtc(xf86_config->crtc[i]);
		if (crtcs & 1 << drmc->num)
			return xf86_config->crtc[i];
	}

	return NULL;
}

static int plane_get_all(ScrnInfoPtr pScrn, struct common_drm_plane **pplanes)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);
	drmModeObjectPropertiesPtr mode_props;
	drmModePlanePtr mode_plane;
	drmModePlaneResPtr res;
	struct common_drm_plane *planes;
	unsigned int i, j, num;

	res = drmModeGetPlaneResources(drm->fd);
	if (!res)
		return -1;

	*pplanes = planes = calloc(sizeof(*planes), res->count_planes);
	if (!*pplanes)
		goto err;

	for (i = num = 0; i < res->count_planes; i++) {
		mode_plane = drmModeGetPlane(drm->fd, res->planes[i]);
		if (!mode_plane)
			goto err;

		planes[num++].mode_plane = mode_plane;

		mode_props = drmModeObjectGetProperties(drm->fd, res->planes[i],
							DRM_MODE_OBJECT_PLANE);
		if (!mode_props)
			goto err;

		planes[num - 1].mode_props = mode_props;

		for (j = 0; j < mode_props->count_props; j++)
			if (!plane_hash_property(drm, mode_props->props[j],
						 TRUE))
				goto err;
	}

	drmModeFreePlaneResources(res);

	return num;

err:
	if (planes)
		plane_free_all(planes, num);

	drmModeFreePlaneResources(res);

	return -1;
}

static Bool plane_parse_types(ScrnInfoPtr pScrn,
	struct common_drm_plane **pplanes, int *pnum)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);
	struct common_drm_plane *overlays, *planes = *pplanes;
	drmModePropertyPtr type;
	uint64_t primary, overlay, val;
	unsigned int i, num_overlay, num = *pnum;
	xf86CrtcPtr crtc;

	type = plane_find_property(drm, "type");
	if (!type)
		return TRUE;

	if (!property_enum_val(type, "Primary", &primary) ||
	    !property_enum_val(type, "Overlay", &overlay))
		return TRUE;

	for (i = num_overlay = 0; i < num; i++)
		if (plane_get_property_val(planes[i].mode_props, type, &val) &&
		    val == overlay)
			num_overlay++;

	overlays = calloc(sizeof(*overlays), num_overlay);
	if (!overlays)
		return FALSE;

	for (i = num_overlay = 0; i < num; i++) {
		if (!plane_get_property_val(planes[i].mode_props, type, &val))
			continue;

		if (val == overlay) {
			overlays[num_overlay++] = planes[i];
			continue;
		}

		crtc = plane_get_crtc(pScrn, planes[i].mode_plane);
		if (crtc && val == primary)
			common_crtc(crtc)->primary_plane_id =
				planes[i].mode_plane->plane_id;

		drmModeFreePlane(planes[i].mode_plane);
		drmModeFreeObjectProperties(planes[i].mode_props);
	}

	free(planes);

	*pplanes = overlays;
	*pnum = num_overlay;

	return TRUE;
}

static Bool plane_universal_planes(ScrnInfoPtr pScrn, Bool enable)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);

	return drmSetClientCap(drm->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES,
			       enable) == 0;
}

drmModePropertyPtr common_drm_plane_get_property(ScrnInfoPtr pScrn,
	uint32_t prop_id)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);

	return plane_hash_property(drm, prop_id, FALSE);
}

void common_drm_cleanup_plane_resources(ScrnInfoPtr pScrn)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);
	struct common_crtc_info *drmc;
	unsigned int i;
	unsigned long key;
	void *val, *hash;

	for (i = 0; i < xf86_config->num_crtc; i++) {
		drmc = common_crtc(xf86_config->crtc[i]);
		drmc->primary_plane_id = 0;
	}

	if (drm->overlay_planes) {
		plane_free_all(drm->overlay_planes, drm->num_overlay_planes);
		drm->overlay_planes = NULL;
		drm->num_overlay_planes = 0;
	}

	if (drm->plane_property_hash) {
		hash = drm->plane_property_hash;

		if (drmHashFirst(hash, &key, &val)) do {
			drmModeFreeProperty(val);
		} while (drmHashNext(hash, &key, &val));

		drmHashDestroy(hash);
		drm->plane_property_hash = NULL;
	}

	drm->has_universal_planes = FALSE;
}

Bool common_drm_init_plane_resources(ScrnInfoPtr pScrn)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);
	struct common_drm_plane *planes;
	int num;

	drm->plane_property_hash = drmHashCreate();
	if (!drm->plane_property_hash)
		return FALSE;

	if (plane_universal_planes(pScrn, TRUE))
		drm->has_universal_planes = TRUE;

	num = plane_get_all(pScrn, &planes);
	if (num < 0) {
		common_drm_cleanup_plane_resources(pScrn);
		return FALSE;
	}

	if (drm->has_universal_planes &&
	    !plane_parse_types(pScrn, &planes, &num)) {
		common_drm_cleanup_plane_resources(pScrn);
		return FALSE;
	}

	drm->num_overlay_planes = num;
	drm->overlay_planes = planes;

	return TRUE;
}
