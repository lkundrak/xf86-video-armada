/*
 * Etnaviv fence support
 *
 * Written by Russell King.
 */
#ifndef ETNAVIV_FENCE_H
#define ETNAVIV_FENCE_H

#include <stdint.h>
#include "compat-list.h"

enum fence_state {
	B_NONE = 0,
	B_PENDING,
	B_FENCED,
};

struct etnaviv_fence_head {
	/* batch-queued fences */
	struct xorg_list batch_head;
	/* submitted fences */
	struct xorg_list fence_head;
};

struct etnaviv_fence {
	struct xorg_list node;
	uint32_t id;
	uint8_t state;
	void (*retire)(struct etnaviv_fence_head *fh, struct etnaviv_fence *f);
};

Bool etnaviv_fence_add(struct etnaviv_fence_head *fh, struct etnaviv_fence *f);
void etnaviv_fence_objects(struct etnaviv_fence_head *fh, uint32_t id);
uint32_t etnaviv_fence_retire_id(struct etnaviv_fence_head *fh, uint32_t id);
void etnaviv_fence_retire_all(struct etnaviv_fence_head *fh);
void etnaviv_fence_head_init(struct etnaviv_fence_head *fh);

static inline Bool etnaviv_fence_batch_pending(struct etnaviv_fence_head *fh)
{
	return !xorg_list_is_empty(&fh->batch_head);
}

static inline Bool etnaviv_fence_fences_pending(struct etnaviv_fence_head *fh)
{
	return !xorg_list_is_empty(&fh->fence_head);
}

#endif
