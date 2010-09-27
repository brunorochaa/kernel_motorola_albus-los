#ifndef __ARCH_ASM_MACH_OMAP2_PRCM_COMMON_H
#define __ARCH_ASM_MACH_OMAP2_PRCM_COMMON_H

/*
 * OMAP2/3 PRCM base and module definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 * OMAP4 defines in this file are automatically generated from the OMAP hardware
 * databases.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* Module offsets from both CM_BASE & PRM_BASE */

/*
 * Offsets that are the same on 24xx and 34xx
 *
 * Technically, in terms of the TRM, OCP_MOD is 34xx only; PLL_MOD is
 * CCR_MOD on 3430; and GFX_MOD only exists < 3430ES2.
 */
#define OCP_MOD						0x000
#define MPU_MOD						0x100
#define CORE_MOD					0x200
#define GFX_MOD						0x300
#define WKUP_MOD					0x400
#define PLL_MOD						0x500


/* Chip-specific module offsets */
#define OMAP24XX_GR_MOD					OCP_MOD
#define OMAP24XX_DSP_MOD				0x800

#define OMAP2430_MDM_MOD				0xc00

/* IVA2 module is < base on 3430 */
#define OMAP3430_IVA2_MOD				-0x800
#define OMAP3430ES2_SGX_MOD				GFX_MOD
#define OMAP3430_CCR_MOD				PLL_MOD
#define OMAP3430_DSS_MOD				0x600
#define OMAP3430_CAM_MOD				0x700
#define OMAP3430_PER_MOD				0x800
#define OMAP3430_EMU_MOD				0x900
#define OMAP3430_GR_MOD					0xa00
#define OMAP3430_NEON_MOD				0xb00
#define OMAP3430ES2_USBHOST_MOD				0xc00

#define BITS(n_bit)	\
	(((1 << n_bit) - 1) | (1 << n_bit))

#define BITFIELD(l_bit, u_bit)	\
	(BITS(u_bit) & ~((BITS(l_bit)) >> 1))

/* OMAP44XX specific module offsets */

/* CM1 instances */

#define OMAP4430_CM1_OCP_SOCKET_MOD	0x0000
#define OMAP4430_CM1_CKGEN_MOD		0x0100
#define OMAP4430_CM1_MPU_MOD		0x0300
#define OMAP4430_CM1_TESLA_MOD		0x0400
#define OMAP4430_CM1_ABE_MOD		0x0500
#define OMAP4430_CM1_RESTORE_MOD	0x0e00
#define OMAP4430_CM1_INSTR_MOD		0x0f00

/* CM2 instances */

#define OMAP4430_CM2_OCP_SOCKET_MOD	0x0000
#define OMAP4430_CM2_CKGEN_MOD		0x0100
#define OMAP4430_CM2_ALWAYS_ON_MOD	0x0600
#define OMAP4430_CM2_CORE_MOD		0x0700
#define OMAP4430_CM2_IVAHD_MOD		0x0f00
#define OMAP4430_CM2_CAM_MOD		0x1000
#define OMAP4430_CM2_DSS_MOD		0x1100
#define OMAP4430_CM2_GFX_MOD		0x1200
#define OMAP4430_CM2_L3INIT_MOD		0x1300
#define OMAP4430_CM2_L4PER_MOD		0x1400
#define OMAP4430_CM2_CEFUSE_MOD		0x1600
#define OMAP4430_CM2_RESTORE_MOD	0x1e00
#define OMAP4430_CM2_INSTR_MOD		0x1f00

/* PRM instances */

#define OMAP4430_PRM_OCP_SOCKET_MOD	0x0000
#define OMAP4430_PRM_CKGEN_MOD		0x0100
#define OMAP4430_PRM_MPU_MOD		0x0300
#define OMAP4430_PRM_TESLA_MOD		0x0400
#define OMAP4430_PRM_ABE_MOD		0x0500
#define OMAP4430_PRM_ALWAYS_ON_MOD	0x0600
#define OMAP4430_PRM_CORE_MOD		0x0700
#define OMAP4430_PRM_IVAHD_MOD		0x0f00
#define OMAP4430_PRM_CAM_MOD		0x1000
#define OMAP4430_PRM_DSS_MOD		0x1100
#define OMAP4430_PRM_GFX_MOD		0x1200
#define OMAP4430_PRM_L3INIT_MOD		0x1300
#define OMAP4430_PRM_L4PER_MOD		0x1400
#define OMAP4430_PRM_CEFUSE_MOD		0x1600
#define OMAP4430_PRM_WKUP_MOD		0x1700
#define OMAP4430_PRM_WKUP_CM_MOD	0x1800
#define OMAP4430_PRM_EMU_MOD		0x1900
#define OMAP4430_PRM_EMU_CM_MOD		0x1a00
#define OMAP4430_PRM_DEVICE_MOD		0x1b00
#define OMAP4430_PRM_INSTR_MOD		0x1f00

/* SCRM instances */

#define OMAP4430_SCRM_SCRM_MOD	0x0000

/* PRCM_MPU instances */

#define OMAP4430_PRCM_MPU_OCP_SOCKET_PRCM_MOD	0x0000
#define OMAP4430_PRCM_MPU_DEVICE_PRM_MOD	0x0200
#define OMAP4430_PRCM_MPU_CPU0_MOD		0x0400
#define OMAP4430_PRCM_MPU_CPU1_MOD		0x0800


/* 24XX register bits shared between CM & PRM registers */

/* CM_FCLKEN1_CORE, CM_ICLKEN1_CORE, PM_WKEN1_CORE shared bits */
#define OMAP2420_EN_MMC_SHIFT				26
#define OMAP2420_EN_MMC_MASK				(1 << 26)
#define OMAP24XX_EN_UART2_SHIFT				22
#define OMAP24XX_EN_UART2_MASK				(1 << 22)
#define OMAP24XX_EN_UART1_SHIFT				21
#define OMAP24XX_EN_UART1_MASK				(1 << 21)
#define OMAP24XX_EN_MCSPI2_SHIFT			18
#define OMAP24XX_EN_MCSPI2_MASK				(1 << 18)
#define OMAP24XX_EN_MCSPI1_SHIFT			17
#define OMAP24XX_EN_MCSPI1_MASK				(1 << 17)
#define OMAP24XX_EN_MCBSP2_SHIFT			16
#define OMAP24XX_EN_MCBSP2_MASK				(1 << 16)
#define OMAP24XX_EN_MCBSP1_SHIFT			15
#define OMAP24XX_EN_MCBSP1_MASK				(1 << 15)
#define OMAP24XX_EN_GPT12_SHIFT				14
#define OMAP24XX_EN_GPT12_MASK				(1 << 14)
#define OMAP24XX_EN_GPT11_SHIFT				13
#define OMAP24XX_EN_GPT11_MASK				(1 << 13)
#define OMAP24XX_EN_GPT10_SHIFT				12
#define OMAP24XX_EN_GPT10_MASK				(1 << 12)
#define OMAP24XX_EN_GPT9_SHIFT				11
#define OMAP24XX_EN_GPT9_MASK				(1 << 11)
#define OMAP24XX_EN_GPT8_SHIFT				10
#define OMAP24XX_EN_GPT8_MASK				(1 << 10)
#define OMAP24XX_EN_GPT7_SHIFT				9
#define OMAP24XX_EN_GPT7_MASK				(1 << 9)
#define OMAP24XX_EN_GPT6_SHIFT				8
#define OMAP24XX_EN_GPT6_MASK				(1 << 8)
#define OMAP24XX_EN_GPT5_SHIFT				7
#define OMAP24XX_EN_GPT5_MASK				(1 << 7)
#define OMAP24XX_EN_GPT4_SHIFT				6
#define OMAP24XX_EN_GPT4_MASK				(1 << 6)
#define OMAP24XX_EN_GPT3_SHIFT				5
#define OMAP24XX_EN_GPT3_MASK				(1 << 5)
#define OMAP24XX_EN_GPT2_SHIFT				4
#define OMAP24XX_EN_GPT2_MASK				(1 << 4)
#define OMAP2420_EN_VLYNQ_SHIFT				3
#define OMAP2420_EN_VLYNQ_MASK				(1 << 3)

/* CM_FCLKEN2_CORE, CM_ICLKEN2_CORE, PM_WKEN2_CORE shared bits */
#define OMAP2430_EN_GPIO5_SHIFT				10
#define OMAP2430_EN_GPIO5_MASK				(1 << 10)
#define OMAP2430_EN_MCSPI3_SHIFT			9
#define OMAP2430_EN_MCSPI3_MASK				(1 << 9)
#define OMAP2430_EN_MMCHS2_SHIFT			8
#define OMAP2430_EN_MMCHS2_MASK				(1 << 8)
#define OMAP2430_EN_MMCHS1_SHIFT			7
#define OMAP2430_EN_MMCHS1_MASK				(1 << 7)
#define OMAP24XX_EN_UART3_SHIFT				2
#define OMAP24XX_EN_UART3_MASK				(1 << 2)
#define OMAP24XX_EN_USB_SHIFT				0
#define OMAP24XX_EN_USB_MASK				(1 << 0)

/* CM_ICLKEN2_CORE, PM_WKEN2_CORE shared bits */
#define OMAP2430_EN_MDM_INTC_SHIFT			11
#define OMAP2430_EN_MDM_INTC_MASK			(1 << 11)
#define OMAP2430_EN_USBHS_SHIFT				6
#define OMAP2430_EN_USBHS_MASK				(1 << 6)

/* CM_IDLEST1_CORE, PM_WKST1_CORE shared bits */
#define OMAP2420_ST_MMC_SHIFT				26
#define OMAP2420_ST_MMC_MASK				(1 << 26)
#define OMAP24XX_ST_UART2_SHIFT				22
#define OMAP24XX_ST_UART2_MASK				(1 << 22)
#define OMAP24XX_ST_UART1_SHIFT				21
#define OMAP24XX_ST_UART1_MASK				(1 << 21)
#define OMAP24XX_ST_MCSPI2_SHIFT			18
#define OMAP24XX_ST_MCSPI2_MASK				(1 << 18)
#define OMAP24XX_ST_MCSPI1_SHIFT			17
#define OMAP24XX_ST_MCSPI1_MASK				(1 << 17)
#define OMAP24XX_ST_GPT12_SHIFT				14
#define OMAP24XX_ST_GPT12_MASK				(1 << 14)
#define OMAP24XX_ST_GPT11_SHIFT				13
#define OMAP24XX_ST_GPT11_MASK				(1 << 13)
#define OMAP24XX_ST_GPT10_SHIFT				12
#define OMAP24XX_ST_GPT10_MASK				(1 << 12)
#define OMAP24XX_ST_GPT9_SHIFT				11
#define OMAP24XX_ST_GPT9_MASK				(1 << 11)
#define OMAP24XX_ST_GPT8_SHIFT				10
#define OMAP24XX_ST_GPT8_MASK				(1 << 10)
#define OMAP24XX_ST_GPT7_SHIFT				9
#define OMAP24XX_ST_GPT7_MASK				(1 << 9)
#define OMAP24XX_ST_GPT6_SHIFT				8
#define OMAP24XX_ST_GPT6_MASK				(1 << 8)
#define OMAP24XX_ST_GPT5_SHIFT				7
#define OMAP24XX_ST_GPT5_MASK				(1 << 7)
#define OMAP24XX_ST_GPT4_SHIFT				6
#define OMAP24XX_ST_GPT4_MASK				(1 << 6)
#define OMAP24XX_ST_GPT3_SHIFT				5
#define OMAP24XX_ST_GPT3_MASK				(1 << 5)
#define OMAP24XX_ST_GPT2_SHIFT				4
#define OMAP24XX_ST_GPT2_MASK				(1 << 4)
#define OMAP2420_ST_VLYNQ_SHIFT				3
#define OMAP2420_ST_VLYNQ_MASK				(1 << 3)

/* CM_IDLEST2_CORE, PM_WKST2_CORE shared bits */
#define OMAP2430_ST_MDM_INTC_SHIFT			11
#define OMAP2430_ST_MDM_INTC_MASK			(1 << 11)
#define OMAP2430_ST_GPIO5_SHIFT				10
#define OMAP2430_ST_GPIO5_MASK				(1 << 10)
#define OMAP2430_ST_MCSPI3_SHIFT			9
#define OMAP2430_ST_MCSPI3_MASK				(1 << 9)
#define OMAP2430_ST_MMCHS2_SHIFT			8
#define OMAP2430_ST_MMCHS2_MASK				(1 << 8)
#define OMAP2430_ST_MMCHS1_SHIFT			7
#define OMAP2430_ST_MMCHS1_MASK				(1 << 7)
#define OMAP2430_ST_USBHS_SHIFT				6
#define OMAP2430_ST_USBHS_MASK				(1 << 6)
#define OMAP24XX_ST_UART3_SHIFT				2
#define OMAP24XX_ST_UART3_MASK				(1 << 2)
#define OMAP24XX_ST_USB_SHIFT				0
#define OMAP24XX_ST_USB_MASK				(1 << 0)

/* CM_FCLKEN_WKUP, CM_ICLKEN_WKUP, PM_WKEN_WKUP shared bits */
#define OMAP24XX_EN_GPIOS_SHIFT				2
#define OMAP24XX_EN_GPIOS_MASK				(1 << 2)
#define OMAP24XX_EN_GPT1_SHIFT				0
#define OMAP24XX_EN_GPT1_MASK				(1 << 0)

/* PM_WKST_WKUP, CM_IDLEST_WKUP shared bits */
#define OMAP24XX_ST_GPIOS_SHIFT				(1 << 2)
#define OMAP24XX_ST_GPIOS_MASK				2
#define OMAP24XX_ST_GPT1_SHIFT				(1 << 0)
#define OMAP24XX_ST_GPT1_MASK				0

/* CM_IDLEST_MDM and PM_WKST_MDM shared bits */
#define OMAP2430_ST_MDM_SHIFT				(1 << 0)


/* 3430 register bits shared between CM & PRM registers */

/* CM_REVISION, PRM_REVISION shared bits */
#define OMAP3430_REV_SHIFT				0
#define OMAP3430_REV_MASK				(0xff << 0)

/* CM_SYSCONFIG, PRM_SYSCONFIG shared bits */
#define OMAP3430_AUTOIDLE_MASK				(1 << 0)

/* CM_FCLKEN1_CORE, CM_ICLKEN1_CORE, PM_WKEN1_CORE shared bits */
#define OMAP3430_EN_MMC2_MASK				(1 << 25)
#define OMAP3430_EN_MMC2_SHIFT				25
#define OMAP3430_EN_MMC1_MASK				(1 << 24)
#define OMAP3430_EN_MMC1_SHIFT				24
#define OMAP3430_EN_MCSPI4_MASK				(1 << 21)
#define OMAP3430_EN_MCSPI4_SHIFT			21
#define OMAP3430_EN_MCSPI3_MASK				(1 << 20)
#define OMAP3430_EN_MCSPI3_SHIFT			20
#define OMAP3430_EN_MCSPI2_MASK				(1 << 19)
#define OMAP3430_EN_MCSPI2_SHIFT			19
#define OMAP3430_EN_MCSPI1_MASK				(1 << 18)
#define OMAP3430_EN_MCSPI1_SHIFT			18
#define OMAP3430_EN_I2C3_MASK				(1 << 17)
#define OMAP3430_EN_I2C3_SHIFT				17
#define OMAP3430_EN_I2C2_MASK				(1 << 16)
#define OMAP3430_EN_I2C2_SHIFT				16
#define OMAP3430_EN_I2C1_MASK				(1 << 15)
#define OMAP3430_EN_I2C1_SHIFT				15
#define OMAP3430_EN_UART2_MASK				(1 << 14)
#define OMAP3430_EN_UART2_SHIFT				14
#define OMAP3430_EN_UART1_MASK				(1 << 13)
#define OMAP3430_EN_UART1_SHIFT				13
#define OMAP3430_EN_GPT11_MASK				(1 << 12)
#define OMAP3430_EN_GPT11_SHIFT				12
#define OMAP3430_EN_GPT10_MASK				(1 << 11)
#define OMAP3430_EN_GPT10_SHIFT				11
#define OMAP3430_EN_MCBSP5_MASK				(1 << 10)
#define OMAP3430_EN_MCBSP5_SHIFT			10
#define OMAP3430_EN_MCBSP1_MASK				(1 << 9)
#define OMAP3430_EN_MCBSP1_SHIFT			9
#define OMAP3430_EN_FSHOSTUSB_MASK			(1 << 5)
#define OMAP3430_EN_FSHOSTUSB_SHIFT			5
#define OMAP3430_EN_D2D_MASK				(1 << 3)
#define OMAP3430_EN_D2D_SHIFT				3

/* CM_ICLKEN1_CORE, PM_WKEN1_CORE shared bits */
#define OMAP3430_EN_HSOTGUSB_MASK			(1 << 4)
#define OMAP3430_EN_HSOTGUSB_SHIFT			4

/* PM_WKST1_CORE, CM_IDLEST1_CORE shared bits */
#define OMAP3430_ST_MMC2_SHIFT				25
#define OMAP3430_ST_MMC2_MASK				(1 << 25)
#define OMAP3430_ST_MMC1_SHIFT				24
#define OMAP3430_ST_MMC1_MASK				(1 << 24)
#define OMAP3430_ST_MCSPI4_SHIFT			21
#define OMAP3430_ST_MCSPI4_MASK				(1 << 21)
#define OMAP3430_ST_MCSPI3_SHIFT			20
#define OMAP3430_ST_MCSPI3_MASK				(1 << 20)
#define OMAP3430_ST_MCSPI2_SHIFT			19
#define OMAP3430_ST_MCSPI2_MASK				(1 << 19)
#define OMAP3430_ST_MCSPI1_SHIFT			18
#define OMAP3430_ST_MCSPI1_MASK				(1 << 18)
#define OMAP3430_ST_I2C3_SHIFT				17
#define OMAP3430_ST_I2C3_MASK				(1 << 17)
#define OMAP3430_ST_I2C2_SHIFT				16
#define OMAP3430_ST_I2C2_MASK				(1 << 16)
#define OMAP3430_ST_I2C1_SHIFT				15
#define OMAP3430_ST_I2C1_MASK				(1 << 15)
#define OMAP3430_ST_UART2_SHIFT				14
#define OMAP3430_ST_UART2_MASK				(1 << 14)
#define OMAP3430_ST_UART1_SHIFT				13
#define OMAP3430_ST_UART1_MASK				(1 << 13)
#define OMAP3430_ST_GPT11_SHIFT				12
#define OMAP3430_ST_GPT11_MASK				(1 << 12)
#define OMAP3430_ST_GPT10_SHIFT				11
#define OMAP3430_ST_GPT10_MASK				(1 << 11)
#define OMAP3430_ST_MCBSP5_SHIFT			10
#define OMAP3430_ST_MCBSP5_MASK				(1 << 10)
#define OMAP3430_ST_MCBSP1_SHIFT			9
#define OMAP3430_ST_MCBSP1_MASK				(1 << 9)
#define OMAP3430ES1_ST_FSHOSTUSB_SHIFT			5
#define OMAP3430ES1_ST_FSHOSTUSB_MASK			(1 << 5)
#define OMAP3430ES1_ST_HSOTGUSB_SHIFT			4
#define OMAP3430ES1_ST_HSOTGUSB_MASK			(1 << 4)
#define OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT		5
#define OMAP3430ES2_ST_HSOTGUSB_IDLE_MASK		(1 << 5)
#define OMAP3430ES2_ST_HSOTGUSB_STDBY_SHIFT		4
#define OMAP3430ES2_ST_HSOTGUSB_STDBY_MASK		(1 << 4)
#define OMAP3430_ST_D2D_SHIFT				3
#define OMAP3430_ST_D2D_MASK				(1 << 3)

/* CM_FCLKEN_WKUP, CM_ICLKEN_WKUP, PM_WKEN_WKUP shared bits */
#define OMAP3430_EN_GPIO1_MASK				(1 << 3)
#define OMAP3430_EN_GPIO1_SHIFT				3
#define OMAP3430_EN_GPT12_MASK				(1 << 1)
#define OMAP3430_EN_GPT12_SHIFT				1
#define OMAP3430_EN_GPT1_MASK				(1 << 0)
#define OMAP3430_EN_GPT1_SHIFT				0

/* CM_FCLKEN_WKUP, PM_WKEN_WKUP shared bits */
#define OMAP3430_EN_SR2_MASK				(1 << 7)
#define OMAP3430_EN_SR2_SHIFT				7
#define OMAP3430_EN_SR1_MASK				(1 << 6)
#define OMAP3430_EN_SR1_SHIFT				6

/* CM_ICLKEN_WKUP, PM_WKEN_WKUP shared bits */
#define OMAP3430_EN_GPT12_MASK				(1 << 1)
#define OMAP3430_EN_GPT12_SHIFT				1

/* CM_IDLEST_WKUP, PM_WKST_WKUP shared bits */
#define OMAP3430_ST_SR2_SHIFT				7
#define OMAP3430_ST_SR2_MASK				(1 << 7)
#define OMAP3430_ST_SR1_SHIFT				6
#define OMAP3430_ST_SR1_MASK				(1 << 6)
#define OMAP3430_ST_GPIO1_SHIFT				3
#define OMAP3430_ST_GPIO1_MASK				(1 << 3)
#define OMAP3430_ST_GPT12_SHIFT				1
#define OMAP3430_ST_GPT12_MASK				(1 << 1)
#define OMAP3430_ST_GPT1_SHIFT				0
#define OMAP3430_ST_GPT1_MASK				(1 << 0)

/*
 * CM_SLEEPDEP_GFX, CM_SLEEPDEP_DSS, CM_SLEEPDEP_CAM,
 * CM_SLEEPDEP_PER, PM_WKDEP_IVA2, PM_WKDEP_GFX,
 * PM_WKDEP_DSS, PM_WKDEP_CAM, PM_WKDEP_PER, PM_WKDEP_NEON shared bits
 */
#define OMAP3430_EN_MPU_MASK				(1 << 1)
#define OMAP3430_EN_MPU_SHIFT				1

/* CM_FCLKEN_PER, CM_ICLKEN_PER, PM_WKEN_PER shared bits */

#define OMAP3630_EN_UART4_MASK				(1 << 18)
#define OMAP3630_EN_UART4_SHIFT				18
#define OMAP3430_EN_GPIO6_MASK				(1 << 17)
#define OMAP3430_EN_GPIO6_SHIFT				17
#define OMAP3430_EN_GPIO5_MASK				(1 << 16)
#define OMAP3430_EN_GPIO5_SHIFT				16
#define OMAP3430_EN_GPIO4_MASK				(1 << 15)
#define OMAP3430_EN_GPIO4_SHIFT				15
#define OMAP3430_EN_GPIO3_MASK				(1 << 14)
#define OMAP3430_EN_GPIO3_SHIFT				14
#define OMAP3430_EN_GPIO2_MASK				(1 << 13)
#define OMAP3430_EN_GPIO2_SHIFT				13
#define OMAP3430_EN_UART3_MASK				(1 << 11)
#define OMAP3430_EN_UART3_SHIFT				11
#define OMAP3430_EN_GPT9_MASK				(1 << 10)
#define OMAP3430_EN_GPT9_SHIFT				10
#define OMAP3430_EN_GPT8_MASK				(1 << 9)
#define OMAP3430_EN_GPT8_SHIFT				9
#define OMAP3430_EN_GPT7_MASK				(1 << 8)
#define OMAP3430_EN_GPT7_SHIFT				8
#define OMAP3430_EN_GPT6_MASK				(1 << 7)
#define OMAP3430_EN_GPT6_SHIFT				7
#define OMAP3430_EN_GPT5_MASK				(1 << 6)
#define OMAP3430_EN_GPT5_SHIFT				6
#define OMAP3430_EN_GPT4_MASK				(1 << 5)
#define OMAP3430_EN_GPT4_SHIFT				5
#define OMAP3430_EN_GPT3_MASK				(1 << 4)
#define OMAP3430_EN_GPT3_SHIFT				4
#define OMAP3430_EN_GPT2_MASK				(1 << 3)
#define OMAP3430_EN_GPT2_SHIFT				3

/* CM_FCLKEN_PER, CM_ICLKEN_PER, PM_WKEN_PER, PM_WKST_PER shared bits */
/* XXX Possible TI documentation bug: should the PM_WKST_PER EN_* bits
 * be ST_* bits instead? */
#define OMAP3430_EN_MCBSP4_MASK				(1 << 2)
#define OMAP3430_EN_MCBSP4_SHIFT			2
#define OMAP3430_EN_MCBSP3_MASK				(1 << 1)
#define OMAP3430_EN_MCBSP3_SHIFT			1
#define OMAP3430_EN_MCBSP2_MASK				(1 << 0)
#define OMAP3430_EN_MCBSP2_SHIFT			0

/* CM_IDLEST_PER, PM_WKST_PER shared bits */
#define OMAP3430_ST_GPIO6_SHIFT				17
#define OMAP3430_ST_GPIO6_MASK				(1 << 17)
#define OMAP3430_ST_GPIO5_SHIFT				16
#define OMAP3430_ST_GPIO5_MASK				(1 << 16)
#define OMAP3430_ST_GPIO4_SHIFT				15
#define OMAP3430_ST_GPIO4_MASK				(1 << 15)
#define OMAP3430_ST_GPIO3_SHIFT				14
#define OMAP3430_ST_GPIO3_MASK				(1 << 14)
#define OMAP3430_ST_GPIO2_SHIFT				13
#define OMAP3430_ST_GPIO2_MASK				(1 << 13)
#define OMAP3430_ST_UART3_SHIFT				11
#define OMAP3430_ST_UART3_MASK				(1 << 11)
#define OMAP3430_ST_GPT9_SHIFT				10
#define OMAP3430_ST_GPT9_MASK				(1 << 10)
#define OMAP3430_ST_GPT8_SHIFT				9
#define OMAP3430_ST_GPT8_MASK				(1 << 9)
#define OMAP3430_ST_GPT7_SHIFT				8
#define OMAP3430_ST_GPT7_MASK				(1 << 8)
#define OMAP3430_ST_GPT6_SHIFT				7
#define OMAP3430_ST_GPT6_MASK				(1 << 7)
#define OMAP3430_ST_GPT5_SHIFT				6
#define OMAP3430_ST_GPT5_MASK				(1 << 6)
#define OMAP3430_ST_GPT4_SHIFT				5
#define OMAP3430_ST_GPT4_MASK				(1 << 5)
#define OMAP3430_ST_GPT3_SHIFT				4
#define OMAP3430_ST_GPT3_MASK				(1 << 4)
#define OMAP3430_ST_GPT2_SHIFT				3
#define OMAP3430_ST_GPT2_MASK				(1 << 3)

/* CM_SLEEPDEP_PER, PM_WKDEP_IVA2, PM_WKDEP_MPU, PM_WKDEP_PER shared bits */
#define OMAP3430_EN_CORE_SHIFT				0
#define OMAP3430_EN_CORE_MASK				(1 << 0)

#endif

