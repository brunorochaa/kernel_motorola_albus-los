/*
 * as3722 definitions
 *
 * Copyright (C) 2013 ams
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 *
 * Author: Florian Lobmaier <florian.lobmaier@ams.com>
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __LINUX_MFD_AS3722_H__
#define __LINUX_MFD_AS3722_H__

#include <linux/regmap.h>

/* AS3722 registers */
#define AS3722_SD0_VOLTAGE_REG				0x00
#define AS3722_SD1_VOLTAGE_REG				0x01
#define AS3722_SD2_VOLTAGE_REG				0x02
#define AS3722_SD3_VOLTAGE_REG				0x03
#define AS3722_SD4_VOLTAGE_REG				0x04
#define AS3722_SD5_VOLTAGE_REG				0x05
#define AS3722_SD6_VOLTAGE_REG				0x06
#define AS3722_GPIO0_CONTROL_REG			0x08
#define AS3722_GPIO1_CONTROL_REG			0x09
#define AS3722_GPIO2_CONTROL_REG			0x0A
#define AS3722_GPIO3_CONTROL_REG			0x0B
#define AS3722_GPIO4_CONTROL_REG			0x0C
#define AS3722_GPIO5_CONTROL_REG			0x0D
#define AS3722_GPIO6_CONTROL_REG			0x0E
#define AS3722_GPIO7_CONTROL_REG			0x0F
#define AS3722_LDO0_VOLTAGE_REG				0x10
#define AS3722_LDO1_VOLTAGE_REG				0x11
#define AS3722_LDO2_VOLTAGE_REG				0x12
#define AS3722_LDO3_VOLTAGE_REG				0x13
#define AS3722_LDO4_VOLTAGE_REG				0x14
#define AS3722_LDO5_VOLTAGE_REG				0x15
#define AS3722_LDO6_VOLTAGE_REG				0x16
#define AS3722_LDO7_VOLTAGE_REG				0x17
#define AS3722_LDO9_VOLTAGE_REG				0x19
#define AS3722_LDO10_VOLTAGE_REG			0x1A
#define AS3722_LDO11_VOLTAGE_REG			0x1B
#define AS3722_GPIO_DEB1_REG				0x1E
#define AS3722_GPIO_DEB2_REG				0x1F
#define AS3722_GPIO_SIGNAL_OUT_REG			0x20
#define AS3722_GPIO_SIGNAL_IN_REG			0x21
#define AS3722_REG_SEQU_MOD1_REG			0x22
#define AS3722_REG_SEQU_MOD2_REG			0x23
#define AS3722_REG_SEQU_MOD3_REG			0x24
#define AS3722_SD_PHSW_CTRL_REG				0x27
#define AS3722_SD_PHSW_STATUS				0x28
#define AS3722_SD0_CONTROL_REG				0x29
#define AS3722_SD1_CONTROL_REG				0x2A
#define AS3722_SDmph_CONTROL_REG			0x2B
#define AS3722_SD23_CONTROL_REG				0x2C
#define AS3722_SD4_CONTROL_REG				0x2D
#define AS3722_SD5_CONTROL_REG				0x2E
#define AS3722_SD6_CONTROL_REG				0x2F
#define AS3722_SD_DVM_REG				0x30
#define AS3722_RESET_REASON_REG				0x31
#define AS3722_BATTERY_VOLTAGE_MONITOR_REG		0x32
#define AS3722_STARTUP_CONTROL_REG			0x33
#define AS3722_RESET_TIMER_REG				0x34
#define AS3722_REFERENCE_CONTROL_REG			0x35
#define AS3722_RESET_CONTROL_REG			0x36
#define AS3722_OVER_TEMP_CONTROL_REG			0x37
#define AS3722_WATCHDOG_CONTROL_REG			0x38
#define AS3722_REG_STANDBY_MOD1_REG			0x39
#define AS3722_REG_STANDBY_MOD2_REG			0x3A
#define AS3722_REG_STANDBY_MOD3_REG			0x3B
#define AS3722_ENABLE_CTRL1_REG				0x3C
#define AS3722_ENABLE_CTRL2_REG				0x3D
#define AS3722_ENABLE_CTRL3_REG				0x3E
#define AS3722_ENABLE_CTRL4_REG				0x3F
#define AS3722_ENABLE_CTRL5_REG				0x40
#define AS3722_PWM_CONTROL_L_REG			0x41
#define AS3722_PWM_CONTROL_H_REG			0x42
#define AS3722_WATCHDOG_TIMER_REG			0x46
#define AS3722_WATCHDOG_SOFTWARE_SIGNAL_REG		0x48
#define AS3722_IOVOLTAGE_REG				0x49
#define AS3722_BATTERY_VOLTAGE_MONITOR2_REG		0x4A
#define AS3722_SD_CONTROL_REG				0x4D
#define AS3722_LDOCONTROL0_REG				0x4E
#define AS3722_LDOCONTROL1_REG				0x4F
#define AS3722_SD0_PROTECT_REG				0x50
#define AS3722_SD6_PROTECT_REG				0x51
#define AS3722_PWM_VCONTROL1_REG			0x52
#define AS3722_PWM_VCONTROL2_REG			0x53
#define AS3722_PWM_VCONTROL3_REG			0x54
#define AS3722_PWM_VCONTROL4_REG			0x55
#define AS3722_BB_CHARGER_REG				0x57
#define AS3722_CTRL_SEQU1_REG				0x58
#define AS3722_CTRL_SEQU2_REG				0x59
#define AS3722_OVCURRENT_REG				0x5A
#define AS3722_OVCURRENT_DEB_REG			0x5B
#define AS3722_SDLV_DEB_REG				0x5C
#define AS3722_OC_PG_CTRL_REG				0x5D
#define AS3722_OC_PG_CTRL2_REG				0x5E
#define AS3722_CTRL_STATUS				0x5F
#define AS3722_RTC_CONTROL_REG				0x60
#define AS3722_RTC_SECOND_REG				0x61
#define AS3722_RTC_MINUTE_REG				0x62
#define AS3722_RTC_HOUR_REG				0x63
#define AS3722_RTC_DAY_REG				0x64
#define AS3722_RTC_MONTH_REG				0x65
#define AS3722_RTC_YEAR_REG				0x66
#define AS3722_RTC_ALARM_SECOND_REG			0x67
#define AS3722_RTC_ALARM_MINUTE_REG			0x68
#define AS3722_RTC_ALARM_HOUR_REG			0x69
#define AS3722_RTC_ALARM_DAY_REG			0x6A
#define AS3722_RTC_ALARM_MONTH_REG			0x6B
#define AS3722_RTC_ALARM_YEAR_REG			0x6C
#define AS3722_SRAM_REG					0x6D
#define AS3722_RTC_ACCESS_REG				0x6F
#define AS3722_RTC_STATUS_REG				0x73
#define AS3722_INTERRUPT_MASK1_REG			0x74
#define AS3722_INTERRUPT_MASK2_REG			0x75
#define AS3722_INTERRUPT_MASK3_REG			0x76
#define AS3722_INTERRUPT_MASK4_REG			0x77
#define AS3722_INTERRUPT_STATUS1_REG			0x78
#define AS3722_INTERRUPT_STATUS2_REG			0x79
#define AS3722_INTERRUPT_STATUS3_REG			0x7A
#define AS3722_INTERRUPT_STATUS4_REG			0x7B
#define AS3722_TEMP_STATUS_REG				0x7D
#define AS3722_ADC0_CONTROL_REG				0x80
#define AS3722_ADC1_CONTROL_REG				0x81
#define AS3722_ADC0_MSB_RESULT_REG			0x82
#define AS3722_ADC0_LSB_RESULT_REG			0x83
#define AS3722_ADC1_MSB_RESULT_REG			0x84
#define AS3722_ADC1_LSB_RESULT_REG			0x85
#define AS3722_ADC1_THRESHOLD_HI_MSB_REG		0x86
#define AS3722_ADC1_THRESHOLD_HI_LSB_REG		0x87
#define AS3722_ADC1_THRESHOLD_LO_MSB_REG		0x88
#define AS3722_ADC1_THRESHOLD_LO_LSB_REG		0x89
#define AS3722_ADC_CONFIGURATION_REG			0x8A
#define AS3722_ASIC_ID1_REG				0x90
#define AS3722_ASIC_ID2_REG				0x91
#define AS3722_LOCK_REG					0x9E
#define AS3722_MAX_REGISTER				0xF4

#define AS3722_SD0_EXT_ENABLE_MASK			0x03
#define AS3722_SD1_EXT_ENABLE_MASK			0x0C
#define AS3722_SD2_EXT_ENABLE_MASK			0x30
#define AS3722_SD3_EXT_ENABLE_MASK			0xC0
#define AS3722_SD4_EXT_ENABLE_MASK			0x03
#define AS3722_SD5_EXT_ENABLE_MASK			0x0C
#define AS3722_SD6_EXT_ENABLE_MASK			0x30
#define AS3722_LDO0_EXT_ENABLE_MASK			0x03
#define AS3722_LDO1_EXT_ENABLE_MASK			0x0C
#define AS3722_LDO2_EXT_ENABLE_MASK			0x30
#define AS3722_LDO3_EXT_ENABLE_MASK			0xC0
#define AS3722_LDO4_EXT_ENABLE_MASK			0x03
#define AS3722_LDO5_EXT_ENABLE_MASK			0x0C
#define AS3722_LDO6_EXT_ENABLE_MASK			0x30
#define AS3722_LDO7_EXT_ENABLE_MASK			0xC0
#define AS3722_LDO9_EXT_ENABLE_MASK			0x0C
#define AS3722_LDO10_EXT_ENABLE_MASK			0x30
#define AS3722_LDO11_EXT_ENABLE_MASK			0xC0

#define AS3722_OVCURRENT_SD0_ALARM_MASK			0x07
#define AS3722_OVCURRENT_SD0_ALARM_SHIFT		0x01
#define AS3722_OVCURRENT_SD0_TRIP_MASK			0x18
#define AS3722_OVCURRENT_SD0_TRIP_SHIFT			0x03
#define AS3722_OVCURRENT_SD1_TRIP_MASK			0x60
#define AS3722_OVCURRENT_SD1_TRIP_SHIFT			0x05

#define AS3722_OVCURRENT_SD6_ALARM_MASK			0x07
#define AS3722_OVCURRENT_SD6_ALARM_SHIFT		0x01
#define AS3722_OVCURRENT_SD6_TRIP_MASK			0x18
#define AS3722_OVCURRENT_SD6_TRIP_SHIFT			0x03

/* AS3722 register bits and bit masks */
#define AS3722_LDO_ILIMIT_MASK				BIT(7)
#define AS3722_LDO_ILIMIT_BIT				BIT(7)
#define AS3722_LDO0_VSEL_MASK				0x1F
#define AS3722_LDO0_VSEL_MIN				0x01
#define AS3722_LDO0_VSEL_MAX				0x12
#define AS3722_LDO0_NUM_VOLT				0x12
#define AS3722_LDO3_VSEL_MASK				0x3F
#define AS3722_LDO3_VSEL_MIN				0x01
#define AS3722_LDO3_VSEL_MAX				0x2D
#define AS3722_LDO3_NUM_VOLT				0x2D
#define AS3722_LDO_VSEL_MASK				0x7F
#define AS3722_LDO_VSEL_MIN				0x01
#define AS3722_LDO_VSEL_MAX				0x7F
#define AS3722_LDO_VSEL_DNU_MIN				0x25
#define AS3722_LDO_VSEL_DNU_MAX				0x3F
#define AS3722_LDO_NUM_VOLT				0x80

#define AS3722_LDO0_CTRL				BIT(0)
#define AS3722_LDO1_CTRL				BIT(1)
#define AS3722_LDO2_CTRL				BIT(2)
#define AS3722_LDO3_CTRL				BIT(3)
#define AS3722_LDO4_CTRL				BIT(4)
#define AS3722_LDO5_CTRL				BIT(5)
#define AS3722_LDO6_CTRL				BIT(6)
#define AS3722_LDO7_CTRL				BIT(7)
#define AS3722_LDO9_CTRL				BIT(1)
#define AS3722_LDO10_CTRL				BIT(2)
#define AS3722_LDO11_CTRL				BIT(3)

#define AS3722_LDO3_MODE_MASK				(3 << 6)
#define AS3722_LDO3_MODE_VAL(n)				(((n) & 0x3) << 6)
#define AS3722_LDO3_MODE_PMOS				AS3722_LDO3_MODE_VAL(0)
#define AS3722_LDO3_MODE_PMOS_TRACKING			AS3722_LDO3_MODE_VAL(1)
#define AS3722_LDO3_MODE_NMOS				AS3722_LDO3_MODE_VAL(2)
#define AS3722_LDO3_MODE_SWITCH				AS3722_LDO3_MODE_VAL(3)

#define AS3722_SD_VSEL_MASK				0x7F
#define AS3722_SD0_VSEL_MIN				0x01
#define AS3722_SD0_VSEL_MAX				0x5A
#define AS3722_SD2_VSEL_MIN				0x01
#define AS3722_SD2_VSEL_MAX				0x7F

#define AS3722_SDn_CTRL(n)				BIT(n)

#define AS3722_SD0_MODE_FAST				BIT(4)
#define AS3722_SD1_MODE_FAST				BIT(4)
#define AS3722_SD2_MODE_FAST				BIT(2)
#define AS3722_SD3_MODE_FAST				BIT(6)
#define AS3722_SD4_MODE_FAST				BIT(2)
#define AS3722_SD5_MODE_FAST				BIT(2)
#define AS3722_SD6_MODE_FAST				BIT(4)

#define AS3722_POWER_OFF				BIT(1)

#define AS3722_INTERRUPT_MASK1_LID			BIT(0)
#define AS3722_INTERRUPT_MASK1_ACOK			BIT(1)
#define AS3722_INTERRUPT_MASK1_ENABLE1			BIT(2)
#define AS3722_INTERRUPT_MASK1_OCURR_ALARM_SD0		BIT(3)
#define AS3722_INTERRUPT_MASK1_ONKEY_LONG		BIT(4)
#define AS3722_INTERRUPT_MASK1_ONKEY			BIT(5)
#define AS3722_INTERRUPT_MASK1_OVTMP			BIT(6)
#define AS3722_INTERRUPT_MASK1_LOWBAT			BIT(7)

#define AS3722_INTERRUPT_MASK2_SD0_LV			BIT(0)
#define AS3722_INTERRUPT_MASK2_SD1_LV			BIT(1)
#define AS3722_INTERRUPT_MASK2_SD2345_LV		BIT(2)
#define AS3722_INTERRUPT_MASK2_PWM1_OV_PROT		BIT(3)
#define AS3722_INTERRUPT_MASK2_PWM2_OV_PROT		BIT(4)
#define AS3722_INTERRUPT_MASK2_ENABLE2			BIT(5)
#define AS3722_INTERRUPT_MASK2_SD6_LV			BIT(6)
#define AS3722_INTERRUPT_MASK2_RTC_REP			BIT(7)

#define AS3722_INTERRUPT_MASK3_RTC_ALARM		BIT(0)
#define AS3722_INTERRUPT_MASK3_GPIO1			BIT(1)
#define AS3722_INTERRUPT_MASK3_GPIO2			BIT(2)
#define AS3722_INTERRUPT_MASK3_GPIO3			BIT(3)
#define AS3722_INTERRUPT_MASK3_GPIO4			BIT(4)
#define AS3722_INTERRUPT_MASK3_GPIO5			BIT(5)
#define AS3722_INTERRUPT_MASK3_WATCHDOG			BIT(6)
#define AS3722_INTERRUPT_MASK3_ENABLE3			BIT(7)

#define AS3722_INTERRUPT_MASK4_TEMP_SD0_SHUTDOWN	BIT(0)
#define AS3722_INTERRUPT_MASK4_TEMP_SD1_SHUTDOWN	BIT(1)
#define AS3722_INTERRUPT_MASK4_TEMP_SD6_SHUTDOWN	BIT(2)
#define AS3722_INTERRUPT_MASK4_TEMP_SD0_ALARM		BIT(3)
#define AS3722_INTERRUPT_MASK4_TEMP_SD1_ALARM		BIT(4)
#define AS3722_INTERRUPT_MASK4_TEMP_SD6_ALARM		BIT(5)
#define AS3722_INTERRUPT_MASK4_OCCUR_ALARM_SD6		BIT(6)
#define AS3722_INTERRUPT_MASK4_ADC			BIT(7)

#define AS3722_ADC1_INTERVAL_TIME			BIT(0)
#define AS3722_ADC1_INT_MODE_ON				BIT(1)
#define AS3722_ADC_BUF_ON				BIT(2)
#define AS3722_ADC1_LOW_VOLTAGE_RANGE			BIT(5)
#define AS3722_ADC1_INTEVAL_SCAN			BIT(6)
#define AS3722_ADC1_INT_MASK				BIT(7)

#define AS3722_ADC_MSB_VAL_MASK				0x7F
#define AS3722_ADC_LSB_VAL_MASK				0x07

#define AS3722_ADC0_CONV_START				BIT(7)
#define AS3722_ADC0_CONV_NOTREADY			BIT(7)
#define AS3722_ADC0_SOURCE_SELECT_MASK			0x1F

#define AS3722_ADC1_CONV_START				BIT(7)
#define AS3722_ADC1_CONV_NOTREADY			BIT(7)
#define AS3722_ADC1_SOURCE_SELECT_MASK			0x1F

/* GPIO modes */
#define AS3722_GPIO_MODE_MASK				0x07
#define AS3722_GPIO_MODE_INPUT				0x00
#define AS3722_GPIO_MODE_OUTPUT_VDDH			0x01
#define AS3722_GPIO_MODE_IO_OPEN_DRAIN			0x02
#define AS3722_GPIO_MODE_ADC_IN				0x03
#define AS3722_GPIO_MODE_INPUT_PULL_UP			0x04
#define AS3722_GPIO_MODE_INPUT_PULL_DOWN		0x05
#define AS3722_GPIO_MODE_IO_OPEN_DRAIN_PULL_UP		0x06
#define AS3722_GPIO_MODE_OUTPUT_VDDL			0x07
#define AS3722_GPIO_MODE_VAL(n)			((n) & AS3722_GPIO_MODE_MASK)

#define AS3722_GPIO_INV					BIT(7)
#define AS3722_GPIO_IOSF_MASK				0x78
#define AS3722_GPIO_IOSF_VAL(n)				(((n) & 0xF) << 3)
#define AS3722_GPIO_IOSF_NORMAL				AS3722_GPIO_IOSF_VAL(0)
#define AS3722_GPIO_IOSF_INTERRUPT_OUT			AS3722_GPIO_IOSF_VAL(1)
#define AS3722_GPIO_IOSF_VSUP_LOW_OUT			AS3722_GPIO_IOSF_VAL(2)
#define AS3722_GPIO_IOSF_GPIO_INTERRUPT_IN		AS3722_GPIO_IOSF_VAL(3)
#define AS3722_GPIO_IOSF_ISINK_PWM_IN			AS3722_GPIO_IOSF_VAL(4)
#define AS3722_GPIO_IOSF_VOLTAGE_STBY			AS3722_GPIO_IOSF_VAL(5)
#define AS3722_GPIO_IOSF_SD0_OUT			AS3722_GPIO_IOSF_VAL(6)
#define AS3722_GPIO_IOSF_PWR_GOOD_OUT			AS3722_GPIO_IOSF_VAL(7)
#define AS3722_GPIO_IOSF_Q32K_OUT			AS3722_GPIO_IOSF_VAL(8)
#define AS3722_GPIO_IOSF_WATCHDOG_IN			AS3722_GPIO_IOSF_VAL(9)
#define AS3722_GPIO_IOSF_SOFT_RESET_IN			AS3722_GPIO_IOSF_VAL(11)
#define AS3722_GPIO_IOSF_PWM_OUT			AS3722_GPIO_IOSF_VAL(12)
#define AS3722_GPIO_IOSF_VSUP_LOW_DEB_OUT		AS3722_GPIO_IOSF_VAL(13)
#define AS3722_GPIO_IOSF_SD6_LOW_VOLT_LOW		AS3722_GPIO_IOSF_VAL(14)

#define AS3722_GPIOn_SIGNAL(n)				BIT(n)
#define AS3722_GPIOn_CONTROL_REG(n)		(AS3722_GPIO0_CONTROL_REG + n)
#define AS3722_I2C_PULL_UP				BIT(4)
#define AS3722_INT_PULL_UP				BIT(5)

#define AS3722_RTC_REP_WAKEUP_EN			BIT(0)
#define AS3722_RTC_ALARM_WAKEUP_EN			BIT(1)
#define AS3722_RTC_ON					BIT(2)
#define AS3722_RTC_IRQMODE				BIT(3)
#define AS3722_RTC_CLK32K_OUT_EN			BIT(5)

#define AS3722_WATCHDOG_TIMER_MAX			0x7F
#define AS3722_WATCHDOG_ON				BIT(0)
#define AS3722_WATCHDOG_SW_SIG				BIT(0)

#define AS3722_EXT_CONTROL_ENABLE1			0x1
#define AS3722_EXT_CONTROL_ENABLE2			0x2
#define AS3722_EXT_CONTROL_ENABLE3			0x3

/* Interrupt IDs */
enum as3722_irq {
	AS3722_IRQ_LID,
	AS3722_IRQ_ACOK,
	AS3722_IRQ_ENABLE1,
	AS3722_IRQ_OCCUR_ALARM_SD0,
	AS3722_IRQ_ONKEY_LONG_PRESS,
	AS3722_IRQ_ONKEY,
	AS3722_IRQ_OVTMP,
	AS3722_IRQ_LOWBAT,
	AS3722_IRQ_SD0_LV,
	AS3722_IRQ_SD1_LV,
	AS3722_IRQ_SD2_LV,
	AS3722_IRQ_PWM1_OV_PROT,
	AS3722_IRQ_PWM2_OV_PROT,
	AS3722_IRQ_ENABLE2,
	AS3722_IRQ_SD6_LV,
	AS3722_IRQ_RTC_REP,
	AS3722_IRQ_RTC_ALARM,
	AS3722_IRQ_GPIO1,
	AS3722_IRQ_GPIO2,
	AS3722_IRQ_GPIO3,
	AS3722_IRQ_GPIO4,
	AS3722_IRQ_GPIO5,
	AS3722_IRQ_WATCHDOG,
	AS3722_IRQ_ENABLE3,
	AS3722_IRQ_TEMP_SD0_SHUTDOWN,
	AS3722_IRQ_TEMP_SD1_SHUTDOWN,
	AS3722_IRQ_TEMP_SD2_SHUTDOWN,
	AS3722_IRQ_TEMP_SD0_ALARM,
	AS3722_IRQ_TEMP_SD1_ALARM,
	AS3722_IRQ_TEMP_SD6_ALARM,
	AS3722_IRQ_OCCUR_ALARM_SD6,
	AS3722_IRQ_ADC,
	AS3722_IRQ_MAX,
};

struct as3722 {
	struct device *dev;
	struct regmap *regmap;
	int chip_irq;
	unsigned long irq_flags;
	bool en_intern_int_pullup;
	bool en_intern_i2c_pullup;
	struct regmap_irq_chip_data *irq_data;
};

static inline int as3722_read(struct as3722 *as3722, u32 reg, u32 *dest)
{
	return regmap_read(as3722->regmap, reg, dest);
}

static inline int as3722_write(struct as3722 *as3722, u32 reg, u32 value)
{
	return regmap_write(as3722->regmap, reg, value);
}

static inline int as3722_block_read(struct as3722 *as3722, u32 reg,
		int count, u8 *buf)
{
	return regmap_bulk_read(as3722->regmap, reg, buf, count);
}

static inline int as3722_block_write(struct as3722 *as3722, u32 reg,
		int count, u8 *data)
{
	return regmap_bulk_write(as3722->regmap, reg, data, count);
}

static inline int as3722_update_bits(struct as3722 *as3722, u32 reg,
		u32 mask, u8 val)
{
	return regmap_update_bits(as3722->regmap, reg, mask, val);
}

static inline int as3722_irq_get_virq(struct as3722 *as3722, int irq)
{
	return regmap_irq_get_virq(as3722->irq_data, irq);
}
#endif /* __LINUX_MFD_AS3722_H__ */
