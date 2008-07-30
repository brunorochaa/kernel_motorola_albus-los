/*
 * linux/arch/arm/mach-pxa/pxa2xx.c
 *
 * code specific to pxa2xx
 *
 * Copyright (C) 2008 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <asm/arch/mfp-pxa2xx.h>
#include <asm/arch/mfp-pxa25x.h>
#include <asm/arch/irda.h>

static unsigned long pxa2xx_mfp_fir[] = {
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,
};

static unsigned long pxa2xx_mfp_sir[] = {
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,
};

static unsigned long pxa2xx_mfp_off[] = {
	GPIO46_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO47_GPIO | MFP_LPM_DRIVE_LOW,
};

void pxa2xx_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF) {
		pxa2xx_mfp_config(pxa2xx_mfp_off, ARRAY_SIZE(pxa2xx_mfp_off));
	} else if (mode & IR_SIRMODE) {
		pxa2xx_mfp_config(pxa2xx_mfp_sir, ARRAY_SIZE(pxa2xx_mfp_sir));
	} else if (mode & IR_FIRMODE) {
		pxa2xx_mfp_config(pxa2xx_mfp_fir, ARRAY_SIZE(pxa2xx_mfp_fir));
	} else
		BUG();
}
