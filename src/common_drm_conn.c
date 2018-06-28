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

#include "utils.h"

#include "common_drm.h"
#include "common_drm_conn.h"
#include "xf86Crtc.h"
#include <xf86DDC.h>
#include <X11/extensions/dpmsconst.h>
#include <X11/Xatom.h>

struct common_drm_property {
	drmModePropertyPtr mode_prop;
	int natoms;
	Atom *atoms;
};

struct common_conn_info {
	int drm_fd;
	int drm_id;
	int dpms_mode;
	int nprops;
	struct common_drm_property *props;
	drmModeConnectorPtr mode_output;
	drmModeEncoderPtr mode_encoder;
	drmModePropertyPtr dpms;
	drmModePropertyPtr edid;
};

static void drmmode_ConvertFromKMode(ScrnInfoPtr pScrn,
	drmModeModeInfoPtr kmode, DisplayModePtr mode)
{
	memset(mode, 0, sizeof(*mode));

	mode->status = MODE_OK;
	mode->Clock = kmode->clock;
	mode->HDisplay = kmode->hdisplay;
	mode->HSyncStart = kmode->hsync_start;
	mode->HSyncEnd = kmode->hsync_end;
	mode->HTotal = kmode->htotal;
	mode->HSkew = kmode->hskew;
	mode->VDisplay = kmode->vdisplay;
	mode->VSyncStart = kmode->vsync_start;
	mode->VSyncEnd = kmode->vsync_end;
	mode->VTotal = kmode->vtotal;
	mode->VScan = kmode->vscan;
	mode->Flags = kmode->flags;
	mode->name = strdup(kmode->name);
	if (kmode->type & DRM_MODE_TYPE_DRIVER)
		mode->type = M_T_DRIVER;
	if (kmode->type & DRM_MODE_TYPE_PREFERRED)
		mode->type |= M_T_PREFERRED;
	xf86SetModeCrtc (mode, pScrn->adjustFlags);
}

static Bool mode_output_find_prop_value(drmModeConnectorPtr koutput,
	uint32_t prop_id, uint64_t *value)
{
	int i;

	for (i = 0; i < koutput->count_props; i++) {
		if (koutput->props[i] == prop_id) {
			*value = koutput->prop_values[i];
			return TRUE;
		}
	}
	return FALSE;
}

static void common_drm_conn_create_resources(xf86OutputPtr output)
{
	struct common_conn_info *conn = output->driver_private;
	int i, j, err;

	for (i = 0; i < conn->nprops; i++) {
		struct common_drm_property *p = &conn->props[i];
		drmModePropertyPtr prop = p->mode_prop;
		uint64_t value;
		Bool immutable;

		if (!mode_output_find_prop_value(conn->mode_output,
						 prop->prop_id, &value))
			continue;

		immutable = !!(prop->flags & DRM_MODE_PROP_IMMUTABLE);

		if (prop->flags & DRM_MODE_PROP_RANGE) {
			INT32 range[2];
			uint32_t val = value;

			p->natoms = 1;
			p->atoms = calloc(p->natoms, sizeof *p->atoms);
			if (!p->atoms)
				continue;

			range[0] = prop->values[0];
			range[1] = prop->values[1];

			p->atoms[0] = MakeAtom(prop->name, strlen(prop->name),
					       TRUE);
			err = RRConfigureOutputProperty(output->randr_output,
							p->atoms[0], FALSE,
							TRUE, immutable, 2,
							range);
			if (err)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRConfigureOutputProperty error %d\n",
					   err);

			err = RRChangeOutputProperty(output->randr_output,
						     p->atoms[0],
						     XA_INTEGER, 32,
					             PropModeReplace, 1,
						     &val, FALSE, TRUE);
			if (err)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRChangeOutputProperty error %d\n",
					   err);
		} else if (prop->flags & DRM_MODE_PROP_ENUM) {
			int current;

			p->natoms = prop->count_enums + 1;
			p->atoms = calloc(p->natoms, sizeof *p->atoms);
			if (!p->atoms)
				continue;

			current = p->natoms;
			p->atoms[0] = MakeAtom(prop->name, strlen(prop->name),
					       TRUE);
			for (j = 1; j < p->natoms; j++) {
				struct drm_mode_property_enum *e;

				e = &prop->enums[j - 1];
				p->atoms[j] = MakeAtom(e->name, strlen(e->name),
						       TRUE);
				if (value == e->value)
					current = j;
			}

			err = RRConfigureOutputProperty(output->randr_output,
						 p->atoms[0], FALSE, FALSE,
						 immutable, p->natoms - 1,
						 (INT32 *)&p->atoms[1]);
			if (err)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRConfigureOutputProperty error, %d\n",
					   err);

			err = RRChangeOutputProperty(output->randr_output,
						     p->atoms[0], XA_ATOM,
						     32, PropModeReplace, 1,
						     &p->atoms[current],
						     FALSE, TRUE);
			if (err)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
					   "RRChangeOutputProperty error, %d\n",
					   err);
		}
	}
}

static void common_drm_conn_dpms(xf86OutputPtr output, int mode)
{
	struct common_conn_info *conn = output->driver_private;

	if (conn->dpms) {
		drmModeConnectorSetProperty(conn->drm_fd, conn->drm_id,
					    conn->dpms->prop_id, mode);
		conn->dpms_mode = mode;
	}
}

static xf86OutputStatus common_drm_conn_detect(xf86OutputPtr output)
{
	struct common_conn_info *conn = output->driver_private;
	xf86OutputStatus status = XF86OutputStatusUnknown;
	drmModeConnectorPtr koutput;

	koutput = drmModeGetConnector(conn->drm_fd, conn->drm_id);
	if (!koutput)
		return XF86OutputStatusUnknown;

	drmModeFreeConnector(conn->mode_output);
	conn->mode_output = koutput;

	switch (koutput->connection) {
	case DRM_MODE_CONNECTED:
		status = XF86OutputStatusConnected;
		break;
	case DRM_MODE_DISCONNECTED:
		status = XF86OutputStatusDisconnected;
		break;
	case DRM_MODE_UNKNOWNCONNECTION:
		break;
	}
	return status;
}

static Bool
common_drm_conn_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
	return MODE_OK;
}

static DisplayModePtr common_drm_conn_get_modes(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	struct common_conn_info *conn = output->driver_private;
	drmModePropertyBlobPtr edid = NULL;
	DisplayModePtr modes = NULL;
	xf86MonPtr mon;
	uint64_t blob;
	int i;

	if (mode_output_find_prop_value(conn->mode_output,
					conn->edid->prop_id, &blob))
		edid = drmModeGetPropertyBlob(conn->drm_fd, blob);

	mon = xf86InterpretEDID(pScrn->scrnIndex, edid ? edid->data : NULL);
	if (mon && edid->length > 128)
		mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
	xf86OutputSetEDID(output, mon);

	drmModeFreePropertyBlob(edid);

	/* modes should already be available */
	for (i = 0; i < conn->mode_output->count_modes; i++) {
		DisplayModePtr mode = xnfalloc(sizeof *mode);

		drmmode_ConvertFromKMode(pScrn, &conn->mode_output->modes[i], mode);
		modes = xf86ModesAdd(modes, mode);
	}

	return modes;
}

#ifdef RANDR_12_INTERFACE
static struct common_drm_property *common_drm_conn_find_prop(
	struct common_conn_info *conn, Atom property)
{
	int i;

	for (i = 0; i < conn->nprops; i++) {
		struct common_drm_property *p = &conn->props[i];

		if (p->atoms && p->atoms[0] == property)
			return p;
	}
	return NULL;
}

static Bool common_drm_conn_set_property(xf86OutputPtr output, Atom property,
	RRPropertyValuePtr value)
{
	struct common_conn_info *conn = output->driver_private;
	struct common_drm_property *prop;
	drmModePropertyPtr dprop;
	uint64_t val;

	prop = common_drm_conn_find_prop(conn, property);
	/* If we didn't recognise this property, just report success
	 * in order to allow the set to continue, otherwise we break
	 * setting of common properties like EDID.
	 */
	if (!prop)
		return TRUE;

	dprop = prop->mode_prop;
	if (dprop->flags & DRM_MODE_PROP_RANGE) {
		if (value->type != XA_INTEGER ||
		    value->format != 32 ||
		    value->size != 1)
			return FALSE;

		val = *(uint32_t *)value->data;
		drmModeConnectorSetProperty(conn->drm_fd, conn->drm_id,
					    dprop->prop_id, val);

		return TRUE;
	} else if (dprop->flags & DRM_MODE_PROP_ENUM) {
		Atom atom;
		const char *name;
		int j;

		if (value->type != XA_ATOM ||
		    value->format != 32 ||
		    value->size != 1)
			return FALSE;

		memcpy(&atom, value->data, sizeof(atom));
		name = NameForAtom(atom);
		if (name == NULL)
			return FALSE;

		for (j = 0; j < dprop->count_enums; j++) {
			if (!strcmp(dprop->enums[j].name, name)) {
				val = dprop->enums[j].value;
				break;
			}
		}

		if (j >= dprop->count_enums)
			return FALSE;

		drmModeConnectorSetProperty(conn->drm_fd, conn->drm_id,
					    dprop->prop_id, val);

		return TRUE;
	}
	return TRUE;
}
#endif
#ifdef RANDR_13_INTERFACE
static Bool common_drm_conn_get_property(xf86OutputPtr output, Atom property)
{
	return FALSE;
}
#endif

static void common_drm_conn_destroy(xf86OutputPtr output)
{
	struct common_conn_info *conn = output->driver_private;
	int i;

	if (conn) {
		if (conn->props) {
			for (i = 0; i < conn->nprops; i++) {
				if (conn->props[i].atoms)
					free(conn->props[i].atoms);
				drmModeFreeProperty(conn->props[i].mode_prop);
			}
			free(conn->props);
		}
		drmModeFreeProperty(conn->edid);
		drmModeFreeProperty(conn->dpms);
		drmModeFreeConnector(conn->mode_output);
		drmModeFreeEncoder(conn->mode_encoder);
		free(conn);
	}

	output->driver_private = NULL;
}

static const xf86OutputFuncsRec drm_output_funcs = {
	.create_resources = common_drm_conn_create_resources,
	.dpms = common_drm_conn_dpms,
	.detect = common_drm_conn_detect,
	.mode_valid = common_drm_conn_mode_valid,
	.get_modes = common_drm_conn_get_modes,
#ifdef RANDR_12_INTERFACE
	.set_property = common_drm_conn_set_property,
#endif
#ifdef RANDR_13_INTERFACE
	.get_property = common_drm_conn_get_property,
#endif
	.destroy = common_drm_conn_destroy,
};

/* Convert libdrm's connector type to Xorg name */
static const char *const output_names[] = {
	[DRM_MODE_CONNECTOR_Unknown]     = "None",
	[DRM_MODE_CONNECTOR_VGA]         = "VGA",
	[DRM_MODE_CONNECTOR_DVII]        = "DVI",
	[DRM_MODE_CONNECTOR_DVID]        = "DVI",
	[DRM_MODE_CONNECTOR_DVIA]        = "DVI",
	[DRM_MODE_CONNECTOR_Composite]   = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO]      = "TV",
	[DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
	[DRM_MODE_CONNECTOR_Component]   = "CTV",
	[DRM_MODE_CONNECTOR_9PinDIN]     = "DIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DP",
	[DRM_MODE_CONNECTOR_HDMIA]       = "HDMI",
	[DRM_MODE_CONNECTOR_HDMIB]       = "HDMI",
};

static const char *common_drm_output_name(uint32_t type)
{
	if (type >= ARRAY_SIZE(output_names))
		type = DRM_MODE_CONNECTOR_Unknown;

	return output_names[type];
}

/* Convert libdrm's subpixel order to Xorg subpixel */
static const int subpixel_conv_table[] = {
	[DRM_MODE_SUBPIXEL_UNKNOWN]        = SubPixelUnknown,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_RGB] = SubPixelHorizontalRGB,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_BGR] = SubPixelHorizontalBGR,
	[DRM_MODE_SUBPIXEL_VERTICAL_RGB]   = SubPixelVerticalRGB,
	[DRM_MODE_SUBPIXEL_VERTICAL_BGR]   = SubPixelVerticalBGR,
	[DRM_MODE_SUBPIXEL_NONE]           = SubPixelNone,
};

static int common_drm_subpixel(drmModeSubPixel k)
{
	if (k >= ARRAY_SIZE(subpixel_conv_table))
		k = DRM_MODE_SUBPIXEL_UNKNOWN;

	return subpixel_conv_table[k];
}

void common_drm_conn_init(ScrnInfoPtr pScrn, uint32_t id)
{
	struct common_drm_info *drm = GET_DRM_INFO(pScrn);
	drmModeConnectorPtr koutput;
	drmModeEncoderPtr kencoder;
	drmModePropertyPtr prop;
	xf86OutputPtr output;
	struct common_conn_info *conn;
	char name[32];
	int i;

	koutput = drmModeGetConnector(drm->fd, id);
	if (!koutput)
		return;

	kencoder = drmModeGetEncoder(drm->fd, koutput->encoders[0]);
	if (!kencoder) {
		drmModeFreeConnector(koutput);
		return;
	}

	snprintf(name, sizeof(name), "%s%d",
		 common_drm_output_name(koutput->connector_type),
		 koutput->connector_type_id);

	output = xf86OutputCreate(pScrn, &drm_output_funcs, name);
	if (!output) {
		drmModeFreeEncoder(kencoder);
		drmModeFreeConnector(koutput);
		return;
	}

	conn = calloc(1, sizeof *conn);
	if (!conn) {
		xf86OutputDestroy(output);
		drmModeFreeEncoder(kencoder);
		drmModeFreeConnector(koutput);
		return;
	}

	conn->drm_fd = drm->fd;
	conn->drm_id = id;
	conn->mode_output = koutput;
	conn->mode_encoder = kencoder;

	output->driver_private = conn;
	output->mm_width = koutput->mmWidth;
	output->mm_height = koutput->mmHeight;
	output->subpixel_order = common_drm_subpixel(koutput->subpixel);
	output->possible_crtcs = kencoder->possible_crtcs;
	output->possible_clones = kencoder->possible_clones;
	output->interlaceAllowed = 1; /* wish there was a way to read that */
	output->doubleScanAllowed = 0;

	conn->props = calloc(koutput->count_props, sizeof *conn->props);
	if (!conn->props) {
		xf86OutputDestroy(output);
		return;
	}

	/* Lookup and save the DPMS and EDID properies */
	for (i = 0; i < koutput->count_props; i++) {
		prop = drmModeGetProperty(conn->drm_fd, koutput->props[i]);
		if (!prop)
			continue;

		if (!strcmp(prop->name, "DPMS")) {
			if (prop->flags & DRM_MODE_PROP_ENUM) {
				conn->dpms = prop;
				prop = NULL;
			}
		} else if (!strcmp(prop->name, "EDID")) {
			if (prop->flags & DRM_MODE_PROP_BLOB) {
				conn->edid = prop;
				prop = NULL;
			}
		} else if (prop->flags & (DRM_MODE_PROP_RANGE |
					  DRM_MODE_PROP_ENUM)) {
			conn->props[conn->nprops].mode_prop = prop;
			conn->nprops++;
			prop = NULL;
		}
		drmModeFreeProperty(prop);
	}
}

uint32_t common_drm_conn_output_ids(xf86CrtcPtr crtc, uint32_t *ids)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	struct common_conn_info *conn;
	uint32_t n;
	int i;

	for (n = i = 0; i < xf86_config->num_output; i++) {
		if (xf86_config->output[i]->crtc == crtc) {
			conn = xf86_config->output[i]->driver_private;
			ids[n++] = conn->drm_id;
		}
	}

	return n;
}
