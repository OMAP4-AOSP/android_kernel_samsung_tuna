/*
 * ION Initialization for OMAP4.
 *
 * Copyright (C) 2011 Texas Instruments
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ion.h>
#include <linux/memblock.h>
#include <linux/omap_ion.h>
#include <linux/platform_device.h>
#ifdef CONFIG_CMA
#include <linux/dma-contiguous.h>
#endif
#include <plat/common.h>

#include <mach/omap4_ion.h>

/* Hardcoded Ducati heap address. */
#define ESPRESSO_DUCATI_HEAP_ADDR 0xbae00000
#define TUNA_DUCATI_HEAP_ADDR 0xba300000

/*
 * Carveouts from higher end of RAM
 *   - SMC
 *   - ION 1D
 *   - Ducati heap
 *   - Tiler 2D secure
 *   - Tiler non-secure
 */

static bool system_512m;

static phys_addr_t omap4_smc_addr;
static phys_addr_t omap4_ion_heap_secure_input_addr;
static phys_addr_t omap4_ion_heap_secure_output_wfdhdcp_addr;
static phys_addr_t omap4_ducati_heap_addr;
static phys_addr_t omap4_ion_heap_tiler_mem_addr;
static phys_addr_t omap4_ion_heap_nonsec_tiler_mem_addr;

static size_t omap4_smc_size;
static size_t omap4_ion_heap_secure_input_size;
static size_t omap4_ion_heap_secure_output_wfdhdcp_size;
static size_t omap4_ducati_heap_size;
static size_t omap4_ion_heap_tiler_mem_size;
static size_t omap4_ion_heap_nonsec_tiler_mem_size;

#ifdef CONFIG_CMA
static phys_addr_t omap4_ion_ipu_cma_addr;
static size_t omap4_ion_ipu_cma_pages_count;
static struct page* omap4_ion_ipu_cma_pages;

/* See RPMSG_IPC_MEM calculation in arch/arm/plat-omap/omap_rpmsg.c */
#define CMA_RPMSG_ADDR ((phys_addr_t)0xb3a00000)
#define CMA_RPMSG_SIZE ((size_t)0x8c000)

static phys_addr_t omap4_ion_rpmsg_cma_addr;
static size_t omap4_ion_rpmsg_cma_pages_count;
static struct page* omap4_ion_rpmsg_cma_pages;
#endif

static struct ion_platform_data omap4_ion_data = {
	.nr = 6,
	.heaps = {
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = OMAP_ION_HEAP_SECURE_INPUT,
			.name = "secure_input",
		},
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = OMAP_ION_HEAP_SECURE_OUTPUT_WFDHDCP,
			.name = "secure_output_wfdhdcp",
		},
		{	.type = OMAP_ION_HEAP_TYPE_TILER,
			.id = OMAP_ION_HEAP_TILER,
			.name = "tiler",
		},
		{
			.type = OMAP_ION_HEAP_TYPE_TILER,
			.id = OMAP_ION_HEAP_NONSECURE_TILER,
			.name = "nonsecure_tiler",
		},
		{
			.type = ION_HEAP_TYPE_SYSTEM,
			.id = OMAP_ION_HEAP_SYSTEM,
			.name = "system",
		},
		{
			.type = OMAP_ION_HEAP_TYPE_TILER_RESERVATION,
			.id = OMAP_ION_HEAP_TILER_RESERVATION,
			.name = "tiler_reservation",
		},
	},
};

static struct omap_ion_platform_data omap4_ion_pdata = {
	.ion = &omap4_ion_data,
};

static struct platform_device omap4_ion_device = {
	.name = "ion-omap4",
	.id = -1,
	.dev = {
		.platform_data = &omap4_ion_data,
	},
};

struct omap_ion_platform_data *get_omap_ion_platform_data(void)
{
	return &omap4_ion_pdata;
}

void __init omap4_register_ion(void)
{
	platform_device_register(&omap4_ion_device);
}

void __init omap_ion_init(void)
{
	int i;
	int ret;
#ifdef CONFIG_CMA
	size_t ipu_cma_pages_count;
	phys_addr_t cma_area_addr;
	size_t cma_area_size;
	const size_t cma_alignment = PAGE_SIZE << max(MAX_ORDER, pageblock_order);
#endif

	system_512m = (omap_total_ram_size() == SZ_512M);

	/* carveout sizes */
	omap4_smc_size = (SZ_1M * 3);

	if (system_512m) {
		omap4_ion_heap_secure_input_size = 0;
		omap4_ion_heap_secure_output_wfdhdcp_size = 0;
		omap4_ducati_heap_size = (SZ_1M * 83);
		omap4_ion_heap_nonsec_tiler_mem_size = 0;
		omap4_ion_heap_tiler_mem_size = 0;
	} else {
		omap4_ion_heap_secure_output_wfdhdcp_size =
				omap4_ion_pdata.secure_output_wfdhdcp_size;
		omap4_ducati_heap_size = omap4_ion_pdata.ducati_heap_size;
#ifdef CONFIG_ION_OMAP_TILER_DYNAMIC_ALLOC
		omap4_ion_heap_secure_input_size = 0;
		omap4_ion_heap_nonsec_tiler_mem_size = 0;
		omap4_ion_heap_tiler_mem_size = 0;
#else
		omap4_ion_heap_secure_input_size = omap4_ion_pdata.tiler1d_size;
		omap4_ion_heap_nonsec_tiler_mem_size =
				omap4_ion_pdata.nonsecure_tiler2d_size;
		omap4_ion_heap_tiler_mem_size = omap4_ion_pdata.tiler2d_size;
#endif
	}

	/* carveout addresses */
	omap4_smc_addr = PLAT_PHYS_OFFSET + omap_total_ram_size() -
				omap4_smc_size;
#ifdef CONFIG_MACH_TUNA /* fixed start address of ducati heap */
	omap4_ion_heap_secure_input_addr = TUNA_DUCATI_HEAP_ADDR;
#else
	omap4_ion_heap_secure_input_addr = omap4_smc_addr -
				omap4_ion_heap_secure_input_size;
#endif
	omap4_ion_heap_secure_output_wfdhdcp_addr =
				omap4_ion_heap_secure_input_addr -
				omap4_ion_heap_secure_output_wfdhdcp_size;
#ifdef CONFIG_MACH_OMAP4_ESPRESSO
	omap4_ducati_heap_addr = ESPRESSO_DUCATI_HEAP_ADDR;
#else
	omap4_ducati_heap_addr = omap4_ion_heap_secure_output_wfdhdcp_addr -
				omap4_ducati_heap_size;
#endif
	omap4_ion_heap_tiler_mem_addr = omap4_ducati_heap_addr -
				omap4_ion_heap_tiler_mem_size;
	omap4_ion_heap_nonsec_tiler_mem_addr = omap4_ion_heap_tiler_mem_addr -
				omap4_ion_heap_nonsec_tiler_mem_size;

	pr_info("omap4_total_ram_size = 0x%x\n" \
				"omap4_smc_size = 0x%x\n"  \
				"omap4_ion_heap_secure_input_size = 0x%x\n"  \
				"omap4_ion_heap_secure_output_wfdhdcp_size = 0x%x\n"  \
				"omap4_ducati_heap_size = 0x%x\n"  \
				"omap4_ion_heap_tiler_mem_size = 0x%x\n"  \
				"omap4_ion_heap_nonsec_tiler_mem_size  = 0x%x\n",
				omap_total_ram_size(),
				omap4_smc_size,
				omap4_ion_heap_secure_input_size,
				omap4_ion_heap_secure_output_wfdhdcp_size,
				omap4_ducati_heap_size,
				omap4_ion_heap_tiler_mem_size,
				omap4_ion_heap_nonsec_tiler_mem_size);

	pr_info(" omap4_smc_addr = 0x%x\n"  \
				"omap4_ion_heap_secure_input_addr = 0x%x\n"  \
				"omap4_ion_heap_secure_output_wfdhdcp_addr = 0x%x\n"  \
				"omap4_ducati_heap_addr = 0x%x\n"  \
				"omap4_ion_heap_tiler_mem_addr = 0x%x\n"  \
				"omap4_ion_heap_nonsec_tiler_mem_addr  = 0x%x\n",
				omap4_smc_addr,
				omap4_ion_heap_secure_input_addr,
				omap4_ion_heap_secure_output_wfdhdcp_addr,
				omap4_ducati_heap_addr,
				omap4_ion_heap_tiler_mem_addr,
				omap4_ion_heap_nonsec_tiler_mem_addr);

#ifdef CONFIG_CMA
	ipu_cma_pages_count = (omap4_ion_heap_secure_input_size +
				omap4_ion_heap_secure_output_wfdhdcp_size +
				omap4_ducati_heap_size +
				omap4_ion_heap_nonsec_tiler_mem_size +
				omap4_ion_heap_tiler_mem_size) / PAGE_SIZE;

	cma_area_addr = round_down(omap4_ion_heap_nonsec_tiler_mem_addr, cma_alignment);
	cma_area_size = round_up(ipu_cma_pages_count * PAGE_SIZE, cma_alignment);

	pr_info("Reserving CMA IPU + RPMSG region at address = 0x%x with size = 0x%x\n",
		cma_area_addr, cma_area_size);
	dma_declare_contiguous(&omap4_ion_device.dev, cma_area_size, cma_area_addr, 0);

	/* We need to separate RPMSG memory region from the overall Ducati range
	 * as it has to remain allocated even when the rest of Ducati is unloaded.
	 * Therefore, IPU carveout area is split into two pieces - below and above
	 * RPMSG region. */
	omap4_ion_ipu_cma_addr = CMA_RPMSG_ADDR + CMA_RPMSG_SIZE;
	omap4_ion_ipu_cma_pages_count = ipu_cma_pages_count - CMA_RPMSG_SIZE / PAGE_SIZE;

	omap4_ion_rpmsg_cma_addr = CMA_RPMSG_ADDR;
	omap4_ion_rpmsg_cma_pages_count = CMA_RPMSG_SIZE / PAGE_SIZE;

	pr_info("CMA RPMSG region: address = 0x%x, size = 0x%lx\n", omap4_ion_rpmsg_cma_addr, omap4_ion_rpmsg_cma_pages_count * PAGE_SIZE);
	pr_info("CMA IPU region: address = 0x%x, size = 0x%lx\n", omap4_ion_ipu_cma_addr, omap4_ion_ipu_cma_pages_count * PAGE_SIZE);

	omap4_ion_ipu_cma_pages = NULL;
	omap4_ion_rpmsg_cma_pages = NULL;
#endif

	for (i = 0; i < omap4_ion_data.nr; i++) {
		struct ion_platform_heap *h = &omap4_ion_data.heaps[i];

		switch (h->id) {
		case OMAP_ION_HEAP_SECURE_INPUT:
			h->base = omap4_ion_heap_secure_input_addr;
			h->size = omap4_ion_heap_secure_input_size;
			break;
		case OMAP_ION_HEAP_SECURE_OUTPUT_WFDHDCP:
			h->base = omap4_ion_heap_secure_output_wfdhdcp_addr;
			h->size = omap4_ion_heap_secure_output_wfdhdcp_size;
			break;
		case OMAP_ION_HEAP_NONSECURE_TILER:
			h->base = omap4_ion_heap_nonsec_tiler_mem_addr;
			h->size = omap4_ion_heap_nonsec_tiler_mem_size;
			break;
		case OMAP_ION_HEAP_TILER:
			h->base = omap4_ion_heap_tiler_mem_addr;
			h->size = omap4_ion_heap_tiler_mem_size;
			break;
		default:
			break;
		}
		pr_info("%s: %s id=%u [%lx-%lx] size=%x\n",
					__func__, h->name, h->id,
					h->base, h->base + h->size, h->size);
	}

	for (i = 0; i < omap4_ion_data.nr; i++)
		if (omap4_ion_data.heaps[i].type == ION_HEAP_TYPE_CARVEOUT ||
		    omap4_ion_data.heaps[i].type == OMAP_ION_HEAP_TYPE_TILER) {
			if (!omap4_ion_data.heaps[i].size)
				continue;
#ifndef CONFIG_CMA
			ret = memblock_remove(omap4_ion_data.heaps[i].base,
					      omap4_ion_data.heaps[i].size);
#endif
			if (omap4_ion_data.heaps[i].id ==
					OMAP_ION_HEAP_SECURE_OUTPUT_WFDHDCP) {
				/* Reducing the actual size being mapped for Ion/Ducati as
				 * secure component uses the remaining memory */
				omap4_ion_data.heaps[i].size =
					omap4_ion_heap_secure_output_wfdhdcp_size >> 1;
			}
			if (ret)
				pr_err("memblock remove of %x@%lx failed\n",
				       omap4_ion_data.heaps[i].size,
				       omap4_ion_data.heaps[i].base);
		}
}

phys_addr_t omap_smc_addr(void)
{
	return omap4_smc_addr;
}

phys_addr_t omap_ion_heap_secure_input_addr(void)
{
	return omap4_ion_heap_secure_input_addr;
}

phys_addr_t omap_ion_heap_secure_output_wfdhdcp_addr(void)
{
	return omap4_ion_heap_secure_output_wfdhdcp_addr;
}

phys_addr_t omap_ducati_heap_addr(void)
{
	return omap4_ducati_heap_addr;
}

phys_addr_t omap_ion_heap_tiler_mem_addr(void)
{
	return omap4_ion_heap_tiler_mem_addr;
}

phys_addr_t omap_ion_heap_nonsec_tiler_mem_addr(void)
{
	return omap4_ion_heap_nonsec_tiler_mem_addr;
}

size_t omap_smc_size(void)
{
	return omap4_smc_size;
}

size_t omap_ion_heap_secure_input_size(void)
{
	return omap4_ion_heap_secure_input_size;
}

size_t omap_ion_heap_secure_output_wfdhdcp_size(void)
{
	return omap4_ion_heap_secure_output_wfdhdcp_size;
}

size_t omap_ducati_heap_size(void)
{
	return omap4_ducati_heap_size;
}

size_t omap_ion_heap_tiler_mem_size(void)
{
	return omap4_ion_heap_tiler_mem_size;
}

size_t omap_ion_heap_nonsec_tiler_mem_size(void)
{
	return omap4_ion_heap_nonsec_tiler_mem_size;
}

#ifdef CONFIG_CMA
bool omap_ion_ipu_allocate_memory(void)
{
	if (omap4_ion_ipu_cma_pages) {
		pr_err("%s: CMA IPU pages are already allocated\n", __func__);
		return false;
	}

	omap4_ion_ipu_cma_pages = dma_alloc_from_contiguous_fixed_addr(&omap4_ion_device.dev,
				omap4_ion_ipu_cma_addr, omap4_ion_ipu_cma_pages_count);

	if (!omap4_ion_ipu_cma_pages) {
		pr_err("CMA IPU region pages allocation failed\n");
		return false;
	}

	return true;
}
EXPORT_SYMBOL(omap_ion_ipu_allocate_memory);

bool omap_ion_ipu_free_memory(void)
{
	if (!omap4_ion_ipu_cma_pages) {
		pr_err("%s: CMA IPU pages are not allocated\n", __func__);
		return false;
	}

	if (!dma_release_from_contiguous(&omap4_ion_device.dev, omap4_ion_ipu_cma_pages,
				omap4_ion_ipu_cma_pages_count)) {
		pr_err("CMA IPU region pages release failed\n");
		return false;
	}

	omap4_ion_ipu_cma_pages = NULL;

	return true;
}
EXPORT_SYMBOL(omap_ion_ipu_free_memory);

bool omap_ion_rpmsg_allocate_memory(void)
{
	if (omap4_ion_rpmsg_cma_pages) {
		pr_err("CMA RPMSG pages are already allocated\n");
		return false;
	}

	omap4_ion_rpmsg_cma_pages = dma_alloc_from_contiguous_fixed_addr(&omap4_ion_device.dev,
			omap4_ion_rpmsg_cma_addr, omap4_ion_rpmsg_cma_pages_count);
	if (!omap4_ion_rpmsg_cma_pages) {
		pr_err("CMA RPMSG region pages allocation failed\n");
		return false;
	}

	return true;
}
EXPORT_SYMBOL(omap_ion_rpmsg_allocate_memory);

bool omap_ion_rpmsg_free_memory(void)
{
	if (!omap4_ion_rpmsg_cma_pages) {
		pr_err("CMA RPMSG pages are not allocated\n");
		return false;
	}

	if (!dma_release_from_contiguous(&omap4_ion_device.dev, omap4_ion_rpmsg_cma_pages,
			omap4_ion_rpmsg_cma_pages_count)) {
		pr_err("CMA RPMSG region pages release failed\n");
		return false;
	}

	omap4_ion_rpmsg_cma_pages = NULL;

	return true;
}
EXPORT_SYMBOL(omap_ion_rpmsg_free_memory);
#endif
