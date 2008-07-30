#ifndef ASM_X86__AGP_H
#define ASM_X86__AGP_H

#include <asm/pgtable.h>
#include <asm/cacheflush.h>

/*
 * Functions to keep the agpgart mappings coherent with the MMU. The
 * GART gives the CPU a physical alias of pages in memory. The alias
 * region is mapped uncacheable. Make sure there are no conflicting
 * mappings with different cachability attributes for the same
 * page. This avoids data corruption on some CPUs.
 */

#define map_page_into_agp(page) set_pages_uc(page, 1)
#define unmap_page_from_agp(page) set_pages_wb(page, 1)

/*
 * Could use CLFLUSH here if the cpu supports it. But then it would
 * need to be called for each cacheline of the whole page so it may
 * not be worth it. Would need a page for it.
 */
#define flush_agp_cache() wbinvd()

/* Convert a physical address to an address suitable for the GART. */
#define phys_to_gart(x) (x)
#define gart_to_phys(x) (x)

/* GATT allocation. Returns/accepts GATT kernel virtual address. */
#define alloc_gatt_pages(order)		\
	((char *)__get_free_pages(GFP_KERNEL, (order)))
#define free_gatt_pages(table, order)	\
	free_pages((unsigned long)(table), (order))

#endif /* ASM_X86__AGP_H */
