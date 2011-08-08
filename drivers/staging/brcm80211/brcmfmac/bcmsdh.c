/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* ****************** SDIO CARD Interface Functions **************************/

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/completion.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <soc.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "sdio_host.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT	2

#define SDIOH_CMD_TYPE_NORMAL   0	/* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1	/* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2	/* Cut-through command */

#define SDIOH_DATA_PIO          0	/* PIO mode */
#define SDIOH_DATA_DMA          1	/* DMA mode */

/* Module parameters specific to each host-controller driver */

module_param(sd_f2_blocksize, int, 0);

int
brcmf_sdcard_iovar_op(struct brcmf_sdio_dev *sdiodev, const char *name,
		void *params, int plen, void *arg, int len, bool set)
{
	return brcmf_sdioh_iovar_op(sdiodev->sdioh, name, params, plen, arg,
				    len, set);
}

int brcmf_sdcard_intr_reg(struct brcmf_sdio_dev *sdiodev)
{
	return brcmf_sdioh_interrupt_register(sdiodev->sdioh);
}

int brcmf_sdcard_intr_dereg(struct brcmf_sdio_dev *sdiodev)
{
	return brcmf_sdioh_interrupt_deregister(sdiodev->sdioh);
}

u8 brcmf_sdcard_cfg_read(struct brcmf_sdio_dev *sdiodev, uint fnc_num, u32 addr,
			 int *err)
{
	int status;
	s32 retry = 0;
	u8 data = 0;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status =
		    brcmf_sdioh_cfg_read(sdiodev->sdioh, fnc_num, addr,
				   (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	BRCMF_INFO(("%s:fun = %d, addr = 0x%x, u8data = 0x%x\n",
		     __func__, fnc_num, addr, data));

	return data;
}

void
brcmf_sdcard_cfg_write(struct brcmf_sdio_dev *sdiodev, uint fnc_num, u32 addr,
		       u8 data, int *err)
{
	int status;
	s32 retry = 0;

	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			udelay(1000);
		status =
		    brcmf_sdioh_cfg_write(sdiodev->sdioh, fnc_num, addr,
				    (u8 *) &data);
	} while (status != 0
		 && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
	if (err)
		*err = status;

	BRCMF_INFO(("%s:fun = %d, addr = 0x%x, u8data = 0x%x\n",
		     __func__, fnc_num, addr, data));
}

int brcmf_sdcard_cis_read(struct brcmf_sdio_dev *sdiodev, uint func, u8 * cis,
			  uint length)
{
	int status;

	u8 *tmp_buf, *tmp_ptr;
	u8 *ptr;
	bool ascii = func & ~0xf;
	func &= 0x7;

	status = brcmf_sdioh_cis_read(sdiodev->sdioh, func, cis, length);

	if (ascii) {
		/* Move binary bits to tmp and format them
			 into the provided buffer. */
		tmp_buf = kmalloc(length, GFP_ATOMIC);
		if (tmp_buf == NULL) {
			BRCMF_ERROR(("%s: out of memory\n", __func__));
			return -ENOMEM;
		}
		memcpy(tmp_buf, cis, length);
		for (tmp_ptr = tmp_buf, ptr = cis; ptr < (cis + length - 4);
		     tmp_ptr++) {
			ptr += sprintf((char *)ptr, "%.2x ", *tmp_ptr & 0xff);
			if ((((tmp_ptr - tmp_buf) + 1) & 0xf) == 0)
				ptr += sprintf((char *)ptr, "\n");
		}
		kfree(tmp_buf);
	}

	return status;
}

static int
brcmf_sdcard_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev, u32 address)
{
	int err = 0;
	brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
			 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRMID,
				       (address >> 16) & SBSDIO_SBADDRMID_MASK,
				       &err);
	if (!err)
		brcmf_sdcard_cfg_write(sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRHIGH,
				       (address >> 24) & SBSDIO_SBADDRHIGH_MASK,
				       &err);

	return err;
}

u32 brcmf_sdcard_reg_read(struct brcmf_sdio_dev *sdiodev, u32 addr, uint size)
{
	int status;
	u32 word = 0;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;

	BRCMF_INFO(("%s:fun = 1, addr = 0x%x, ", __func__, addr));

	if (bar0 != sdiodev->sbwad) {
		if (brcmf_sdcard_set_sbaddr_window(sdiodev, bar0))
			return 0xFFFFFFFF;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = brcmf_sdioh_request_word(sdiodev->sdioh, SDIOH_CMD_TYPE_NORMAL,
				    SDIOH_READ, SDIO_FUNC_1, addr, &word, size);

	sdiodev->regfail = (status != 0);

	BRCMF_INFO(("u32data = 0x%x\n", word));

	/* if ok, return appropriately masked word */
	if (status == 0) {
		switch (size) {
		case sizeof(u8):
			return word & 0xff;
		case sizeof(u16):
			return word & 0xffff;
		case sizeof(u32):
			return word;
		default:
			sdiodev->regfail = true;

		}
	}

	/* otherwise, bad sdio access or invalid size */
	BRCMF_ERROR(("%s: error reading addr 0x%04x size %d\n", __func__,
		      addr, size));
	return 0xFFFFFFFF;
}

u32 brcmf_sdcard_reg_write(struct brcmf_sdio_dev *sdiodev, u32 addr, uint size,
			   u32 data)
{
	int status;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_INFO(("%s:fun = 1, addr = 0x%x, uint%ddata = 0x%x\n",
		     __func__, addr, size * 8, data));

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
	status =
	    brcmf_sdioh_request_word(sdiodev->sdioh, SDIOH_CMD_TYPE_NORMAL,
			       SDIOH_WRITE, SDIO_FUNC_1, addr, &data, size);
	sdiodev->regfail = (status != 0);

	if (status == 0)
		return 0;

	BRCMF_ERROR(("%s: error writing 0x%08x to addr 0x%04x size %d\n",
		      __func__, data, addr, size));
	return 0xFFFFFFFF;
}

bool brcmf_sdcard_regfail(struct brcmf_sdio_dev *sdiodev)
{
	return sdiodev->regfail;
}

int
brcmf_sdcard_recv_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags,
		      u8 *buf, uint nbytes, struct sk_buff *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle)
{
	int status;
	uint incr_fix;
	uint width;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
		     __func__, fn, addr, nbytes));

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = brcmf_sdioh_request_buffer(sdiodev->sdioh, SDIOH_DATA_PIO,
		incr_fix, SDIOH_READ, fn, addr, width, nbytes, buf, pkt);

	return status;
}

int
brcmf_sdcard_send_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, void *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle)
{
	uint incr_fix;
	uint width;
	uint bar0 = addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	BRCMF_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
		     __func__, fn, addr, nbytes));

	/* Async not implemented yet */
	if (flags & SDIO_REQ_ASYNC)
		return -ENOTSUPP;

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdcard_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return brcmf_sdioh_request_buffer(sdiodev->sdioh, SDIOH_DATA_PIO,
		incr_fix, SDIOH_WRITE, fn, addr, width, nbytes, buf, pkt);
}

int brcmf_sdcard_rwdata(struct brcmf_sdio_dev *sdiodev, uint rw, u32 addr,
			u8 *buf, uint nbytes)
{
	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return brcmf_sdioh_request_buffer(sdiodev->sdioh, SDIOH_DATA_PIO,
		SDIOH_DATA_INC, (rw ? SDIOH_WRITE : SDIOH_READ), SDIO_FUNC_1,
		addr, 4, nbytes, buf, NULL);
}

int brcmf_sdcard_abort(struct brcmf_sdio_dev *sdiodev, uint fn)
{
	return brcmf_sdioh_abort(sdiodev->sdioh, fn);
}

u32 brcmf_sdcard_cur_sbwad(struct brcmf_sdio_dev *sdiodev)
{
	return sdiodev->sbwad;
}

int brcmf_sdio_probe(struct brcmf_sdio_dev *sdiodev)
{
	u32 regs = 0;
	int ret = 0;

	sdiodev->sdioh = brcmf_sdioh_attach((void *)0);
	if (!sdiodev->sdioh) {
		ret = -ENODEV;
		goto out;
	}

	regs = SI_ENUM_BASE;

	/* Report the BAR, to fix if needed */
	sdiodev->sbwad = SI_ENUM_BASE;

	/* try to attach to the target device */
	sdiodev->bus = brcmf_sdbrcm_probe(0, 0, 0, 0, regs, sdiodev);
	if (!sdiodev->bus) {
		BRCMF_ERROR(("%s: device attach failed\n", __func__));
		ret = -ENODEV;
		goto out;
	}

out:
	if (ret)
		brcmf_sdio_remove(sdiodev);

	return ret;
}
EXPORT_SYMBOL(brcmf_sdio_probe);

int brcmf_sdio_remove(struct brcmf_sdio_dev *sdiodev)
{
	if (sdiodev->bus) {
		brcmf_sdbrcm_disconnect(sdiodev->bus);
		sdiodev->bus = NULL;
	}

	if (sdiodev->sdioh) {
		brcmf_sdioh_detach(sdiodev->sdioh);
		sdiodev->sdioh = NULL;
	}

	sdiodev->sbwad = 0;

	return 0;
}
EXPORT_SYMBOL(brcmf_sdio_remove);

int brcmf_sdio_register(void)
{
	return brcmf_sdio_function_init();
}

void brcmf_sdio_unregister(void)
{
	brcmf_sdio_function_cleanup();
}

void brcmf_sdio_wdtmr_enable(struct brcmf_sdio_dev *sdiodev, bool enable)
{
	if (enable)
		brcmf_sdbrcm_wd_timer(sdiodev->bus, brcmf_watchdog_ms);
	else
		brcmf_sdbrcm_wd_timer(sdiodev->bus, 0);
}
