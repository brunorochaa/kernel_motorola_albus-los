/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/backlight.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	struct {
		unsigned int width;
		unsigned int height;
	} size;
};

/* TODO: convert to gpiod_*() API once it's been merged */
#define GPIO_ACTIVE_LOW	(1 << 0)

struct panel_simple {
	struct drm_panel base;
	bool enabled;

	const struct panel_desc *desc;

	struct backlight_device *backlight;
	struct regulator *supply;
	struct i2c_adapter *ddc;

	unsigned long enable_gpio_flags;
	int enable_gpio;
};

static inline struct panel_simple *to_panel_simple(struct drm_panel *panel)
{
	return container_of(panel, struct panel_simple, base);
}

static int panel_simple_get_fixed_modes(struct panel_simple *panel)
{
	struct drm_connector *connector = panel->base.connector;
	struct drm_device *drm = panel->base.drm;
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	if (!panel->desc)
		return 0;

	for (i = 0; i < panel->desc->num_modes; i++) {
		const struct drm_display_mode *m = &panel->desc->modes[i];

		mode = drm_mode_duplicate(drm, m);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	connector->display_info.width_mm = panel->desc->size.width;
	connector->display_info.height_mm = panel->desc->size.height;

	return num;
}

static int panel_simple_disable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (!p->enabled)
		return 0;

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(p->backlight);
	}

	if (gpio_is_valid(p->enable_gpio)) {
		if (p->enable_gpio_flags & GPIO_ACTIVE_LOW)
			gpio_set_value(p->enable_gpio, 1);
		else
			gpio_set_value(p->enable_gpio, 0);
	}

	regulator_disable(p->supply);
	p->enabled = false;

	return 0;
}

static int panel_simple_enable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int err;

	if (p->enabled)
		return 0;

	err = regulator_enable(p->supply);
	if (err < 0) {
		dev_err(panel->dev, "failed to enable supply: %d\n", err);
		return err;
	}

	if (gpio_is_valid(p->enable_gpio)) {
		if (p->enable_gpio_flags & GPIO_ACTIVE_LOW)
			gpio_set_value(p->enable_gpio, 0);
		else
			gpio_set_value(p->enable_gpio, 1);
	}

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(p->backlight);
	}

	p->enabled = true;

	return 0;
}

static int panel_simple_get_modes(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int num = 0;

	/* probe EDID if a DDC bus is available */
	if (p->ddc) {
		struct edid *edid = drm_get_edid(panel->connector, p->ddc);
		if (edid) {
			num += drm_add_edid_modes(panel->connector, edid);
			kfree(edid);
		}
	}

	/* add hard-coded panel modes */
	num += panel_simple_get_fixed_modes(p);

	return num;
}

static const struct drm_panel_funcs panel_simple_funcs = {
	.disable = panel_simple_disable,
	.enable = panel_simple_enable,
	.get_modes = panel_simple_get_modes,
};

static int panel_simple_probe(struct device *dev, const struct panel_desc *desc)
{
	struct device_node *backlight, *ddc;
	struct panel_simple *panel;
	enum of_gpio_flags flags;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->enabled = false;
	panel->desc = desc;

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply))
		return PTR_ERR(panel->supply);

	panel->enable_gpio = of_get_named_gpio_flags(dev->of_node,
						     "enable-gpios", 0,
						     &flags);
	if (gpio_is_valid(panel->enable_gpio)) {
		unsigned int value;

		if (flags & OF_GPIO_ACTIVE_LOW)
			panel->enable_gpio_flags |= GPIO_ACTIVE_LOW;

		err = gpio_request(panel->enable_gpio, "enable");
		if (err < 0) {
			dev_err(dev, "failed to request GPIO#%u: %d\n",
				panel->enable_gpio, err);
			return err;
		}

		value = (panel->enable_gpio_flags & GPIO_ACTIVE_LOW) != 0;

		err = gpio_direction_output(panel->enable_gpio, value);
		if (err < 0) {
			dev_err(dev, "failed to setup GPIO%u: %d\n",
				panel->enable_gpio, err);
			goto free_gpio;
		}
	}

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!panel->backlight) {
			err = -EPROBE_DEFER;
			goto free_gpio;
		}
	}

	ddc = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc) {
		panel->ddc = of_find_i2c_adapter_by_node(ddc);
		of_node_put(ddc);

		if (!panel->ddc) {
			err = -EPROBE_DEFER;
			goto free_backlight;
		}
	}

	drm_panel_init(&panel->base);
	panel->base.dev = dev;
	panel->base.funcs = &panel_simple_funcs;

	err = drm_panel_add(&panel->base);
	if (err < 0)
		goto free_ddc;

	dev_set_drvdata(dev, panel);

	return 0;

free_ddc:
	if (panel->ddc)
		put_device(&panel->ddc->dev);
free_backlight:
	if (panel->backlight)
		put_device(&panel->backlight->dev);
free_gpio:
	if (gpio_is_valid(panel->enable_gpio))
		gpio_free(panel->enable_gpio);

	return err;
}

static int panel_simple_remove(struct device *dev)
{
	struct panel_simple *panel = dev_get_drvdata(dev);

	drm_panel_detach(&panel->base);
	drm_panel_remove(&panel->base);

	panel_simple_disable(&panel->base);

	if (panel->ddc)
		put_device(&panel->ddc->dev);

	if (panel->backlight)
		put_device(&panel->backlight->dev);

	if (gpio_is_valid(panel->enable_gpio))
		gpio_free(panel->enable_gpio);

	regulator_disable(panel->supply);

	return 0;
}

static const struct drm_display_mode auo_b101aw03_mode = {
	.clock = 51450,
	.hdisplay = 1024,
	.hsync_start = 1024 + 156,
	.hsync_end = 1024 + 156 + 8,
	.htotal = 1024 + 156 + 8 + 156,
	.vdisplay = 600,
	.vsync_start = 600 + 16,
	.vsync_end = 600 + 16 + 6,
	.vtotal = 600 + 16 + 6 + 16,
	.vrefresh = 60,
};

static const struct panel_desc auo_b101aw03 = {
	.modes = &auo_b101aw03_mode,
	.num_modes = 1,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode chunghwa_claa101wb01_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 16,
	.vsync_end = 768 + 16 + 8,
	.vtotal = 768 + 16 + 8 + 16,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa101wb01 = {
	.modes = &chunghwa_claa101wb01_mode,
	.num_modes = 1,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct of_device_id platform_of_match[] = {
	{
		.compatible = "auo,b101aw03",
		.data = &auo_b101aw03,
	}, {
		.compatible = "chunghwa,claa101wb01",
		.data = &chunghwa_claa101wb01
	}, {
		.compatible = "simple-panel",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, platform_of_match);

static int panel_simple_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	id = of_match_node(platform_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return panel_simple_probe(&pdev->dev, id->data);
}

static int panel_simple_platform_remove(struct platform_device *pdev)
{
	return panel_simple_remove(&pdev->dev);
}

static struct platform_driver panel_simple_platform_driver = {
	.driver = {
		.name = "panel-simple",
		.owner = THIS_MODULE,
		.of_match_table = platform_of_match,
	},
	.probe = panel_simple_platform_probe,
	.remove = panel_simple_platform_remove,
};

struct panel_desc_dsi {
	struct panel_desc desc;

	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};

static const struct drm_display_mode panasonic_vvx10f004b00_mode = {
	.clock = 157200,
	.hdisplay = 1920,
	.hsync_start = 1920 + 154,
	.hsync_end = 1920 + 154 + 16,
	.htotal = 1920 + 154 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 17,
	.vsync_end = 1200 + 17 + 2,
	.vtotal = 1200 + 17 + 2 + 16,
	.vrefresh = 60,
};

static const struct panel_desc_dsi panasonic_vvx10f004b00 = {
	.desc = {
		.modes = &panasonic_vvx10f004b00_mode,
		.num_modes = 1,
		.size = {
			.width = 217,
			.height = 136,
		},
	},
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct of_device_id dsi_of_match[] = {
	{
		.compatible = "panasonic,vvx10f004b00",
		.data = &panasonic_vvx10f004b00
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, dsi_of_match);

static int panel_simple_dsi_probe(struct mipi_dsi_device *dsi)
{
	const struct panel_desc_dsi *desc;
	const struct of_device_id *id;
	int err;

	id = of_match_node(dsi_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	desc = id->data;

	err = panel_simple_probe(&dsi->dev, &desc->desc);
	if (err < 0)
		return err;

	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	return mipi_dsi_attach(dsi);
}

static int panel_simple_dsi_remove(struct mipi_dsi_device *dsi)
{
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	return panel_simple_remove(&dsi->dev);
}

static struct mipi_dsi_driver panel_simple_dsi_driver = {
	.driver = {
		.name = "panel-simple-dsi",
		.owner = THIS_MODULE,
		.of_match_table = dsi_of_match,
	},
	.probe = panel_simple_dsi_probe,
	.remove = panel_simple_dsi_remove,
};

static int __init panel_simple_init(void)
{
	int err;

	err = platform_driver_register(&panel_simple_platform_driver);
	if (err < 0)
		return err;

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI)) {
		err = mipi_dsi_driver_register(&panel_simple_dsi_driver);
		if (err < 0)
			return err;
	}

	return 0;
}
module_init(panel_simple_init);

static void __exit panel_simple_exit(void)
{
	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&panel_simple_dsi_driver);

	platform_driver_unregister(&panel_simple_platform_driver);
}
module_exit(panel_simple_exit);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM Driver for Simple Panels");
MODULE_LICENSE("GPL and additional rights");
