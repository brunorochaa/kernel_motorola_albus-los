#ifndef __config_defs_h
#define __config_defs_h

/*
 * This file is autogenerated from
 *   file:           ../../rtl/config_regs.r
 *     id:           config_regs.r,v 1.23 2004/03/04 11:34:42 mikaeln Exp
 *     last modfied: Thu Mar  4 12:34:39 2004
 *
 *   by /n/asic/design/tools/rdesc/src/rdes2c --outfile config_defs.h ../../rtl/config_regs.r
 *      id: $Id: config_defs.h,v 1.6 2005/04/24 18:30:58 starvik Exp $
 * Any changes here will be lost.
 *
 * -*- buffer-read-only: t -*-
 */
/* Main access macros */
#ifndef REG_RD
#define REG_RD( scope, inst, reg ) \
  REG_READ( reg_##scope##_##reg, \
            (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_WR
#define REG_WR( scope, inst, reg, val ) \
  REG_WRITE( reg_##scope##_##reg, \
             (inst) + REG_WR_ADDR_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_VECT
#define REG_RD_VECT( scope, inst, reg, index ) \
  REG_READ( reg_##scope##_##reg, \
            (inst) + REG_RD_ADDR_##scope##_##reg + \
	    (index) * STRIDE_##scope##_##reg )
#endif

#ifndef REG_WR_VECT
#define REG_WR_VECT( scope, inst, reg, index, val ) \
  REG_WRITE( reg_##scope##_##reg, \
             (inst) + REG_WR_ADDR_##scope##_##reg + \
	     (index) * STRIDE_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_INT
#define REG_RD_INT( scope, inst, reg ) \
  REG_READ( int, (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_WR_INT
#define REG_WR_INT( scope, inst, reg, val ) \
  REG_WRITE( int, (inst) + REG_WR_ADDR_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_INT_VECT
#define REG_RD_INT_VECT( scope, inst, reg, index ) \
  REG_READ( int, (inst) + REG_RD_ADDR_##scope##_##reg + \
	    (index) * STRIDE_##scope##_##reg )
#endif

#ifndef REG_WR_INT_VECT
#define REG_WR_INT_VECT( scope, inst, reg, index, val ) \
  REG_WRITE( int, (inst) + REG_WR_ADDR_##scope##_##reg + \
	     (index) * STRIDE_##scope##_##reg, (val) )
#endif

#ifndef REG_TYPE_CONV
#define REG_TYPE_CONV( type, orgtype, val ) \
  ( { union { orgtype o; type n; } r; r.o = val; r.n; } )
#endif

#ifndef reg_page_size
#define reg_page_size 8192
#endif

#ifndef REG_ADDR
#define REG_ADDR( scope, inst, reg ) \
  ( (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_ADDR_VECT
#define REG_ADDR_VECT( scope, inst, reg, index ) \
  ( (inst) + REG_RD_ADDR_##scope##_##reg + \
    (index) * STRIDE_##scope##_##reg )
#endif

/* C-code for register scope config */

/* Register r_bootsel, scope config, type r */
typedef struct {
  unsigned int boot_mode   : 3;
  unsigned int full_duplex : 1;
  unsigned int user        : 1;
  unsigned int pll         : 1;
  unsigned int flash_bw    : 1;
  unsigned int dummy1      : 25;
} reg_config_r_bootsel;
#define REG_RD_ADDR_config_r_bootsel 0

/* Register rw_clk_ctrl, scope config, type rw */
typedef struct {
  unsigned int pll          : 1;
  unsigned int cpu          : 1;
  unsigned int iop          : 1;
  unsigned int dma01_eth0   : 1;
  unsigned int dma23        : 1;
  unsigned int dma45        : 1;
  unsigned int dma67        : 1;
  unsigned int dma89_strcop : 1;
  unsigned int bif          : 1;
  unsigned int fix_io       : 1;
  unsigned int dummy1       : 22;
} reg_config_rw_clk_ctrl;
#define REG_RD_ADDR_config_rw_clk_ctrl 4
#define REG_WR_ADDR_config_rw_clk_ctrl 4

/* Register rw_pad_ctrl, scope config, type rw */
typedef struct {
  unsigned int usb_susp : 1;
  unsigned int phyrst_n : 1;
  unsigned int dummy1   : 30;
} reg_config_rw_pad_ctrl;
#define REG_RD_ADDR_config_rw_pad_ctrl 8
#define REG_WR_ADDR_config_rw_pad_ctrl 8


/* Constants */
enum {
  regk_config_bw16                         = 0x00000000,
  regk_config_bw32                         = 0x00000001,
  regk_config_master                       = 0x00000005,
  regk_config_nand                         = 0x00000003,
  regk_config_net_rx                       = 0x00000001,
  regk_config_net_tx_rx                    = 0x00000002,
  regk_config_no                           = 0x00000000,
  regk_config_none                         = 0x00000007,
  regk_config_nor                          = 0x00000000,
  regk_config_rw_clk_ctrl_default          = 0x00000002,
  regk_config_rw_pad_ctrl_default          = 0x00000000,
  regk_config_ser                          = 0x00000004,
  regk_config_slave                        = 0x00000006,
  regk_config_yes                          = 0x00000001
};
#endif /* __config_defs_h */
