#define pr_fmt(fmt)  "irq: " fmt

#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/topology.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/fs.h>

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);

static DEFINE_MUTEX(revmap_trees_mutex);
static struct irq_domain *irq_default_domain;

/**
 * __irq_domain_add() - Allocate a new irq_domain data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @size: Size of linear map; 0 for radix mapping only
 * @direct_max: Maximum value of direct maps; Use ~0 for no limit; 0 for no
 *              direct mapping
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Allocates and initialize and irq_domain structure.  Caller is expected to
 * register allocated irq_domain with irq_domain_register().  Returns pointer
 * to IRQ domain, or NULL on failure.
 */
struct irq_domain *__irq_domain_add(struct device_node *of_node,
				    int size, int direct_max,
				    const struct irq_domain_ops *ops,
				    void *host_data)
{
	struct irq_domain *domain;

	domain = kzalloc_node(sizeof(*domain) + (sizeof(unsigned int) * size),
			      GFP_KERNEL, of_node_to_nid(of_node));
	if (WARN_ON(!domain))
		return NULL;

	/* Fill structure */
	INIT_RADIX_TREE(&domain->revmap_tree, GFP_KERNEL);
	domain->ops = ops;
	domain->host_data = host_data;
	domain->of_node = of_node_get(of_node);
	domain->revmap_size = size;
	domain->revmap_direct_max_irq = direct_max;

	mutex_lock(&irq_domain_mutex);
	list_add(&domain->link, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);

	pr_debug("Added domain %s\n", domain->name);
	return domain;
}
EXPORT_SYMBOL_GPL(__irq_domain_add);

/**
 * irq_domain_remove() - Remove an irq domain.
 * @domain: domain to remove
 *
 * This routine is used to remove an irq domain. The caller must ensure
 * that all mappings within the domain have been disposed of prior to
 * use, depending on the revmap type.
 */
void irq_domain_remove(struct irq_domain *domain)
{
	mutex_lock(&irq_domain_mutex);

	/*
	 * radix_tree_delete() takes care of destroying the root
	 * node when all entries are removed. Shout if there are
	 * any mappings left.
	 */
	WARN_ON(domain->revmap_tree.height);

	list_del(&domain->link);

	/*
	 * If the going away domain is the default one, reset it.
	 */
	if (unlikely(irq_default_domain == domain))
		irq_set_default_host(NULL);

	mutex_unlock(&irq_domain_mutex);

	pr_debug("Removed domain %s\n", domain->name);

	of_node_put(domain->of_node);
	kfree(domain);
}
EXPORT_SYMBOL_GPL(irq_domain_remove);

/**
 * irq_domain_add_simple() - Register an irq_domain and optionally map a range of irqs
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: total number of irqs in mapping
 * @first_irq: first number of irq block assigned to the domain,
 *	pass zero to assign irqs on-the-fly. If first_irq is non-zero, then
 *	pre-map all of the irqs in the domain to virqs starting at first_irq.
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Allocates an irq_domain, and optionally if first_irq is positive then also
 * allocate irq_descs and map all of the hwirqs to virqs starting at first_irq.
 *
 * This is intended to implement the expected behaviour for most
 * interrupt controllers. If device tree is used, then first_irq will be 0 and
 * irqs get mapped dynamically on the fly. However, if the controller requires
 * static virq assignments (non-DT boot) then it will set that up correctly.
 */
struct irq_domain *irq_domain_add_simple(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain;

	domain = __irq_domain_add(of_node, size, 0, ops, host_data);
	if (!domain)
		return NULL;

	if (first_irq > 0) {
		if (IS_ENABLED(CONFIG_SPARSE_IRQ)) {
			/* attempt to allocated irq_descs */
			int rc = irq_alloc_descs(first_irq, first_irq, size,
						 of_node_to_nid(of_node));
			if (rc < 0)
				pr_info("Cannot allocate irq_descs @ IRQ%d, assuming pre-allocated\n",
					first_irq);
		}
		WARN_ON(irq_domain_associate_many(domain, first_irq, 0, size));
	}

	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_simple);

/**
 * irq_domain_add_legacy() - Allocate and register a legacy revmap irq_domain.
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: total number of irqs in legacy mapping
 * @first_irq: first number of irq block assigned to the domain
 * @first_hwirq: first hwirq number to use for the translation. Should normally
 *               be '0', but a positive integer can be used if the effective
 *               hwirqs numbering does not begin at zero.
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Note: the map() callback will be called before this function returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller).
 */
struct irq_domain *irq_domain_add_legacy(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 irq_hw_number_t first_hwirq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain;

	domain = __irq_domain_add(of_node, first_hwirq + size, 0, ops, host_data);
	if (!domain)
		return NULL;

	WARN_ON(irq_domain_associate_many(domain, first_irq, first_hwirq, size));

	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_legacy);

/**
 * irq_find_host() - Locates a domain for a given device node
 * @node: device-tree node of the interrupt controller
 */
struct irq_domain *irq_find_host(struct device_node *node)
{
	struct irq_domain *h, *found = NULL;
	int rc;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(h, &irq_domain_list, link) {
		if (h->ops->match)
			rc = h->ops->match(h, node);
		else
			rc = (h->of_node != NULL) && (h->of_node == node);

		if (rc) {
			found = h;
			break;
		}
	}
	mutex_unlock(&irq_domain_mutex);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_host);

/**
 * irq_set_default_host() - Set a "default" irq domain
 * @domain: default domain pointer
 *
 * For convenience, it's possible to set a "default" domain that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
void irq_set_default_host(struct irq_domain *domain)
{
	pr_debug("Default domain set to @0x%p\n", domain);

	irq_default_domain = domain;
}
EXPORT_SYMBOL_GPL(irq_set_default_host);

static void irq_domain_disassociate_many(struct irq_domain *domain,
					 unsigned int irq_base, int count)
{
	/*
	 * disassociate in reverse order;
	 * not strictly necessary, but nice for unwinding
	 */
	while (count--) {
		int irq = irq_base + count;
		struct irq_data *irq_data = irq_get_irq_data(irq);
		irq_hw_number_t hwirq;

		if (WARN_ON(!irq_data || irq_data->domain != domain))
			continue;

		hwirq = irq_data->hwirq;
		irq_set_status_flags(irq, IRQ_NOREQUEST);

		/* remove chip and handler */
		irq_set_chip_and_handler(irq, NULL, NULL);

		/* Make sure it's completed */
		synchronize_irq(irq);

		/* Tell the PIC about it */
		if (domain->ops->unmap)
			domain->ops->unmap(domain, irq);
		smp_mb();

		irq_data->domain = NULL;
		irq_data->hwirq = 0;

		/* Clear reverse map for this hwirq */
		if (hwirq < domain->revmap_size) {
			domain->linear_revmap[hwirq] = 0;
		} else {
			mutex_lock(&revmap_trees_mutex);
			radix_tree_delete(&domain->revmap_tree, hwirq);
			mutex_unlock(&revmap_trees_mutex);
		}
	}
}

int irq_domain_associate_many(struct irq_domain *domain, unsigned int irq_base,
			      irq_hw_number_t hwirq_base, int count)
{
	unsigned int virq = irq_base;
	irq_hw_number_t hwirq = hwirq_base;
	int i, ret;

	pr_debug("%s(%s, irqbase=%i, hwbase=%i, count=%i)\n", __func__,
		of_node_full_name(domain->of_node), irq_base, (int)hwirq_base, count);

	for (i = 0; i < count; i++) {
		struct irq_data *irq_data = irq_get_irq_data(virq + i);

		if (WARN(!irq_data, "error: irq_desc not allocated; "
			 "irq=%i hwirq=0x%x\n", virq + i, (int)hwirq + i))
			return -EINVAL;
		if (WARN(irq_data->domain, "error: irq_desc already associated; "
			 "irq=%i hwirq=0x%x\n", virq + i, (int)hwirq + i))
			return -EINVAL;
	};

	for (i = 0; i < count; i++, virq++, hwirq++) {
		struct irq_data *irq_data = irq_get_irq_data(virq);

		irq_data->hwirq = hwirq;
		irq_data->domain = domain;
		if (domain->ops->map) {
			ret = domain->ops->map(domain, virq, hwirq);
			if (ret != 0) {
				/*
				 * If map() returns -EPERM, this interrupt is protected
				 * by the firmware or some other service and shall not
				 * be mapped. Don't bother telling the user about it.
				 */
				if (ret != -EPERM) {
					pr_info("%s didn't like hwirq-0x%lx to VIRQ%i mapping (rc=%d)\n",
					       domain->name, hwirq, virq, ret);
				}
				irq_data->domain = NULL;
				irq_data->hwirq = 0;
				continue;
			}
			/* If not already assigned, give the domain the chip's name */
			if (!domain->name && irq_data->chip)
				domain->name = irq_data->chip->name;
		}

		if (hwirq < domain->revmap_size) {
			domain->linear_revmap[hwirq] = virq;
		} else {
			mutex_lock(&revmap_trees_mutex);
			radix_tree_insert(&domain->revmap_tree, hwirq, irq_data);
			mutex_unlock(&revmap_trees_mutex);
		}

		irq_clear_status_flags(virq, IRQ_NOREQUEST);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_associate_many);

/**
 * irq_create_direct_mapping() - Allocate an irq for direct mapping
 * @domain: domain to allocate the irq for or NULL for default domain
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux irq as the hardware interrupt number. It still uses the linear
 * or radix tree to store the mapping, but the irq controller can optimize
 * the revmap path by using the hwirq directly.
 */
unsigned int irq_create_direct_mapping(struct irq_domain *domain)
{
	unsigned int virq;

	if (domain == NULL)
		domain = irq_default_domain;

	virq = irq_alloc_desc_from(1, of_node_to_nid(domain->of_node));
	if (!virq) {
		pr_debug("create_direct virq allocation failed\n");
		return 0;
	}
	if (virq >= domain->revmap_direct_max_irq) {
		pr_err("ERROR: no free irqs available below %i maximum\n",
			domain->revmap_direct_max_irq);
		irq_free_desc(virq);
		return 0;
	}
	pr_debug("create_direct obtained virq %d\n", virq);

	if (irq_domain_associate(domain, virq, virq)) {
		irq_free_desc(virq);
		return 0;
	}

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_direct_mapping);

/**
 * irq_create_mapping() - Map a hardware interrupt into linux irq space
 * @domain: domain owning this hardware interrupt or NULL for default domain
 * @hwirq: hardware irq number in that domain space
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * irq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
unsigned int irq_create_mapping(struct irq_domain *domain,
				irq_hw_number_t hwirq)
{
	unsigned int hint;
	int virq;

	pr_debug("irq_create_mapping(0x%p, 0x%lx)\n", domain, hwirq);

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL) {
		pr_warning("irq_create_mapping called for"
			   " NULL domain, hwirq=%lx\n", hwirq);
		WARN_ON(1);
		return 0;
	}
	pr_debug("-> using domain @%p\n", domain);

	/* Check if mapping already exists */
	virq = irq_find_mapping(domain, hwirq);
	if (virq) {
		pr_debug("-> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Allocate a virtual interrupt number */
	hint = hwirq % nr_irqs;
	if (hint == 0)
		hint++;
	virq = irq_alloc_desc_from(hint, of_node_to_nid(domain->of_node));
	if (virq <= 0)
		virq = irq_alloc_desc_from(1, of_node_to_nid(domain->of_node));
	if (virq <= 0) {
		pr_debug("-> virq allocation failed\n");
		return 0;
	}

	if (irq_domain_associate(domain, virq, hwirq)) {
		irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq %lu on domain %s mapped to virtual irq %u\n",
		hwirq, of_node_full_name(domain->of_node), virq);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

/**
 * irq_create_strict_mappings() - Map a range of hw irqs to fixed linux irqs
 * @domain: domain owning the interrupt range
 * @irq_base: beginning of linux IRQ range
 * @hwirq_base: beginning of hardware IRQ range
 * @count: Number of interrupts to map
 *
 * This routine is used for allocating and mapping a range of hardware
 * irqs to linux irqs where the linux irq numbers are at pre-defined
 * locations. For use by controllers that already have static mappings
 * to insert in to the domain.
 *
 * Non-linear users can use irq_create_identity_mapping() for IRQ-at-a-time
 * domain insertion.
 *
 * 0 is returned upon success, while any failure to establish a static
 * mapping is treated as an error.
 */
int irq_create_strict_mappings(struct irq_domain *domain, unsigned int irq_base,
			       irq_hw_number_t hwirq_base, int count)
{
	int ret;

	ret = irq_alloc_descs(irq_base, irq_base, count,
			      of_node_to_nid(domain->of_node));
	if (unlikely(ret < 0))
		return ret;

	ret = irq_domain_associate_many(domain, irq_base, hwirq_base, count);
	if (unlikely(ret < 0)) {
		irq_free_descs(irq_base, count);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(irq_create_strict_mappings);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *domain;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	unsigned int virq;

	domain = controller ? irq_find_host(controller) : irq_default_domain;
	if (!domain) {
#ifdef CONFIG_MIPS
		/*
		 * Workaround to avoid breaking interrupt controller drivers
		 * that don't yet register an irq_domain.  This is temporary
		 * code. ~~~gcl, Feb 24, 2012
		 *
		 * Scheduled for removal in Linux v3.6.  That should be enough
		 * time.
		 */
		if (intsize > 0)
			return intspec[0];
#endif
		pr_warning("no irq domain found for %s !\n",
			   of_node_full_name(controller));
		return 0;
	}

	/* If domain has no translation, then we assume interrupt line */
	if (domain->ops->xlate == NULL)
		hwirq = intspec[0];
	else {
		if (domain->ops->xlate(domain, controller, intspec, intsize,
				     &hwirq, &type))
			return 0;
	}

	/* Create mapping */
	virq = irq_create_mapping(domain, hwirq);
	if (!virq)
		return virq;

	/* Set type if specified and different than the current one */
	if (type != IRQ_TYPE_NONE &&
	    type != (irqd_get_trigger_type(irq_get_irq_data(virq))))
		irq_set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

/**
 * irq_dispose_mapping() - Unmap an interrupt
 * @virq: linux irq number of the interrupt to unmap
 */
void irq_dispose_mapping(unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	struct irq_domain *domain;

	if (!virq || !irq_data)
		return;

	domain = irq_data->domain;
	if (WARN_ON(domain == NULL))
		return;

	irq_domain_disassociate_many(domain, virq, 1);
	irq_free_desc(virq);
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

/**
 * irq_find_mapping() - Find a linux irq from an hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 */
unsigned int irq_find_mapping(struct irq_domain *domain,
			      irq_hw_number_t hwirq)
{
	struct irq_data *data;

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL)
		return 0;

	if (hwirq < domain->revmap_direct_max_irq) {
		data = irq_get_irq_data(hwirq);
		if (data && (data->domain == domain) && (data->hwirq == hwirq))
			return hwirq;
	}

	return irq_linear_revmap(domain, hwirq);
}
EXPORT_SYMBOL_GPL(irq_find_mapping);

/**
 * irq_linear_revmap() - Find a linux irq from a hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 *
 * This is a fast path that can be called directly by irq controller code to
 * save a handful of instructions.
 */
unsigned int irq_linear_revmap(struct irq_domain *domain,
			       irq_hw_number_t hwirq)
{
	struct irq_data *data;

	/* Check revmap bounds; complain if exceeded */
	if (hwirq >= domain->revmap_size) {
		rcu_read_lock();
		data = radix_tree_lookup(&domain->revmap_tree, hwirq);
		rcu_read_unlock();
		return data ? data->irq : 0;
	}

	return domain->linear_revmap[hwirq];
}
EXPORT_SYMBOL_GPL(irq_linear_revmap);

#ifdef CONFIG_IRQ_DOMAIN_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	struct irq_desc *desc;
	struct irq_domain *domain;
	struct radix_tree_iter iter;
	void *data, **slot;
	int i;

	seq_printf(m, " %-16s  %-6s  %-10s  %-10s  %s\n",
		   "name", "mapped", "linear-max", "direct-max", "devtree-node");
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(domain, &irq_domain_list, link) {
		int count = 0;
		radix_tree_for_each_slot(slot, &domain->revmap_tree, &iter, 0)
			count++;
		seq_printf(m, "%c%-16s  %6u  %10u  %10u  %s\n",
			   domain == irq_default_domain ? '*' : ' ', domain->name,
			   domain->revmap_size + count, domain->revmap_size,
			   domain->revmap_direct_max_irq,
			   domain->of_node ? of_node_full_name(domain->of_node) : "");
	}
	mutex_unlock(&irq_domain_mutex);

	seq_printf(m, "%-5s  %-7s  %-15s  %-*s  %6s  %-14s  %s\n", "irq", "hwirq",
		      "chip name", (int)(2 * sizeof(void *) + 2), "chip data",
		      "active", "type", "domain");

	for (i = 1; i < nr_irqs; i++) {
		desc = irq_to_desc(i);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);
		domain = desc->irq_data.domain;

		if (domain) {
			struct irq_chip *chip;
			int hwirq = desc->irq_data.hwirq;
			bool direct;

			seq_printf(m, "%5d  ", i);
			seq_printf(m, "0x%05x  ", hwirq);

			chip = irq_desc_get_chip(desc);
			seq_printf(m, "%-15s  ", (chip && chip->name) ? chip->name : "none");

			data = irq_desc_get_chip_data(desc);
			seq_printf(m, data ? "0x%p  " : "  %p  ", data);

			seq_printf(m, "   %c    ", (desc->action && desc->action->handler) ? '*' : ' ');
			direct = (i == hwirq) && (i < domain->revmap_direct_max_irq);
			seq_printf(m, "%6s%-8s  ",
				   (hwirq < domain->revmap_size) ? "LINEAR" : "RADIX",
				   direct ? "(DIRECT)" : "");
			seq_printf(m, "%s\n", desc->irq_data.domain->name);
		}

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	return 0;
}

static int virq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, virq_debug_show, inode->i_private);
}

static const struct file_operations virq_debug_fops = {
	.open = virq_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init irq_debugfs_init(void)
{
	if (debugfs_create_file("irq_domain_mapping", S_IRUGO, NULL,
				 NULL, &virq_debug_fops) == NULL)
		return -ENOMEM;

	return 0;
}
__initcall(irq_debugfs_init);
#endif /* CONFIG_IRQ_DOMAIN_DEBUG */

/**
 * irq_domain_xlate_onecell() - Generic xlate for direct one cell bindings
 *
 * Device Tree IRQ specifier translation function which works with one cell
 * bindings where the cell value maps directly to the hwirq number.
 */
int irq_domain_xlate_onecell(struct irq_domain *d, struct device_node *ctrlr,
			     const u32 *intspec, unsigned int intsize,
			     unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_onecell);

/**
 * irq_domain_xlate_twocell() - Generic xlate for direct two cell bindings
 *
 * Device Tree IRQ specifier translation function which works with two cell
 * bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 */
int irq_domain_xlate_twocell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 2))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_twocell);

/**
 * irq_domain_xlate_onetwocell() - Generic xlate for one or two cell bindings
 *
 * Device Tree IRQ specifier translation function which works with either one
 * or two cell bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 *
 * Note: don't use this function unless your interrupt controller explicitly
 * supports both one and two cell bindings.  For the majority of controllers
 * the _onecell() or _twocell() variants above should be used.
 */
int irq_domain_xlate_onetwocell(struct irq_domain *d,
				struct device_node *ctrlr,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = (intsize > 1) ? intspec[1] : IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_onetwocell);

const struct irq_domain_ops irq_domain_simple_ops = {
	.xlate = irq_domain_xlate_onetwocell,
};
EXPORT_SYMBOL_GPL(irq_domain_simple_ops);

#ifdef CONFIG_OF_IRQ
void irq_domain_generate_simple(const struct of_device_id *match,
				u64 phys_base, unsigned int irq_start)
{
	struct device_node *node;
	pr_debug("looking for phys_base=%llx, irq_start=%i\n",
		(unsigned long long) phys_base, (int) irq_start);
	node = of_find_matching_node_by_address(NULL, match, phys_base);
	if (node)
		irq_domain_add_legacy(node, 32, irq_start, 0,
				      &irq_domain_simple_ops, NULL);
}
EXPORT_SYMBOL_GPL(irq_domain_generate_simple);
#endif
