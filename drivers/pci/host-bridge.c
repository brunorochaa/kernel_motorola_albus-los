/*
 * host bridge related code
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>

#include "pci.h"

static LIST_HEAD(pci_host_bridges);

void add_to_pci_host_bridges(struct pci_host_bridge *bridge)
{
	list_add_tail(&bridge->list, &pci_host_bridges);
}

static struct pci_host_bridge *pci_host_bridge(struct pci_dev *dev)
{
	struct pci_bus *bus;
	struct pci_host_bridge *bridge;

	bus = dev->bus;
	while (bus->parent)
		bus = bus->parent;

	list_for_each_entry(bridge, &pci_host_bridges, list) {
		if (bridge->bus == bus)
			return bridge;
	}

	return NULL;
}

static bool resource_contains(struct resource *res1, struct resource *res2)
{
	return res1->start <= res2->start && res1->end >= res2->end;
}

void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			     struct resource *res)
{
	struct pci_host_bridge *bridge = pci_host_bridge(dev);
	struct pci_host_bridge_window *window;
	resource_size_t offset = 0;

	list_for_each_entry(window, &bridge->windows, list) {
		if (resource_type(res) != resource_type(window->res))
			continue;

		if (resource_contains(window->res, res)) {
			offset = window->offset;
			break;
		}
	}

	region->start = res->start - offset;
	region->end = res->end - offset;
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

static bool region_contains(struct pci_bus_region *region1,
			    struct pci_bus_region *region2)
{
	return region1->start <= region2->start && region1->end >= region2->end;
}

void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			     struct pci_bus_region *region)
{
	struct pci_host_bridge *bridge = pci_host_bridge(dev);
	struct pci_host_bridge_window *window;
	struct pci_bus_region bus_region;
	resource_size_t offset = 0;

	list_for_each_entry(window, &bridge->windows, list) {
		if (resource_type(res) != resource_type(window->res))
			continue;

		bus_region.start = window->res->start - window->offset;
		bus_region.end = window->res->end - window->offset;

		if (region_contains(&bus_region, region)) {
			offset = window->offset;
			break;
		}
	}

	res->start = region->start + offset;
	res->end = region->end + offset;
}
EXPORT_SYMBOL(pcibios_bus_to_resource);
