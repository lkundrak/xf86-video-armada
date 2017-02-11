/*
 * Vivante GPU Acceleration Xorg driver
 *
 * Written by Russell King, 2012, derived in part from the
 * Intel xorg X server driver.
 */
#ifndef ETNAVIV_COMPAT_H
#define ETNAVIV_COMPAT_H

/*
 * Etnaviv itself does not provide these functions.  We'd like these
 * to be named this way, but some incompatible etnaviv functions clash.
 */
#define etna_bo_from_name my_etna_bo_from_name
struct etna_bo *etna_bo_from_name(struct viv_conn *conn, uint32_t name);
#define etna_bo_to_dmabuf my_etna_bo_to_dmabuf
int etna_bo_to_dmabuf(struct viv_conn *conn, struct etna_bo *bo);
#define etna_bo_flink my_etna_bo_flink
int etna_bo_flink(struct etna_bo *bo, uint32_t *name);

#endif
