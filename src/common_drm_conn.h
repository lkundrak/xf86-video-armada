#ifndef COMMON_DRM_CONN_H
#define COMMON_DRM_CONN_H

#include <stdint.h>
#include "xf86.h"

void common_drm_conn_init(ScrnInfoPtr pScrn, uint32_t id);
uint32_t common_drm_conn_output_ids(xf86CrtcPtr crtc, uint32_t *ids);

#endif
