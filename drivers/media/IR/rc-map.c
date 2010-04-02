/* ir-raw-event.c - handle IR Pulse/Space event
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <media/ir-core.h>
#include <linux/spinlock.h>

/* Used to handle IR raw handler extensions */
static LIST_HEAD(rc_map_list);
static spinlock_t rc_map_lock;


static struct rc_keymap *seek_rc_map(const char *name)
{
	struct rc_keymap *map = NULL;

	spin_lock(&rc_map_lock);
	list_for_each_entry(map, &rc_map_list, list) {
		if (!strcmp(name, map->map.name))
			break;
	}
	spin_unlock(&rc_map_lock);

	return map;
}

struct ir_scancode_table *get_rc_map(const char *name)
{
	int rc = 0;

	struct rc_keymap *map;

	map = seek_rc_map(name);
#ifdef MODULE
	if (!map) {
		rc = request_module("name");
		if (rc < 0)
			return NULL;

		map = seek_rc_map(name);
	}
#endif
	if (!map)
		return NULL;

	return &map->map;
}
EXPORT_SYMBOL_GPL(get_rc_map);

int ir_register_map(struct rc_keymap *map)
{
	spin_lock(&rc_map_lock);
	list_add_tail(&map->list, &rc_map_list);
	spin_unlock(&rc_map_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ir_register_map);

void ir_unregister_map(struct rc_keymap *map)
{
	spin_lock(&rc_map_lock);
	list_del(&map->list);
	spin_unlock(&rc_map_lock);
}
EXPORT_SYMBOL_GPL(ir_unregister_map);
