// SPDX-License-Identifier: GPL-2.0

#include <linux/mtd/spi-nor.h>
#include "core.h"

static const struct flash_info id_collision_parts[] = {
	/* Boya */
	{ "by25q128as", INFO(0x684018, 0, 64 * 1024, 256, SPI_NOR_SKIP_SFDP |
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },

	/* XTX (XTX Technology Limited) */
	{ "xt25f128b", INFO(0x0b4018, 0, 64 * 1024, 256, SPI_NOR_PARSE_SFDP |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
};

const struct spi_nor_manufacturer spi_nor_manuf_id_collisions = {
	.name = "manufacturer ID collisions",
	.parts = id_collision_parts,
	.nparts = ARRAY_SIZE(id_collision_parts),
};
