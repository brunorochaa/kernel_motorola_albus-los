/*
 * Interface the pinmux subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINMUX_H
#define __LINUX_PINCTRL_PINMUX_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include "pinctrl.h"

/* This struct is private to the core and should be regarded as a cookie */
struct pinmux;

#ifdef CONFIG_PINMUX

struct pinctrl_dev;

/**
 * struct pinmux_ops - pinmux operations, to be implemented by pin controller
 * drivers that support pinmuxing
 * @request: called by the core to see if a certain pin can be made available
 *	available for muxing. This is called by the core to acquire the pins
 *	before selecting any actual mux setting across a function. The driver
 *	is allowed to answer "no" by returning a negative error code
 * @free: the reverse function of the request() callback, frees a pin after
 *	being requested
 * @list_functions: list the number of selectable named functions available
 *	in this pinmux driver, the core will begin on 0 and call this
 *	repeatedly as long as it returns >= 0 to enumerate mux settings
 * @get_function_name: return the function name of the muxing selector,
 *	called by the core to figure out which mux setting it shall map a
 *	certain device to
 * @get_function_groups: return an array of groups names (in turn
 *	referencing pins) connected to a certain function selector. The group
 *	name can be used with the generic @pinctrl_ops to retrieve the
 *	actual pins affected. The applicable groups will be returned in
 *	@groups and the number of groups in @num_groups
 * @enable: enable a certain muxing function with a certain pin group. The
 *	driver does not need to figure out whether enabling this function
 *	conflicts some other use of the pins in that group, such collisions
 *	are handled by the pinmux subsystem. The @func_selector selects a
 *	certain function whereas @group_selector selects a certain set of pins
 *	to be used. On simple controllers the latter argument may be ignored
 * @disable: disable a certain muxing selector with a certain pin group
 * @gpio_request_enable: requests and enables GPIO on a certain pin.
 *	Implement this only if you can mux every pin individually as GPIO. The
 *	affected GPIO range is passed along with an offset into that
 *	specific GPIO range - function selectors and pin groups are orthogonal
 *	to this, the core will however make sure the pins do not collide
 */
struct pinmux_ops {
	int (*request) (struct pinctrl_dev *pctldev, unsigned offset);
	int (*free) (struct pinctrl_dev *pctldev, unsigned offset);
	int (*list_functions) (struct pinctrl_dev *pctldev, unsigned selector);
	const char *(*get_function_name) (struct pinctrl_dev *pctldev,
					  unsigned selector);
	int (*get_function_groups) (struct pinctrl_dev *pctldev,
				  unsigned selector,
				  const char * const **groups,
				  unsigned * const num_groups);
	int (*enable) (struct pinctrl_dev *pctldev, unsigned func_selector,
		       unsigned group_selector);
	void (*disable) (struct pinctrl_dev *pctldev, unsigned func_selector,
			 unsigned group_selector);
	int (*gpio_request_enable) (struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned offset);
	void (*gpio_disable_free) (struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset);
};

/* External interface to pinmux */
extern int pinmux_request_gpio(unsigned gpio);
extern void pinmux_free_gpio(unsigned gpio);
extern struct pinmux * __must_check pinmux_get(struct device *dev, const char *name);
extern void pinmux_put(struct pinmux *pmx);
extern int pinmux_enable(struct pinmux *pmx);
extern void pinmux_disable(struct pinmux *pmx);

#else /* !CONFIG_PINMUX */

static inline int pinmux_request_gpio(unsigned gpio)
{
	return 0;
}

static inline void pinmux_free_gpio(unsigned gpio)
{
}

static inline struct pinmux * __must_check pinmux_get(struct device *dev, const char *name)
{
	return NULL;
}

static inline void pinmux_put(struct pinmux *pmx)
{
}

static inline int pinmux_enable(struct pinmux *pmx)
{
	return 0;
}

static inline void pinmux_disable(struct pinmux *pmx)
{
}

#endif /* CONFIG_PINMUX */

#endif /* __LINUX_PINCTRL_PINMUX_H */
