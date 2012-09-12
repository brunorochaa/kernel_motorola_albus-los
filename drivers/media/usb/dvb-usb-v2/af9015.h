/*
 * DVB USB Linux driver for Afatech AF9015 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
 *
 * Thanks to Afatech who kindly provided information.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef AF9015_H
#define AF9015_H

#include <linux/hash.h>
#include "dvb_usb.h"
#include "af9013.h"
#include "dvb-pll.h"
#include "mt2060.h"
#include "qt1010.h"
#include "tda18271.h"
#include "mxl5005s.h"
#include "mc44s803.h"
#include "tda18218.h"
#include "mxl5007t.h"

#define AF9015_FIRMWARE "dvb-usb-af9015.fw"

/* Windows driver uses packet count 21 for USB1.1 and 348 for USB2.0.
   We use smaller - about 1/4 from the original, 5 and 87. */
#define TS_PACKET_SIZE            188

#define TS_USB20_PACKET_COUNT      87
#define TS_USB20_FRAME_SIZE       (TS_PACKET_SIZE*TS_USB20_PACKET_COUNT)

#define TS_USB11_PACKET_COUNT       5
#define TS_USB11_FRAME_SIZE       (TS_PACKET_SIZE*TS_USB11_PACKET_COUNT)

#define TS_USB20_MAX_PACKET_SIZE  512
#define TS_USB11_MAX_PACKET_SIZE   64

#define AF9015_I2C_EEPROM  0xa0
#define AF9015_I2C_DEMOD   0x38
#define AF9015_USB_TIMEOUT 2000

/* EEPROM locations */
#define AF9015_EEPROM_IR_MODE        0x18
#define AF9015_EEPROM_IR_REMOTE_TYPE 0x34
#define AF9015_EEPROM_TS_MODE        0x31
#define AF9015_EEPROM_DEMOD2_I2C     0x32

#define AF9015_EEPROM_SAW_BW1        0x35
#define AF9015_EEPROM_XTAL_TYPE1     0x36
#define AF9015_EEPROM_SPEC_INV1      0x37
#define AF9015_EEPROM_IF1L           0x38
#define AF9015_EEPROM_IF1H           0x39
#define AF9015_EEPROM_MT2060_IF1L    0x3a
#define AF9015_EEPROM_MT2060_IF1H    0x3b
#define AF9015_EEPROM_TUNER_ID1      0x3c

#define AF9015_EEPROM_SAW_BW2        0x45
#define AF9015_EEPROM_XTAL_TYPE2     0x46
#define AF9015_EEPROM_SPEC_INV2      0x47
#define AF9015_EEPROM_IF2L           0x48
#define AF9015_EEPROM_IF2H           0x49
#define AF9015_EEPROM_MT2060_IF2L    0x4a
#define AF9015_EEPROM_MT2060_IF2H    0x4b
#define AF9015_EEPROM_TUNER_ID2      0x4c

#define AF9015_EEPROM_OFFSET (AF9015_EEPROM_SAW_BW2 - AF9015_EEPROM_SAW_BW1)

struct req_t {
	u8  cmd;       /* [0] */
	/*  seq */     /* [1] */
	u8  i2c_addr;  /* [2] */
	u16 addr;      /* [3|4] */
	u8  mbox;      /* [5] */
	u8  addr_len;  /* [6] */
	u8  data_len;  /* [7] */
	u8  *data;
};

enum af9015_cmd {
	GET_CONFIG           = 0x10,
	DOWNLOAD_FIRMWARE    = 0x11,
	BOOT                 = 0x13,
	READ_MEMORY          = 0x20,
	WRITE_MEMORY         = 0x21,
	READ_WRITE_I2C       = 0x22,
	COPY_FIRMWARE        = 0x23,
	RECONNECT_USB        = 0x5a,
	WRITE_VIRTUAL_MEMORY = 0x26,
	GET_IR_CODE          = 0x27,
	READ_I2C,
	WRITE_I2C,
};

enum af9015_ir_mode {
	AF9015_IR_MODE_DISABLED = 0,
	AF9015_IR_MODE_HID,
	AF9015_IR_MODE_RLC,
	AF9015_IR_MODE_RC6,
	AF9015_IR_MODE_POLLING, /* just guess */
};

struct af9015_state {
	u8 ir_mode;
	u8 rc_repeat;
	u32 rc_keycode;
	u8 rc_last[4];
	bool rc_failed;
	u8 dual_mode;
	u8 seq; /* packet sequence number */
	u16 mt2060_if1[2];
	u16 firmware_size;
	u16 firmware_checksum;
	u32 eeprom_sum;
	struct af9013_config af9013_config[2];

	/* for demod callback override */
	int (*set_frontend[2]) (struct dvb_frontend *fe);
	int (*read_status[2]) (struct dvb_frontend *fe, fe_status_t *status);
	int (*init[2]) (struct dvb_frontend *fe);
	int (*sleep[2]) (struct dvb_frontend *fe);
	int (*tuner_init[2]) (struct dvb_frontend *fe);
	int (*tuner_sleep[2]) (struct dvb_frontend *fe);
	struct mutex fe_mutex;
};

enum af9015_remote {
	AF9015_REMOTE_NONE                    = 0,
/* 1 */	AF9015_REMOTE_A_LINK_DTU_M,
	AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3,
	AF9015_REMOTE_MYGICTV_U718,
	AF9015_REMOTE_DIGITTRADE_DVB_T,
/* 5 */	AF9015_REMOTE_AVERMEDIA_KS,
};

#endif
