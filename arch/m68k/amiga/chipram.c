/*
**  linux/amiga/chipram.c
**
**      Modified 03-May-94 by Geert Uytterhoeven <geert@linux-m68k.org>
**          - 64-bit aligned allocations for full AGA compatibility
**
**	Rewritten 15/9/2000 by Geert to use resource management
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/module.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/amigahw.h>

unsigned long amiga_chip_size;
EXPORT_SYMBOL(amiga_chip_size);

static struct resource chipram_res = {
	.name = "Chip RAM", .start = CHIP_PHYSADDR
};
static atomic_t chipavail;


void __init amiga_chip_init(void)
{
	if (!AMIGAHW_PRESENT(CHIP_RAM))
		return;

	chipram_res.end = amiga_chip_size-1;
	request_resource(&iomem_resource, &chipram_res);

	atomic_set(&chipavail, amiga_chip_size);
}


void *amiga_chip_alloc(unsigned long size, const char *name)
{
	struct resource *res;
	void *p;

	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!res)
		return NULL;

	res->name = name;
	p = amiga_chip_alloc_res(size, res);
	if (!p) {
		kfree(res);
		return NULL;
	}

	return p;
}
EXPORT_SYMBOL(amiga_chip_alloc);


	/*
	 *  Warning:
	 *  amiga_chip_alloc_res is meant only for drivers that need to
	 *  allocate Chip RAM before kmalloc() is functional. As a consequence,
	 *  those drivers must not free that Chip RAM afterwards.
	 */

void *amiga_chip_alloc_res(unsigned long size, struct resource *res)
{
	int error;

	/* round up */
	size = PAGE_ALIGN(size);

	pr_debug("amiga_chip_alloc_res: allocate %lu bytes\n", size);
	error = allocate_resource(&chipram_res, res, size, 0, UINT_MAX,
				  PAGE_SIZE, NULL, NULL);
	if (error < 0) {
		pr_err("amiga_chip_alloc_res: allocate_resource() failed %d!\n",
		       error);
		return NULL;
	}

	atomic_sub(size, &chipavail);
	pr_debug("amiga_chip_alloc_res: returning %pR\n", res);
	return (void *)ZTWO_VADDR(res->start);
}

void amiga_chip_free(void *ptr)
{
	unsigned long start = ZTWO_PADDR(ptr);
	struct resource **p, *res;
	unsigned long size;

	for (p = &chipram_res.child; (res = *p); p = &res->sibling) {
		if (res->start != start)
			continue;
		*p = res->sibling;
		size = res->end-start;
		pr_debug("amiga_chip_free: free %lu bytes at %p\n", size, ptr);
		atomic_add(size, &chipavail);
		kfree(res);
		return;
	}
	pr_err("amiga_chip_free: trying to free nonexistent region at %p\n",
	       ptr);
}
EXPORT_SYMBOL(amiga_chip_free);


unsigned long amiga_chip_avail(void)
{
	unsigned long n = atomic_read(&chipavail);

	pr_debug("amiga_chip_avail : %lu bytes\n", n);
	return n;
}
EXPORT_SYMBOL(amiga_chip_avail);

