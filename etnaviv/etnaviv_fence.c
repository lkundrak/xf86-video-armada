/*
 * Etnaviv fence support
 *
 * Written by Russell King.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <xf86.h>

#include <etnaviv/viv.h>
#include "etnaviv_fence.h"

static void etnaviv_fence_retire(struct etnaviv_fence_head *fh,
	struct etnaviv_fence *f)
{
	xorg_list_del(&f->node);
	f->state = B_NONE;
	f->retire(fh, f);
}

Bool etnaviv_fence_add(struct etnaviv_fence_head *fh, struct etnaviv_fence *f)
{
	Bool was_idle = f->state == B_NONE;

	switch (f->state) {
	case B_PENDING:
		break;
	case B_FENCED:
		xorg_list_del(&f->node);
	case B_NONE:
		xorg_list_append(&f->node, &fh->batch_head);
		f->state = B_PENDING;
		break;
	}

	return was_idle;
}

void etnaviv_fence_objects(struct etnaviv_fence_head *fh, uint32_t id)
{
	struct etnaviv_fence *f, *n;

	xorg_list_for_each_entry_safe(f, n, &fh->batch_head, node) {
		xorg_list_del(&f->node);
		xorg_list_append(&f->node, &fh->fence_head);
		f->state = B_FENCED;
		f->id = id;
	}
}

uint32_t etnaviv_fence_retire_id(struct etnaviv_fence_head *fh, uint32_t id)
{
	struct etnaviv_fence *f, *n;
	uint32_t last = id;

	xorg_list_for_each_entry_safe(f, n, &fh->fence_head, node) {
		assert(f->state == B_FENCED);

		if (VIV_FENCE_BEFORE_EQ(f->id, id)) {
			etnaviv_fence_retire(fh, f);
		} else {
			last = f->id;
			break;
		}
	}

	return last;
}

void etnaviv_fence_retire_all(struct etnaviv_fence_head *fh)
{
	struct etnaviv_fence *f, *n;

	xorg_list_for_each_entry_safe(f, n, &fh->batch_head, node)
		etnaviv_fence_retire(fh, f);
	xorg_list_for_each_entry_safe(f, n, &fh->fence_head, node)
		etnaviv_fence_retire(fh, f);
}

void etnaviv_fence_head_init(struct etnaviv_fence_head *fh)
{
	xorg_list_init(&fh->batch_head);
	xorg_list_init(&fh->fence_head);
}
