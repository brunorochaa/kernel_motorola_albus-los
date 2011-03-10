/* linux/arch/arm/mach-exynos4/include/mach/regs-pmu.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Power management unit definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_PMU_H
#define __ASM_ARCH_REGS_PMU_H __FILE__

#include <mach/map.h>

#define S5P_PMUREG(x)			(S5P_VA_PMU + (x))

#define S5P_MIPI_DPHY_CONTROL(n)	S5P_PMUREG(0x0710 + (n) * 4)
#define S5P_MIPI_DPHY_ENABLE		(1 << 0)
#define S5P_MIPI_DPHY_SRESETN		(1 << 1)
#define S5P_MIPI_DPHY_MRESETN		(1 << 2)

#define S5P_PMU_CAM_CONF		S5P_PMUREG(0x3C00)
#define S5P_PMU_TV_CONF			S5P_PMUREG(0x3C20)
#define S5P_PMU_MFC_CONF		S5P_PMUREG(0x3C40)
#define S5P_PMU_G3D_CONF		S5P_PMUREG(0x3C60)
#define S5P_PMU_LCD0_CONF		S5P_PMUREG(0x3C80)
#define S5P_PMU_LCD1_CONF		S5P_PMUREG(0x3CA0)
#define S5P_PMU_GPS_CONF		S5P_PMUREG(0x3CE0)

#define S5P_INT_LOCAL_PWR_EN		0x7

#endif /* __ASM_ARCH_REGS_PMU_H */
