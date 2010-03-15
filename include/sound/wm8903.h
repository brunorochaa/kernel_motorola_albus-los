/*
 * linux/sound/wm8903.h -- Platform data for WM8903
 *
 * Copyright 2010 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_WM8903_H
#define __LINUX_SND_WM8903_H

/* Used to enable configuration of a GPIO to all zeros */
#define WM8903_GPIO_NO_CONFIG 0x8000

/*
 * R116 (0x74) - GPIO Control 1
 */
#define WM8903_GP1_FN_MASK                      0x1F00  /* GP1_FN - [12:8] */
#define WM8903_GP1_FN_SHIFT                          8  /* GP1_FN - [12:8] */
#define WM8903_GP1_FN_WIDTH                          5  /* GP1_FN - [12:8] */
#define WM8903_GP1_DIR                          0x0080  /* GP1_DIR */
#define WM8903_GP1_DIR_MASK                     0x0080  /* GP1_DIR */
#define WM8903_GP1_DIR_SHIFT                         7  /* GP1_DIR */
#define WM8903_GP1_DIR_WIDTH                         1  /* GP1_DIR */
#define WM8903_GP1_OP_CFG                       0x0040  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_MASK                  0x0040  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_SHIFT                      6  /* GP1_OP_CFG */
#define WM8903_GP1_OP_CFG_WIDTH                      1  /* GP1_OP_CFG */
#define WM8903_GP1_IP_CFG                       0x0020  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_MASK                  0x0020  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_SHIFT                      5  /* GP1_IP_CFG */
#define WM8903_GP1_IP_CFG_WIDTH                      1  /* GP1_IP_CFG */
#define WM8903_GP1_LVL                          0x0010  /* GP1_LVL */
#define WM8903_GP1_LVL_MASK                     0x0010  /* GP1_LVL */
#define WM8903_GP1_LVL_SHIFT                         4  /* GP1_LVL */
#define WM8903_GP1_LVL_WIDTH                         1  /* GP1_LVL */
#define WM8903_GP1_PD                           0x0008  /* GP1_PD */
#define WM8903_GP1_PD_MASK                      0x0008  /* GP1_PD */
#define WM8903_GP1_PD_SHIFT                          3  /* GP1_PD */
#define WM8903_GP1_PD_WIDTH                          1  /* GP1_PD */
#define WM8903_GP1_PU                           0x0004  /* GP1_PU */
#define WM8903_GP1_PU_MASK                      0x0004  /* GP1_PU */
#define WM8903_GP1_PU_SHIFT                          2  /* GP1_PU */
#define WM8903_GP1_PU_WIDTH                          1  /* GP1_PU */
#define WM8903_GP1_INTMODE                      0x0002  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_MASK                 0x0002  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_SHIFT                     1  /* GP1_INTMODE */
#define WM8903_GP1_INTMODE_WIDTH                     1  /* GP1_INTMODE */
#define WM8903_GP1_DB                           0x0001  /* GP1_DB */
#define WM8903_GP1_DB_MASK                      0x0001  /* GP1_DB */
#define WM8903_GP1_DB_SHIFT                          0  /* GP1_DB */
#define WM8903_GP1_DB_WIDTH                          1  /* GP1_DB */

/*
 * R117 (0x75) - GPIO Control 2
 */
#define WM8903_GP2_FN_MASK                      0x1F00  /* GP2_FN - [12:8] */
#define WM8903_GP2_FN_SHIFT                          8  /* GP2_FN - [12:8] */
#define WM8903_GP2_FN_WIDTH                          5  /* GP2_FN - [12:8] */
#define WM8903_GP2_DIR                          0x0080  /* GP2_DIR */
#define WM8903_GP2_DIR_MASK                     0x0080  /* GP2_DIR */
#define WM8903_GP2_DIR_SHIFT                         7  /* GP2_DIR */
#define WM8903_GP2_DIR_WIDTH                         1  /* GP2_DIR */
#define WM8903_GP2_OP_CFG                       0x0040  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_MASK                  0x0040  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_SHIFT                      6  /* GP2_OP_CFG */
#define WM8903_GP2_OP_CFG_WIDTH                      1  /* GP2_OP_CFG */
#define WM8903_GP2_IP_CFG                       0x0020  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_MASK                  0x0020  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_SHIFT                      5  /* GP2_IP_CFG */
#define WM8903_GP2_IP_CFG_WIDTH                      1  /* GP2_IP_CFG */
#define WM8903_GP2_LVL                          0x0010  /* GP2_LVL */
#define WM8903_GP2_LVL_MASK                     0x0010  /* GP2_LVL */
#define WM8903_GP2_LVL_SHIFT                         4  /* GP2_LVL */
#define WM8903_GP2_LVL_WIDTH                         1  /* GP2_LVL */
#define WM8903_GP2_PD                           0x0008  /* GP2_PD */
#define WM8903_GP2_PD_MASK                      0x0008  /* GP2_PD */
#define WM8903_GP2_PD_SHIFT                          3  /* GP2_PD */
#define WM8903_GP2_PD_WIDTH                          1  /* GP2_PD */
#define WM8903_GP2_PU                           0x0004  /* GP2_PU */
#define WM8903_GP2_PU_MASK                      0x0004  /* GP2_PU */
#define WM8903_GP2_PU_SHIFT                          2  /* GP2_PU */
#define WM8903_GP2_PU_WIDTH                          1  /* GP2_PU */
#define WM8903_GP2_INTMODE                      0x0002  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_MASK                 0x0002  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_SHIFT                     1  /* GP2_INTMODE */
#define WM8903_GP2_INTMODE_WIDTH                     1  /* GP2_INTMODE */
#define WM8903_GP2_DB                           0x0001  /* GP2_DB */
#define WM8903_GP2_DB_MASK                      0x0001  /* GP2_DB */
#define WM8903_GP2_DB_SHIFT                          0  /* GP2_DB */
#define WM8903_GP2_DB_WIDTH                          1  /* GP2_DB */

/*
 * R118 (0x76) - GPIO Control 3
 */
#define WM8903_GP3_FN_MASK                      0x1F00  /* GP3_FN - [12:8] */
#define WM8903_GP3_FN_SHIFT                          8  /* GP3_FN - [12:8] */
#define WM8903_GP3_FN_WIDTH                          5  /* GP3_FN - [12:8] */
#define WM8903_GP3_DIR                          0x0080  /* GP3_DIR */
#define WM8903_GP3_DIR_MASK                     0x0080  /* GP3_DIR */
#define WM8903_GP3_DIR_SHIFT                         7  /* GP3_DIR */
#define WM8903_GP3_DIR_WIDTH                         1  /* GP3_DIR */
#define WM8903_GP3_OP_CFG                       0x0040  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_MASK                  0x0040  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_SHIFT                      6  /* GP3_OP_CFG */
#define WM8903_GP3_OP_CFG_WIDTH                      1  /* GP3_OP_CFG */
#define WM8903_GP3_IP_CFG                       0x0020  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_MASK                  0x0020  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_SHIFT                      5  /* GP3_IP_CFG */
#define WM8903_GP3_IP_CFG_WIDTH                      1  /* GP3_IP_CFG */
#define WM8903_GP3_LVL                          0x0010  /* GP3_LVL */
#define WM8903_GP3_LVL_MASK                     0x0010  /* GP3_LVL */
#define WM8903_GP3_LVL_SHIFT                         4  /* GP3_LVL */
#define WM8903_GP3_LVL_WIDTH                         1  /* GP3_LVL */
#define WM8903_GP3_PD                           0x0008  /* GP3_PD */
#define WM8903_GP3_PD_MASK                      0x0008  /* GP3_PD */
#define WM8903_GP3_PD_SHIFT                          3  /* GP3_PD */
#define WM8903_GP3_PD_WIDTH                          1  /* GP3_PD */
#define WM8903_GP3_PU                           0x0004  /* GP3_PU */
#define WM8903_GP3_PU_MASK                      0x0004  /* GP3_PU */
#define WM8903_GP3_PU_SHIFT                          2  /* GP3_PU */
#define WM8903_GP3_PU_WIDTH                          1  /* GP3_PU */
#define WM8903_GP3_INTMODE                      0x0002  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_MASK                 0x0002  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_SHIFT                     1  /* GP3_INTMODE */
#define WM8903_GP3_INTMODE_WIDTH                     1  /* GP3_INTMODE */
#define WM8903_GP3_DB                           0x0001  /* GP3_DB */
#define WM8903_GP3_DB_MASK                      0x0001  /* GP3_DB */
#define WM8903_GP3_DB_SHIFT                          0  /* GP3_DB */
#define WM8903_GP3_DB_WIDTH                          1  /* GP3_DB */

/*
 * R119 (0x77) - GPIO Control 4
 */
#define WM8903_GP4_FN_MASK                      0x1F00  /* GP4_FN - [12:8] */
#define WM8903_GP4_FN_SHIFT                          8  /* GP4_FN - [12:8] */
#define WM8903_GP4_FN_WIDTH                          5  /* GP4_FN - [12:8] */
#define WM8903_GP4_DIR                          0x0080  /* GP4_DIR */
#define WM8903_GP4_DIR_MASK                     0x0080  /* GP4_DIR */
#define WM8903_GP4_DIR_SHIFT                         7  /* GP4_DIR */
#define WM8903_GP4_DIR_WIDTH                         1  /* GP4_DIR */
#define WM8903_GP4_OP_CFG                       0x0040  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_MASK                  0x0040  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_SHIFT                      6  /* GP4_OP_CFG */
#define WM8903_GP4_OP_CFG_WIDTH                      1  /* GP4_OP_CFG */
#define WM8903_GP4_IP_CFG                       0x0020  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_MASK                  0x0020  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_SHIFT                      5  /* GP4_IP_CFG */
#define WM8903_GP4_IP_CFG_WIDTH                      1  /* GP4_IP_CFG */
#define WM8903_GP4_LVL                          0x0010  /* GP4_LVL */
#define WM8903_GP4_LVL_MASK                     0x0010  /* GP4_LVL */
#define WM8903_GP4_LVL_SHIFT                         4  /* GP4_LVL */
#define WM8903_GP4_LVL_WIDTH                         1  /* GP4_LVL */
#define WM8903_GP4_PD                           0x0008  /* GP4_PD */
#define WM8903_GP4_PD_MASK                      0x0008  /* GP4_PD */
#define WM8903_GP4_PD_SHIFT                          3  /* GP4_PD */
#define WM8903_GP4_PD_WIDTH                          1  /* GP4_PD */
#define WM8903_GP4_PU                           0x0004  /* GP4_PU */
#define WM8903_GP4_PU_MASK                      0x0004  /* GP4_PU */
#define WM8903_GP4_PU_SHIFT                          2  /* GP4_PU */
#define WM8903_GP4_PU_WIDTH                          1  /* GP4_PU */
#define WM8903_GP4_INTMODE                      0x0002  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_MASK                 0x0002  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_SHIFT                     1  /* GP4_INTMODE */
#define WM8903_GP4_INTMODE_WIDTH                     1  /* GP4_INTMODE */
#define WM8903_GP4_DB                           0x0001  /* GP4_DB */
#define WM8903_GP4_DB_MASK                      0x0001  /* GP4_DB */
#define WM8903_GP4_DB_SHIFT                          0  /* GP4_DB */
#define WM8903_GP4_DB_WIDTH                          1  /* GP4_DB */

/*
 * R120 (0x78) - GPIO Control 5
 */
#define WM8903_GP5_FN_MASK                      0x1F00  /* GP5_FN - [12:8] */
#define WM8903_GP5_FN_SHIFT                          8  /* GP5_FN - [12:8] */
#define WM8903_GP5_FN_WIDTH                          5  /* GP5_FN - [12:8] */
#define WM8903_GP5_DIR                          0x0080  /* GP5_DIR */
#define WM8903_GP5_DIR_MASK                     0x0080  /* GP5_DIR */
#define WM8903_GP5_DIR_SHIFT                         7  /* GP5_DIR */
#define WM8903_GP5_DIR_WIDTH                         1  /* GP5_DIR */
#define WM8903_GP5_OP_CFG                       0x0040  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_MASK                  0x0040  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_SHIFT                      6  /* GP5_OP_CFG */
#define WM8903_GP5_OP_CFG_WIDTH                      1  /* GP5_OP_CFG */
#define WM8903_GP5_IP_CFG                       0x0020  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_MASK                  0x0020  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_SHIFT                      5  /* GP5_IP_CFG */
#define WM8903_GP5_IP_CFG_WIDTH                      1  /* GP5_IP_CFG */
#define WM8903_GP5_LVL                          0x0010  /* GP5_LVL */
#define WM8903_GP5_LVL_MASK                     0x0010  /* GP5_LVL */
#define WM8903_GP5_LVL_SHIFT                         4  /* GP5_LVL */
#define WM8903_GP5_LVL_WIDTH                         1  /* GP5_LVL */
#define WM8903_GP5_PD                           0x0008  /* GP5_PD */
#define WM8903_GP5_PD_MASK                      0x0008  /* GP5_PD */
#define WM8903_GP5_PD_SHIFT                          3  /* GP5_PD */
#define WM8903_GP5_PD_WIDTH                          1  /* GP5_PD */
#define WM8903_GP5_PU                           0x0004  /* GP5_PU */
#define WM8903_GP5_PU_MASK                      0x0004  /* GP5_PU */
#define WM8903_GP5_PU_SHIFT                          2  /* GP5_PU */
#define WM8903_GP5_PU_WIDTH                          1  /* GP5_PU */
#define WM8903_GP5_INTMODE                      0x0002  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_MASK                 0x0002  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_SHIFT                     1  /* GP5_INTMODE */
#define WM8903_GP5_INTMODE_WIDTH                     1  /* GP5_INTMODE */
#define WM8903_GP5_DB                           0x0001  /* GP5_DB */
#define WM8903_GP5_DB_MASK                      0x0001  /* GP5_DB */
#define WM8903_GP5_DB_SHIFT                          0  /* GP5_DB */
#define WM8903_GP5_DB_WIDTH                          1  /* GP5_DB */

struct wm8903_platform_data {
	u32 gpio_cfg[5];       /* Default register values for GPIO pin mux */
};

#endif
