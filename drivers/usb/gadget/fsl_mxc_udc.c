/*
 * Copyright (C) 2009
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Description:
 * Helper routines for i.MX3x SoCs from Freescale, needed by the fsl_usb2_udc.c
 * driver to function correctly on these systems.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fsl_devices.h>
#include <linux/platform_device.h>
#include <linux/io.h>

static struct clk *mxc_ahb_clk;
static struct clk *mxc_per_clk;
static struct clk *mxc_ipg_clk;

/* workaround ENGcm09152 for i.MX35 */
#define USBPHYCTRL_OTGBASE_OFFSET	0x608
#define USBPHYCTRL_EVDO			(1 << 23)

int fsl_udc_clk_init(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata;
	unsigned long freq;
	int ret;

	pdata = pdev->dev.platform_data;

	mxc_ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(mxc_ipg_clk)) {
		dev_err(&pdev->dev, "clk_get(\"ipg\") failed\n");
		return PTR_ERR(mxc_ipg_clk);
	}

	mxc_ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(mxc_ahb_clk)) {
		dev_err(&pdev->dev, "clk_get(\"ahb\") failed\n");
		return PTR_ERR(mxc_ahb_clk);
	}

	mxc_per_clk = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(mxc_per_clk)) {
		dev_err(&pdev->dev, "clk_get(\"per\") failed\n");
		return PTR_ERR(mxc_per_clk);
	}

	clk_prepare_enable(mxc_ipg_clk);
	clk_prepare_enable(mxc_ahb_clk);
	clk_prepare_enable(mxc_per_clk);

	/* make sure USB_CLK is running at 60 MHz +/- 1000 Hz */
	if (!strcmp(pdev->id_entry->name, "imx-udc-mx27")) {
		freq = clk_get_rate(mxc_per_clk);
		if (pdata->phy_mode != FSL_USB2_PHY_ULPI &&
		    (freq < 59999000 || freq > 60001000)) {
			dev_err(&pdev->dev, "USB_CLK=%lu, should be 60MHz\n", freq);
			ret = -EINVAL;
			goto eclkrate;
		}
	}

	return 0;

eclkrate:
	clk_disable_unprepare(mxc_ipg_clk);
	clk_disable_unprepare(mxc_ahb_clk);
	clk_disable_unprepare(mxc_per_clk);
	mxc_per_clk = NULL;
	return ret;
}

void fsl_udc_clk_finalize(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;
	unsigned int v;

	/* workaround ENGcm09152 for i.MX35 */
	if (pdata->workaround & FLS_USB2_WORKAROUND_ENGCM09152) {
		v = readl(MX35_IO_ADDRESS(MX35_USB_BASE_ADDR +
				USBPHYCTRL_OTGBASE_OFFSET));
		writel(v | USBPHYCTRL_EVDO,
			MX35_IO_ADDRESS(MX35_USB_BASE_ADDR +
				USBPHYCTRL_OTGBASE_OFFSET));
	}

	/* ULPI transceivers don't need usbpll */
	if (pdata->phy_mode == FSL_USB2_PHY_ULPI) {
		clk_disable_unprepare(mxc_per_clk);
		mxc_per_clk = NULL;
	}
}

void fsl_udc_clk_release(void)
{
	if (mxc_per_clk)
		clk_disable_unprepare(mxc_per_clk);
	clk_disable_unprepare(mxc_ahb_clk);
	clk_disable_unprepare(mxc_ipg_clk);
}
