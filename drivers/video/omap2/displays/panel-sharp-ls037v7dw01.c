/*
 * LCD panel driver for Sharp LS037V7DW01
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <video/omapdss.h>

struct sharp_data {
};

static struct omap_video_timings sharp_ls_timings = {
	.x_res = 480,
	.y_res = 640,

	.pixel_clock	= 19200,

	.hsw		= 2,
	.hfp		= 1,
	.hbp		= 28,

	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 1,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
};


static int sharp_ls_panel_probe(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd;

	dssdev->panel.timings = sharp_ls_timings;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	dev_set_drvdata(&dssdev->dev, sd);

	return 0;
}

static void __exit sharp_ls_panel_remove(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd = dev_get_drvdata(&dssdev->dev);

	kfree(sd);
}

static int sharp_ls_power_on(struct omap_dss_device *dssdev)
{
	int r = 0;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	omapdss_dpi_set_timings(dssdev, &dssdev->panel.timings);
	omapdss_dpi_set_data_lines(dssdev, dssdev->phy.dpi.data_lines);

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err1;
	}

	return 0;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void sharp_ls_power_off(struct omap_dss_device *dssdev)
{
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);

	omapdss_dpi_display_disable(dssdev);
}

static int sharp_ls_panel_enable(struct omap_dss_device *dssdev)
{
	int r;
	r = sharp_ls_power_on(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
	return r;
}

static void sharp_ls_panel_disable(struct omap_dss_device *dssdev)
{
	sharp_ls_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static struct omap_dss_driver sharp_ls_driver = {
	.probe		= sharp_ls_panel_probe,
	.remove		= __exit_p(sharp_ls_panel_remove),

	.enable		= sharp_ls_panel_enable,
	.disable	= sharp_ls_panel_disable,

	.driver         = {
		.name   = "sharp_ls_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init sharp_ls_panel_drv_init(void)
{
	return omap_dss_register_driver(&sharp_ls_driver);
}

static void __exit sharp_ls_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&sharp_ls_driver);
}

module_init(sharp_ls_panel_drv_init);
module_exit(sharp_ls_panel_drv_exit);
MODULE_LICENSE("GPL");
