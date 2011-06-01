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
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <bcmdefs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <stdarg.h>
#include <bcmutils.h>
#include <bcmsoc.h>
#include <sbchipc.h>
#include <bcmdevs.h>
#include <pcicfg.h>
#include <aiutils.h>
#include <bcmsrom.h>

#include <bcmnvram.h>
#include <bcmotp.h>

#define SROM_OFFSET(sih) ((sih->ccrev > 31) ? \
	(((sih->cccaps & CC_CAP_SROM) == 0) ? NULL : \
	 ((u8 *)curmap + PCI_16KB0_CCREGS_OFFSET + CC_SROM_OTP)) : \
	((u8 *)curmap + PCI_BAR0_SPROM_OFFSET))

#if defined(BCMDBG)
#define WRITE_ENABLE_DELAY	500	/* 500 ms after write enable/disable toggle */
#define WRITE_WORD_DELAY	20	/* 20 ms between each word write */
#endif

/* SROM flags (see sromvar_t) */
#define SRFL_MORE	1	/* value continues as described by the next entry */
#define	SRFL_NOFFS	2	/* value bits can't be all one's */
#define	SRFL_PRHEX	4	/* value is in hexdecimal format */
#define	SRFL_PRSIGN	8	/* value is in signed decimal format */
#define	SRFL_CCODE	0x10	/* value is in country code format */
#define	SRFL_ETHADDR	0x20	/* value is an Ethernet address */
#define SRFL_LEDDC	0x40	/* value is an LED duty cycle */
#define SRFL_NOVAR	0x80	/* do not generate a nvram param, entry is for mfgc */

typedef struct {
	const char *name;
	u32 revmask;
	u32 flags;
	u16 off;
	u16 mask;
} sromvar_t;

typedef struct varbuf {
	char *base;		/* pointer to buffer base */
	char *buf;		/* pointer to current position */
	unsigned int size;	/* current (residual) size in bytes */
} varbuf_t;
extern char *_vars;
extern uint _varsz;

/* Assumptions:
 * - Ethernet address spans across 3 consective words
 *
 * Table rules:
 * - Add multiple entries next to each other if a value spans across multiple words
 *   (even multiple fields in the same word) with each entry except the last having
 *   it's SRFL_MORE bit set.
 * - Ethernet address entry does not follow above rule and must not have SRFL_MORE
 *   bit set. Its SRFL_ETHADDR bit implies it takes multiple words.
 * - The last entry's name field must be NULL to indicate the end of the table. Other
 *   entries must have non-NULL name.
 */
static const sromvar_t pci_sromvars[] = {
	{"devid", 0xffffff00, SRFL_PRHEX | SRFL_NOVAR, PCI_F0DEVID, 0xffff},
	{"boardrev", 0x0000000e, SRFL_PRHEX, SROM_AABREV, SROM_BR_MASK},
	{"boardrev", 0x000000f0, SRFL_PRHEX, SROM4_BREV, 0xffff},
	{"boardrev", 0xffffff00, SRFL_PRHEX, SROM8_BREV, 0xffff},
	{"boardflags", 0x00000002, SRFL_PRHEX, SROM_BFL, 0xffff},
	{"boardflags", 0x00000004, SRFL_PRHEX | SRFL_MORE, SROM_BFL, 0xffff},
	{"", 0, 0, SROM_BFL2, 0xffff},
	{"boardflags", 0x00000008, SRFL_PRHEX | SRFL_MORE, SROM_BFL, 0xffff},
	{"", 0, 0, SROM3_BFL2, 0xffff},
	{"boardflags", 0x00000010, SRFL_PRHEX | SRFL_MORE, SROM4_BFL0, 0xffff},
	{"", 0, 0, SROM4_BFL1, 0xffff},
	{"boardflags", 0x000000e0, SRFL_PRHEX | SRFL_MORE, SROM5_BFL0, 0xffff},
	{"", 0, 0, SROM5_BFL1, 0xffff},
	{"boardflags", 0xffffff00, SRFL_PRHEX | SRFL_MORE, SROM8_BFL0, 0xffff},
	{"", 0, 0, SROM8_BFL1, 0xffff},
	{"boardflags2", 0x00000010, SRFL_PRHEX | SRFL_MORE, SROM4_BFL2, 0xffff},
	{"", 0, 0, SROM4_BFL3, 0xffff},
	{"boardflags2", 0x000000e0, SRFL_PRHEX | SRFL_MORE, SROM5_BFL2, 0xffff},
	{"", 0, 0, SROM5_BFL3, 0xffff},
	{"boardflags2", 0xffffff00, SRFL_PRHEX | SRFL_MORE, SROM8_BFL2, 0xffff},
	{"", 0, 0, SROM8_BFL3, 0xffff},
	{"boardtype", 0xfffffffc, SRFL_PRHEX, SROM_SSID, 0xffff},
	{"boardnum", 0x00000006, 0, SROM_MACLO_IL0, 0xffff},
	{"boardnum", 0x00000008, 0, SROM3_MACLO, 0xffff},
	{"boardnum", 0x00000010, 0, SROM4_MACLO, 0xffff},
	{"boardnum", 0x000000e0, 0, SROM5_MACLO, 0xffff},
	{"boardnum", 0xffffff00, 0, SROM8_MACLO, 0xffff},
	{"cc", 0x00000002, 0, SROM_AABREV, SROM_CC_MASK},
	{"regrev", 0x00000008, 0, SROM_OPO, 0xff00},
	{"regrev", 0x00000010, 0, SROM4_REGREV, 0x00ff},
	{"regrev", 0x000000e0, 0, SROM5_REGREV, 0x00ff},
	{"regrev", 0xffffff00, 0, SROM8_REGREV, 0x00ff},
	{"ledbh0", 0x0000000e, SRFL_NOFFS, SROM_LEDBH10, 0x00ff},
	{"ledbh1", 0x0000000e, SRFL_NOFFS, SROM_LEDBH10, 0xff00},
	{"ledbh2", 0x0000000e, SRFL_NOFFS, SROM_LEDBH32, 0x00ff},
	{"ledbh3", 0x0000000e, SRFL_NOFFS, SROM_LEDBH32, 0xff00},
	{"ledbh0", 0x00000010, SRFL_NOFFS, SROM4_LEDBH10, 0x00ff},
	{"ledbh1", 0x00000010, SRFL_NOFFS, SROM4_LEDBH10, 0xff00},
	{"ledbh2", 0x00000010, SRFL_NOFFS, SROM4_LEDBH32, 0x00ff},
	{"ledbh3", 0x00000010, SRFL_NOFFS, SROM4_LEDBH32, 0xff00},
	{"ledbh0", 0x000000e0, SRFL_NOFFS, SROM5_LEDBH10, 0x00ff},
	{"ledbh1", 0x000000e0, SRFL_NOFFS, SROM5_LEDBH10, 0xff00},
	{"ledbh2", 0x000000e0, SRFL_NOFFS, SROM5_LEDBH32, 0x00ff},
	{"ledbh3", 0x000000e0, SRFL_NOFFS, SROM5_LEDBH32, 0xff00},
	{"ledbh0", 0xffffff00, SRFL_NOFFS, SROM8_LEDBH10, 0x00ff},
	{"ledbh1", 0xffffff00, SRFL_NOFFS, SROM8_LEDBH10, 0xff00},
	{"ledbh2", 0xffffff00, SRFL_NOFFS, SROM8_LEDBH32, 0x00ff},
	{"ledbh3", 0xffffff00, SRFL_NOFFS, SROM8_LEDBH32, 0xff00},
	{"pa0b0", 0x0000000e, SRFL_PRHEX, SROM_WL0PAB0, 0xffff},
	{"pa0b1", 0x0000000e, SRFL_PRHEX, SROM_WL0PAB1, 0xffff},
	{"pa0b2", 0x0000000e, SRFL_PRHEX, SROM_WL0PAB2, 0xffff},
	{"pa0itssit", 0x0000000e, 0, SROM_ITT, 0x00ff},
	{"pa0maxpwr", 0x0000000e, 0, SROM_WL10MAXP, 0x00ff},
	{"pa0b0", 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB0, 0xffff},
	{"pa0b1", 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB1, 0xffff},
	{"pa0b2", 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB2, 0xffff},
	{"pa0itssit", 0xffffff00, 0, SROM8_W0_ITTMAXP, 0xff00},
	{"pa0maxpwr", 0xffffff00, 0, SROM8_W0_ITTMAXP, 0x00ff},
	{"opo", 0x0000000c, 0, SROM_OPO, 0x00ff},
	{"opo", 0xffffff00, 0, SROM8_2G_OFDMPO, 0x00ff},
	{"aa2g", 0x0000000e, 0, SROM_AABREV, SROM_AA0_MASK},
	{"aa2g", 0x000000f0, 0, SROM4_AA, 0x00ff},
	{"aa2g", 0xffffff00, 0, SROM8_AA, 0x00ff},
	{"aa5g", 0x0000000e, 0, SROM_AABREV, SROM_AA1_MASK},
	{"aa5g", 0x000000f0, 0, SROM4_AA, 0xff00},
	{"aa5g", 0xffffff00, 0, SROM8_AA, 0xff00},
	{"ag0", 0x0000000e, 0, SROM_AG10, 0x00ff},
	{"ag1", 0x0000000e, 0, SROM_AG10, 0xff00},
	{"ag0", 0x000000f0, 0, SROM4_AG10, 0x00ff},
	{"ag1", 0x000000f0, 0, SROM4_AG10, 0xff00},
	{"ag2", 0x000000f0, 0, SROM4_AG32, 0x00ff},
	{"ag3", 0x000000f0, 0, SROM4_AG32, 0xff00},
	{"ag0", 0xffffff00, 0, SROM8_AG10, 0x00ff},
	{"ag1", 0xffffff00, 0, SROM8_AG10, 0xff00},
	{"ag2", 0xffffff00, 0, SROM8_AG32, 0x00ff},
	{"ag3", 0xffffff00, 0, SROM8_AG32, 0xff00},
	{"pa1b0", 0x0000000e, SRFL_PRHEX, SROM_WL1PAB0, 0xffff},
	{"pa1b1", 0x0000000e, SRFL_PRHEX, SROM_WL1PAB1, 0xffff},
	{"pa1b2", 0x0000000e, SRFL_PRHEX, SROM_WL1PAB2, 0xffff},
	{"pa1lob0", 0x0000000c, SRFL_PRHEX, SROM_WL1LPAB0, 0xffff},
	{"pa1lob1", 0x0000000c, SRFL_PRHEX, SROM_WL1LPAB1, 0xffff},
	{"pa1lob2", 0x0000000c, SRFL_PRHEX, SROM_WL1LPAB2, 0xffff},
	{"pa1hib0", 0x0000000c, SRFL_PRHEX, SROM_WL1HPAB0, 0xffff},
	{"pa1hib1", 0x0000000c, SRFL_PRHEX, SROM_WL1HPAB1, 0xffff},
	{"pa1hib2", 0x0000000c, SRFL_PRHEX, SROM_WL1HPAB2, 0xffff},
	{"pa1itssit", 0x0000000e, 0, SROM_ITT, 0xff00},
	{"pa1maxpwr", 0x0000000e, 0, SROM_WL10MAXP, 0xff00},
	{"pa1lomaxpwr", 0x0000000c, 0, SROM_WL1LHMAXP, 0xff00},
	{"pa1himaxpwr", 0x0000000c, 0, SROM_WL1LHMAXP, 0x00ff},
	{"pa1b0", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0, 0xffff},
	{"pa1b1", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1, 0xffff},
	{"pa1b2", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2, 0xffff},
	{"pa1lob0", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0_LC, 0xffff},
	{"pa1lob1", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1_LC, 0xffff},
	{"pa1lob2", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2_LC, 0xffff},
	{"pa1hib0", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0_HC, 0xffff},
	{"pa1hib1", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1_HC, 0xffff},
	{"pa1hib2", 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2_HC, 0xffff},
	{"pa1itssit", 0xffffff00, 0, SROM8_W1_ITTMAXP, 0xff00},
	{"pa1maxpwr", 0xffffff00, 0, SROM8_W1_ITTMAXP, 0x00ff},
	{"pa1lomaxpwr", 0xffffff00, 0, SROM8_W1_MAXP_LCHC, 0xff00},
	{"pa1himaxpwr", 0xffffff00, 0, SROM8_W1_MAXP_LCHC, 0x00ff},
	{"bxa2g", 0x00000008, 0, SROM_BXARSSI2G, 0x1800},
	{"rssisav2g", 0x00000008, 0, SROM_BXARSSI2G, 0x0700},
	{"rssismc2g", 0x00000008, 0, SROM_BXARSSI2G, 0x00f0},
	{"rssismf2g", 0x00000008, 0, SROM_BXARSSI2G, 0x000f},
	{"bxa2g", 0xffffff00, 0, SROM8_BXARSSI2G, 0x1800},
	{"rssisav2g", 0xffffff00, 0, SROM8_BXARSSI2G, 0x0700},
	{"rssismc2g", 0xffffff00, 0, SROM8_BXARSSI2G, 0x00f0},
	{"rssismf2g", 0xffffff00, 0, SROM8_BXARSSI2G, 0x000f},
	{"bxa5g", 0x00000008, 0, SROM_BXARSSI5G, 0x1800},
	{"rssisav5g", 0x00000008, 0, SROM_BXARSSI5G, 0x0700},
	{"rssismc5g", 0x00000008, 0, SROM_BXARSSI5G, 0x00f0},
	{"rssismf5g", 0x00000008, 0, SROM_BXARSSI5G, 0x000f},
	{"bxa5g", 0xffffff00, 0, SROM8_BXARSSI5G, 0x1800},
	{"rssisav5g", 0xffffff00, 0, SROM8_BXARSSI5G, 0x0700},
	{"rssismc5g", 0xffffff00, 0, SROM8_BXARSSI5G, 0x00f0},
	{"rssismf5g", 0xffffff00, 0, SROM8_BXARSSI5G, 0x000f},
	{"tri2g", 0x00000008, 0, SROM_TRI52G, 0x00ff},
	{"tri5g", 0x00000008, 0, SROM_TRI52G, 0xff00},
	{"tri5gl", 0x00000008, 0, SROM_TRI5GHL, 0x00ff},
	{"tri5gh", 0x00000008, 0, SROM_TRI5GHL, 0xff00},
	{"tri2g", 0xffffff00, 0, SROM8_TRI52G, 0x00ff},
	{"tri5g", 0xffffff00, 0, SROM8_TRI52G, 0xff00},
	{"tri5gl", 0xffffff00, 0, SROM8_TRI5GHL, 0x00ff},
	{"tri5gh", 0xffffff00, 0, SROM8_TRI5GHL, 0xff00},
	{"rxpo2g", 0x00000008, SRFL_PRSIGN, SROM_RXPO52G, 0x00ff},
	{"rxpo5g", 0x00000008, SRFL_PRSIGN, SROM_RXPO52G, 0xff00},
	{"rxpo2g", 0xffffff00, SRFL_PRSIGN, SROM8_RXPO52G, 0x00ff},
	{"rxpo5g", 0xffffff00, SRFL_PRSIGN, SROM8_RXPO52G, 0xff00},
	{"txchain", 0x000000f0, SRFL_NOFFS, SROM4_TXRXC, SROM4_TXCHAIN_MASK},
	{"rxchain", 0x000000f0, SRFL_NOFFS, SROM4_TXRXC, SROM4_RXCHAIN_MASK},
	{"antswitch", 0x000000f0, SRFL_NOFFS, SROM4_TXRXC, SROM4_SWITCH_MASK},
	{"txchain", 0xffffff00, SRFL_NOFFS, SROM8_TXRXC, SROM4_TXCHAIN_MASK},
	{"rxchain", 0xffffff00, SRFL_NOFFS, SROM8_TXRXC, SROM4_RXCHAIN_MASK},
	{"antswitch", 0xffffff00, SRFL_NOFFS, SROM8_TXRXC, SROM4_SWITCH_MASK},
	{"tssipos2g", 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_TSSIPOS_MASK},
	{"extpagain2g", 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_EXTPA_GAIN_MASK},
	{"pdetrange2g", 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_PDET_RANGE_MASK},
	{"triso2g", 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_TR_ISO_MASK},
	{"antswctl2g", 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_ANTSWLUT_MASK},
	{"tssipos5g", 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_TSSIPOS_MASK},
	{"extpagain5g", 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_EXTPA_GAIN_MASK},
	{"pdetrange5g", 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_PDET_RANGE_MASK},
	{"triso5g", 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_TR_ISO_MASK},
	{"antswctl5g", 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_ANTSWLUT_MASK},
	{"tempthresh", 0xffffff00, 0, SROM8_THERMAL, 0xff00},
	{"tempoffset", 0xffffff00, 0, SROM8_THERMAL, 0x00ff},
	{"txpid2ga0", 0x000000f0, 0, SROM4_TXPID2G, 0x00ff},
	{"txpid2ga1", 0x000000f0, 0, SROM4_TXPID2G, 0xff00},
	{"txpid2ga2", 0x000000f0, 0, SROM4_TXPID2G + 1, 0x00ff},
	{"txpid2ga3", 0x000000f0, 0, SROM4_TXPID2G + 1, 0xff00},
	{"txpid5ga0", 0x000000f0, 0, SROM4_TXPID5G, 0x00ff},
	{"txpid5ga1", 0x000000f0, 0, SROM4_TXPID5G, 0xff00},
	{"txpid5ga2", 0x000000f0, 0, SROM4_TXPID5G + 1, 0x00ff},
	{"txpid5ga3", 0x000000f0, 0, SROM4_TXPID5G + 1, 0xff00},
	{"txpid5gla0", 0x000000f0, 0, SROM4_TXPID5GL, 0x00ff},
	{"txpid5gla1", 0x000000f0, 0, SROM4_TXPID5GL, 0xff00},
	{"txpid5gla2", 0x000000f0, 0, SROM4_TXPID5GL + 1, 0x00ff},
	{"txpid5gla3", 0x000000f0, 0, SROM4_TXPID5GL + 1, 0xff00},
	{"txpid5gha0", 0x000000f0, 0, SROM4_TXPID5GH, 0x00ff},
	{"txpid5gha1", 0x000000f0, 0, SROM4_TXPID5GH, 0xff00},
	{"txpid5gha2", 0x000000f0, 0, SROM4_TXPID5GH + 1, 0x00ff},
	{"txpid5gha3", 0x000000f0, 0, SROM4_TXPID5GH + 1, 0xff00},

	{"ccode", 0x0000000f, SRFL_CCODE, SROM_CCODE, 0xffff},
	{"ccode", 0x00000010, SRFL_CCODE, SROM4_CCODE, 0xffff},
	{"ccode", 0x000000e0, SRFL_CCODE, SROM5_CCODE, 0xffff},
	{"ccode", 0xffffff00, SRFL_CCODE, SROM8_CCODE, 0xffff},
	{"macaddr", 0xffffff00, SRFL_ETHADDR, SROM8_MACHI, 0xffff},
	{"macaddr", 0x000000e0, SRFL_ETHADDR, SROM5_MACHI, 0xffff},
	{"macaddr", 0x00000010, SRFL_ETHADDR, SROM4_MACHI, 0xffff},
	{"macaddr", 0x00000008, SRFL_ETHADDR, SROM3_MACHI, 0xffff},
	{"il0macaddr", 0x00000007, SRFL_ETHADDR, SROM_MACHI_IL0, 0xffff},
	{"et1macaddr", 0x00000007, SRFL_ETHADDR, SROM_MACHI_ET1, 0xffff},
	{"leddc", 0xffffff00, SRFL_NOFFS | SRFL_LEDDC, SROM8_LEDDC, 0xffff},
	{"leddc", 0x000000e0, SRFL_NOFFS | SRFL_LEDDC, SROM5_LEDDC, 0xffff},
	{"leddc", 0x00000010, SRFL_NOFFS | SRFL_LEDDC, SROM4_LEDDC, 0xffff},
	{"leddc", 0x00000008, SRFL_NOFFS | SRFL_LEDDC, SROM3_LEDDC, 0xffff},
	{"rawtempsense", 0xffffff00, SRFL_PRHEX, SROM8_MPWR_RAWTS, 0x01ff},
	{"measpower", 0xffffff00, SRFL_PRHEX, SROM8_MPWR_RAWTS, 0xfe00},
	{"tempsense_slope", 0xffffff00, SRFL_PRHEX, SROM8_TS_SLP_OPT_CORRX,
	 0x00ff},
	{"tempcorrx", 0xffffff00, SRFL_PRHEX, SROM8_TS_SLP_OPT_CORRX, 0xfc00},
	{"tempsense_option", 0xffffff00, SRFL_PRHEX, SROM8_TS_SLP_OPT_CORRX,
	 0x0300},
	{"freqoffset_corr", 0xffffff00, SRFL_PRHEX, SROM8_FOC_HWIQ_IQSWP,
	 0x000f},
	{"iqcal_swp_dis", 0xffffff00, SRFL_PRHEX, SROM8_FOC_HWIQ_IQSWP, 0x0010},
	{"hw_iqcal_en", 0xffffff00, SRFL_PRHEX, SROM8_FOC_HWIQ_IQSWP, 0x0020},
	{"phycal_tempdelta", 0xffffff00, 0, SROM8_PHYCAL_TEMPDELTA, 0x00ff},

	{"cck2gpo", 0x000000f0, 0, SROM4_2G_CCKPO, 0xffff},
	{"cck2gpo", 0x00000100, 0, SROM8_2G_CCKPO, 0xffff},
	{"ofdm2gpo", 0x000000f0, SRFL_MORE, SROM4_2G_OFDMPO, 0xffff},
	{"", 0, 0, SROM4_2G_OFDMPO + 1, 0xffff},
	{"ofdm5gpo", 0x000000f0, SRFL_MORE, SROM4_5G_OFDMPO, 0xffff},
	{"", 0, 0, SROM4_5G_OFDMPO + 1, 0xffff},
	{"ofdm5glpo", 0x000000f0, SRFL_MORE, SROM4_5GL_OFDMPO, 0xffff},
	{"", 0, 0, SROM4_5GL_OFDMPO + 1, 0xffff},
	{"ofdm5ghpo", 0x000000f0, SRFL_MORE, SROM4_5GH_OFDMPO, 0xffff},
	{"", 0, 0, SROM4_5GH_OFDMPO + 1, 0xffff},
	{"ofdm2gpo", 0x00000100, SRFL_MORE, SROM8_2G_OFDMPO, 0xffff},
	{"", 0, 0, SROM8_2G_OFDMPO + 1, 0xffff},
	{"ofdm5gpo", 0x00000100, SRFL_MORE, SROM8_5G_OFDMPO, 0xffff},
	{"", 0, 0, SROM8_5G_OFDMPO + 1, 0xffff},
	{"ofdm5glpo", 0x00000100, SRFL_MORE, SROM8_5GL_OFDMPO, 0xffff},
	{"", 0, 0, SROM8_5GL_OFDMPO + 1, 0xffff},
	{"ofdm5ghpo", 0x00000100, SRFL_MORE, SROM8_5GH_OFDMPO, 0xffff},
	{"", 0, 0, SROM8_5GH_OFDMPO + 1, 0xffff},
	{"mcs2gpo0", 0x000000f0, 0, SROM4_2G_MCSPO, 0xffff},
	{"mcs2gpo1", 0x000000f0, 0, SROM4_2G_MCSPO + 1, 0xffff},
	{"mcs2gpo2", 0x000000f0, 0, SROM4_2G_MCSPO + 2, 0xffff},
	{"mcs2gpo3", 0x000000f0, 0, SROM4_2G_MCSPO + 3, 0xffff},
	{"mcs2gpo4", 0x000000f0, 0, SROM4_2G_MCSPO + 4, 0xffff},
	{"mcs2gpo5", 0x000000f0, 0, SROM4_2G_MCSPO + 5, 0xffff},
	{"mcs2gpo6", 0x000000f0, 0, SROM4_2G_MCSPO + 6, 0xffff},
	{"mcs2gpo7", 0x000000f0, 0, SROM4_2G_MCSPO + 7, 0xffff},
	{"mcs5gpo0", 0x000000f0, 0, SROM4_5G_MCSPO, 0xffff},
	{"mcs5gpo1", 0x000000f0, 0, SROM4_5G_MCSPO + 1, 0xffff},
	{"mcs5gpo2", 0x000000f0, 0, SROM4_5G_MCSPO + 2, 0xffff},
	{"mcs5gpo3", 0x000000f0, 0, SROM4_5G_MCSPO + 3, 0xffff},
	{"mcs5gpo4", 0x000000f0, 0, SROM4_5G_MCSPO + 4, 0xffff},
	{"mcs5gpo5", 0x000000f0, 0, SROM4_5G_MCSPO + 5, 0xffff},
	{"mcs5gpo6", 0x000000f0, 0, SROM4_5G_MCSPO + 6, 0xffff},
	{"mcs5gpo7", 0x000000f0, 0, SROM4_5G_MCSPO + 7, 0xffff},
	{"mcs5glpo0", 0x000000f0, 0, SROM4_5GL_MCSPO, 0xffff},
	{"mcs5glpo1", 0x000000f0, 0, SROM4_5GL_MCSPO + 1, 0xffff},
	{"mcs5glpo2", 0x000000f0, 0, SROM4_5GL_MCSPO + 2, 0xffff},
	{"mcs5glpo3", 0x000000f0, 0, SROM4_5GL_MCSPO + 3, 0xffff},
	{"mcs5glpo4", 0x000000f0, 0, SROM4_5GL_MCSPO + 4, 0xffff},
	{"mcs5glpo5", 0x000000f0, 0, SROM4_5GL_MCSPO + 5, 0xffff},
	{"mcs5glpo6", 0x000000f0, 0, SROM4_5GL_MCSPO + 6, 0xffff},
	{"mcs5glpo7", 0x000000f0, 0, SROM4_5GL_MCSPO + 7, 0xffff},
	{"mcs5ghpo0", 0x000000f0, 0, SROM4_5GH_MCSPO, 0xffff},
	{"mcs5ghpo1", 0x000000f0, 0, SROM4_5GH_MCSPO + 1, 0xffff},
	{"mcs5ghpo2", 0x000000f0, 0, SROM4_5GH_MCSPO + 2, 0xffff},
	{"mcs5ghpo3", 0x000000f0, 0, SROM4_5GH_MCSPO + 3, 0xffff},
	{"mcs5ghpo4", 0x000000f0, 0, SROM4_5GH_MCSPO + 4, 0xffff},
	{"mcs5ghpo5", 0x000000f0, 0, SROM4_5GH_MCSPO + 5, 0xffff},
	{"mcs5ghpo6", 0x000000f0, 0, SROM4_5GH_MCSPO + 6, 0xffff},
	{"mcs5ghpo7", 0x000000f0, 0, SROM4_5GH_MCSPO + 7, 0xffff},
	{"mcs2gpo0", 0x00000100, 0, SROM8_2G_MCSPO, 0xffff},
	{"mcs2gpo1", 0x00000100, 0, SROM8_2G_MCSPO + 1, 0xffff},
	{"mcs2gpo2", 0x00000100, 0, SROM8_2G_MCSPO + 2, 0xffff},
	{"mcs2gpo3", 0x00000100, 0, SROM8_2G_MCSPO + 3, 0xffff},
	{"mcs2gpo4", 0x00000100, 0, SROM8_2G_MCSPO + 4, 0xffff},
	{"mcs2gpo5", 0x00000100, 0, SROM8_2G_MCSPO + 5, 0xffff},
	{"mcs2gpo6", 0x00000100, 0, SROM8_2G_MCSPO + 6, 0xffff},
	{"mcs2gpo7", 0x00000100, 0, SROM8_2G_MCSPO + 7, 0xffff},
	{"mcs5gpo0", 0x00000100, 0, SROM8_5G_MCSPO, 0xffff},
	{"mcs5gpo1", 0x00000100, 0, SROM8_5G_MCSPO + 1, 0xffff},
	{"mcs5gpo2", 0x00000100, 0, SROM8_5G_MCSPO + 2, 0xffff},
	{"mcs5gpo3", 0x00000100, 0, SROM8_5G_MCSPO + 3, 0xffff},
	{"mcs5gpo4", 0x00000100, 0, SROM8_5G_MCSPO + 4, 0xffff},
	{"mcs5gpo5", 0x00000100, 0, SROM8_5G_MCSPO + 5, 0xffff},
	{"mcs5gpo6", 0x00000100, 0, SROM8_5G_MCSPO + 6, 0xffff},
	{"mcs5gpo7", 0x00000100, 0, SROM8_5G_MCSPO + 7, 0xffff},
	{"mcs5glpo0", 0x00000100, 0, SROM8_5GL_MCSPO, 0xffff},
	{"mcs5glpo1", 0x00000100, 0, SROM8_5GL_MCSPO + 1, 0xffff},
	{"mcs5glpo2", 0x00000100, 0, SROM8_5GL_MCSPO + 2, 0xffff},
	{"mcs5glpo3", 0x00000100, 0, SROM8_5GL_MCSPO + 3, 0xffff},
	{"mcs5glpo4", 0x00000100, 0, SROM8_5GL_MCSPO + 4, 0xffff},
	{"mcs5glpo5", 0x00000100, 0, SROM8_5GL_MCSPO + 5, 0xffff},
	{"mcs5glpo6", 0x00000100, 0, SROM8_5GL_MCSPO + 6, 0xffff},
	{"mcs5glpo7", 0x00000100, 0, SROM8_5GL_MCSPO + 7, 0xffff},
	{"mcs5ghpo0", 0x00000100, 0, SROM8_5GH_MCSPO, 0xffff},
	{"mcs5ghpo1", 0x00000100, 0, SROM8_5GH_MCSPO + 1, 0xffff},
	{"mcs5ghpo2", 0x00000100, 0, SROM8_5GH_MCSPO + 2, 0xffff},
	{"mcs5ghpo3", 0x00000100, 0, SROM8_5GH_MCSPO + 3, 0xffff},
	{"mcs5ghpo4", 0x00000100, 0, SROM8_5GH_MCSPO + 4, 0xffff},
	{"mcs5ghpo5", 0x00000100, 0, SROM8_5GH_MCSPO + 5, 0xffff},
	{"mcs5ghpo6", 0x00000100, 0, SROM8_5GH_MCSPO + 6, 0xffff},
	{"mcs5ghpo7", 0x00000100, 0, SROM8_5GH_MCSPO + 7, 0xffff},
	{"cddpo", 0x000000f0, 0, SROM4_CDDPO, 0xffff},
	{"stbcpo", 0x000000f0, 0, SROM4_STBCPO, 0xffff},
	{"bw40po", 0x000000f0, 0, SROM4_BW40PO, 0xffff},
	{"bwduppo", 0x000000f0, 0, SROM4_BWDUPPO, 0xffff},
	{"cddpo", 0x00000100, 0, SROM8_CDDPO, 0xffff},
	{"stbcpo", 0x00000100, 0, SROM8_STBCPO, 0xffff},
	{"bw40po", 0x00000100, 0, SROM8_BW40PO, 0xffff},
	{"bwduppo", 0x00000100, 0, SROM8_BWDUPPO, 0xffff},

	/* power per rate from sromrev 9 */
	{"cckbw202gpo", 0xfffffe00, 0, SROM9_2GPO_CCKBW20, 0xffff},
	{"cckbw20ul2gpo", 0xfffffe00, 0, SROM9_2GPO_CCKBW20UL, 0xffff},
	{"legofdmbw202gpo", 0xfffffe00, SRFL_MORE, SROM9_2GPO_LOFDMBW20,
	 0xffff},
	{"", 0, 0, SROM9_2GPO_LOFDMBW20 + 1, 0xffff},
	{"legofdmbw20ul2gpo", 0xfffffe00, SRFL_MORE, SROM9_2GPO_LOFDMBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_2GPO_LOFDMBW20UL + 1, 0xffff},
	{"legofdmbw205glpo", 0xfffffe00, SRFL_MORE, SROM9_5GLPO_LOFDMBW20,
	 0xffff},
	{"", 0, 0, SROM9_5GLPO_LOFDMBW20 + 1, 0xffff},
	{"legofdmbw20ul5glpo", 0xfffffe00, SRFL_MORE, SROM9_5GLPO_LOFDMBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GLPO_LOFDMBW20UL + 1, 0xffff},
	{"legofdmbw205gmpo", 0xfffffe00, SRFL_MORE, SROM9_5GMPO_LOFDMBW20,
	 0xffff},
	{"", 0, 0, SROM9_5GMPO_LOFDMBW20 + 1, 0xffff},
	{"legofdmbw20ul5gmpo", 0xfffffe00, SRFL_MORE, SROM9_5GMPO_LOFDMBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GMPO_LOFDMBW20UL + 1, 0xffff},
	{"legofdmbw205ghpo", 0xfffffe00, SRFL_MORE, SROM9_5GHPO_LOFDMBW20,
	 0xffff},
	{"", 0, 0, SROM9_5GHPO_LOFDMBW20 + 1, 0xffff},
	{"legofdmbw20ul5ghpo", 0xfffffe00, SRFL_MORE, SROM9_5GHPO_LOFDMBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GHPO_LOFDMBW20UL + 1, 0xffff},
	{"mcsbw202gpo", 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW20, 0xffff},
	{"", 0, 0, SROM9_2GPO_MCSBW20 + 1, 0xffff},
	{"mcsbw20ul2gpo", 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW20UL, 0xffff},
	{"", 0, 0, SROM9_2GPO_MCSBW20UL + 1, 0xffff},
	{"mcsbw402gpo", 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW40, 0xffff},
	{"", 0, 0, SROM9_2GPO_MCSBW40 + 1, 0xffff},
	{"mcsbw205glpo", 0xfffffe00, SRFL_MORE, SROM9_5GLPO_MCSBW20, 0xffff},
	{"", 0, 0, SROM9_5GLPO_MCSBW20 + 1, 0xffff},
	{"mcsbw20ul5glpo", 0xfffffe00, SRFL_MORE, SROM9_5GLPO_MCSBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GLPO_MCSBW20UL + 1, 0xffff},
	{"mcsbw405glpo", 0xfffffe00, SRFL_MORE, SROM9_5GLPO_MCSBW40, 0xffff},
	{"", 0, 0, SROM9_5GLPO_MCSBW40 + 1, 0xffff},
	{"mcsbw205gmpo", 0xfffffe00, SRFL_MORE, SROM9_5GMPO_MCSBW20, 0xffff},
	{"", 0, 0, SROM9_5GMPO_MCSBW20 + 1, 0xffff},
	{"mcsbw20ul5gmpo", 0xfffffe00, SRFL_MORE, SROM9_5GMPO_MCSBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GMPO_MCSBW20UL + 1, 0xffff},
	{"mcsbw405gmpo", 0xfffffe00, SRFL_MORE, SROM9_5GMPO_MCSBW40, 0xffff},
	{"", 0, 0, SROM9_5GMPO_MCSBW40 + 1, 0xffff},
	{"mcsbw205ghpo", 0xfffffe00, SRFL_MORE, SROM9_5GHPO_MCSBW20, 0xffff},
	{"", 0, 0, SROM9_5GHPO_MCSBW20 + 1, 0xffff},
	{"mcsbw20ul5ghpo", 0xfffffe00, SRFL_MORE, SROM9_5GHPO_MCSBW20UL,
	 0xffff},
	{"", 0, 0, SROM9_5GHPO_MCSBW20UL + 1, 0xffff},
	{"mcsbw405ghpo", 0xfffffe00, SRFL_MORE, SROM9_5GHPO_MCSBW40, 0xffff},
	{"", 0, 0, SROM9_5GHPO_MCSBW40 + 1, 0xffff},
	{"mcs32po", 0xfffffe00, 0, SROM9_PO_MCS32, 0xffff},
	{"legofdm40duppo", 0xfffffe00, 0, SROM9_PO_LOFDM40DUP, 0xffff},

	{NULL, 0, 0, 0, 0}
};

static const sromvar_t perpath_pci_sromvars[] = {
	{"maxp2ga", 0x000000f0, 0, SROM4_2G_ITT_MAXP, 0x00ff},
	{"itt2ga", 0x000000f0, 0, SROM4_2G_ITT_MAXP, 0xff00},
	{"itt5ga", 0x000000f0, 0, SROM4_5G_ITT_MAXP, 0xff00},
	{"pa2gw0a", 0x000000f0, SRFL_PRHEX, SROM4_2G_PA, 0xffff},
	{"pa2gw1a", 0x000000f0, SRFL_PRHEX, SROM4_2G_PA + 1, 0xffff},
	{"pa2gw2a", 0x000000f0, SRFL_PRHEX, SROM4_2G_PA + 2, 0xffff},
	{"pa2gw3a", 0x000000f0, SRFL_PRHEX, SROM4_2G_PA + 3, 0xffff},
	{"maxp5ga", 0x000000f0, 0, SROM4_5G_ITT_MAXP, 0x00ff},
	{"maxp5gha", 0x000000f0, 0, SROM4_5GLH_MAXP, 0x00ff},
	{"maxp5gla", 0x000000f0, 0, SROM4_5GLH_MAXP, 0xff00},
	{"pa5gw0a", 0x000000f0, SRFL_PRHEX, SROM4_5G_PA, 0xffff},
	{"pa5gw1a", 0x000000f0, SRFL_PRHEX, SROM4_5G_PA + 1, 0xffff},
	{"pa5gw2a", 0x000000f0, SRFL_PRHEX, SROM4_5G_PA + 2, 0xffff},
	{"pa5gw3a", 0x000000f0, SRFL_PRHEX, SROM4_5G_PA + 3, 0xffff},
	{"pa5glw0a", 0x000000f0, SRFL_PRHEX, SROM4_5GL_PA, 0xffff},
	{"pa5glw1a", 0x000000f0, SRFL_PRHEX, SROM4_5GL_PA + 1, 0xffff},
	{"pa5glw2a", 0x000000f0, SRFL_PRHEX, SROM4_5GL_PA + 2, 0xffff},
	{"pa5glw3a", 0x000000f0, SRFL_PRHEX, SROM4_5GL_PA + 3, 0xffff},
	{"pa5ghw0a", 0x000000f0, SRFL_PRHEX, SROM4_5GH_PA, 0xffff},
	{"pa5ghw1a", 0x000000f0, SRFL_PRHEX, SROM4_5GH_PA + 1, 0xffff},
	{"pa5ghw2a", 0x000000f0, SRFL_PRHEX, SROM4_5GH_PA + 2, 0xffff},
	{"pa5ghw3a", 0x000000f0, SRFL_PRHEX, SROM4_5GH_PA + 3, 0xffff},
	{"maxp2ga", 0xffffff00, 0, SROM8_2G_ITT_MAXP, 0x00ff},
	{"itt2ga", 0xffffff00, 0, SROM8_2G_ITT_MAXP, 0xff00},
	{"itt5ga", 0xffffff00, 0, SROM8_5G_ITT_MAXP, 0xff00},
	{"pa2gw0a", 0xffffff00, SRFL_PRHEX, SROM8_2G_PA, 0xffff},
	{"pa2gw1a", 0xffffff00, SRFL_PRHEX, SROM8_2G_PA + 1, 0xffff},
	{"pa2gw2a", 0xffffff00, SRFL_PRHEX, SROM8_2G_PA + 2, 0xffff},
	{"maxp5ga", 0xffffff00, 0, SROM8_5G_ITT_MAXP, 0x00ff},
	{"maxp5gha", 0xffffff00, 0, SROM8_5GLH_MAXP, 0x00ff},
	{"maxp5gla", 0xffffff00, 0, SROM8_5GLH_MAXP, 0xff00},
	{"pa5gw0a", 0xffffff00, SRFL_PRHEX, SROM8_5G_PA, 0xffff},
	{"pa5gw1a", 0xffffff00, SRFL_PRHEX, SROM8_5G_PA + 1, 0xffff},
	{"pa5gw2a", 0xffffff00, SRFL_PRHEX, SROM8_5G_PA + 2, 0xffff},
	{"pa5glw0a", 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA, 0xffff},
	{"pa5glw1a", 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA + 1, 0xffff},
	{"pa5glw2a", 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA + 2, 0xffff},
	{"pa5ghw0a", 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA, 0xffff},
	{"pa5ghw1a", 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA + 1, 0xffff},
	{"pa5ghw2a", 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA + 2, 0xffff},
	{NULL, 0, 0, 0, 0}
};

static int initvars_srom_si(si_t *sih, void *curmap, char **vars, uint *count);
static void _initvars_srom_pci(u8 sromrev, u16 *srom, uint off, varbuf_t *b);
static int initvars_srom_pci(si_t *sih, void *curmap, char **vars, uint *count);
static int initvars_flash_si(si_t *sih, char **vars, uint *count);
static int sprom_read_pci(si_t *sih, u16 *sprom,
			  uint wordoff, u16 *buf, uint nwords, bool check_crc);
#if defined(BCMNVRAMR)
static int otp_read_pci(si_t *sih, u16 *buf, uint bufsz);
#endif
static u16 srom_cc_cmd(si_t *sih, void *ccregs, u32 cmd,
			  uint wordoff, u16 data);

static int initvars_table(char *start, char *end,
			  char **vars, uint *count);
static int initvars_flash(si_t *sih, char **vp,
			  uint len);

/* Initialization of varbuf structure */
static void varbuf_init(varbuf_t *b, char *buf, uint size)
{
	b->size = size;
	b->base = b->buf = buf;
}

/* append a null terminated var=value string */
static int varbuf_append(varbuf_t *b, const char *fmt, ...)
{
	va_list ap;
	int r;
	size_t len;
	char *s;

	if (b->size < 2)
		return 0;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);
	va_end(ap);

	/* C99 snprintf behavior returns r >= size on overflow,
	 * others return -1 on overflow.
	 * All return -1 on format error.
	 * We need to leave room for 2 null terminations, one for the current var
	 * string, and one for final null of the var table. So check that the
	 * strlen written, r, leaves room for 2 chars.
	 */
	if ((r == -1) || (r > (int)(b->size - 2))) {
		b->size = 0;
		return 0;
	}

	/* Remove any earlier occurrence of the same variable */
	s = strchr(b->buf, '=');
	if (s != NULL) {
		len = (size_t) (s - b->buf);
		for (s = b->base; s < b->buf;) {
			if ((memcmp(s, b->buf, len) == 0) && s[len] == '=') {
				len = strlen(s) + 1;
				memmove(s, (s + len),
					((b->buf + r + 1) - (s + len)));
				b->buf -= len;
				b->size += (unsigned int)len;
				break;
			}

			while (*s++)
				;
		}
	}

	/* skip over this string's null termination */
	r++;
	b->size -= r;
	b->buf += r;

	return r;
}

/*
 * Initialize local vars from the right source for this platform.
 * Return 0 on success, nonzero on error.
 */
int srom_var_init(si_t *sih, uint bustype, void *curmap,
		  char **vars, uint *count)
{
	uint len;

	len = 0;

	if (vars == NULL || count == NULL)
		return 0;

	*vars = NULL;
	*count = 0;

	switch (bustype) {
	case SI_BUS:
	case JTAG_BUS:
		return initvars_srom_si(sih, curmap, vars, count);

	case PCI_BUS:
		if (curmap == NULL)
			return -1;

		return initvars_srom_pci(sih, curmap, vars, count);

	default:
		break;
	}
	return -1;
}

/* In chips with chipcommon rev 32 and later, the srom is in chipcommon,
 * not in the bus cores.
 */
static u16
srom_cc_cmd(si_t *sih, void *ccregs, u32 cmd,
	    uint wordoff, u16 data)
{
	chipcregs_t *cc = (chipcregs_t *) ccregs;
	uint wait_cnt = 1000;

	if ((cmd == SRC_OP_READ) || (cmd == SRC_OP_WRITE)) {
		W_REG(&cc->sromaddress, wordoff * 2);
		if (cmd == SRC_OP_WRITE)
			W_REG(&cc->sromdata, data);
	}

	W_REG(&cc->sromcontrol, SRC_START | cmd);

	while (wait_cnt--) {
		if ((R_REG(&cc->sromcontrol) & SRC_BUSY) == 0)
			break;
	}

	if (!wait_cnt) {
		return 0xffff;
	}
	if (cmd == SRC_OP_READ)
		return (u16) R_REG(&cc->sromdata);
	else
		return 0xffff;
}

static inline void ltoh16_buf(u16 *buf, unsigned int size)
{
	for (size /= 2; size; size--)
		*(buf + size) = le16_to_cpu(*(buf + size));
}

static inline void htol16_buf(u16 *buf, unsigned int size)
{
	for (size /= 2; size; size--)
		*(buf + size) = cpu_to_le16(*(buf + size));
}

/*
 * Read in and validate sprom.
 * Return 0 on success, nonzero on error.
 */
static int
sprom_read_pci(si_t *sih, u16 *sprom, uint wordoff,
	       u16 *buf, uint nwords, bool check_crc)
{
	int err = 0;
	uint i;
	void *ccregs = NULL;

	/* read the sprom */
	for (i = 0; i < nwords; i++) {

		if (sih->ccrev > 31 && ISSIM_ENAB(sih)) {
			/* use indirect since direct is too slow on QT */
			if ((sih->cccaps & CC_CAP_SROM) == 0)
				return 1;

			ccregs = (void *)((u8 *) sprom - CC_SROM_OTP);
			buf[i] =
			    srom_cc_cmd(sih, ccregs, SRC_OP_READ,
					wordoff + i, 0);

		} else {
			if (ISSIM_ENAB(sih))
				buf[i] = R_REG(&sprom[wordoff + i]);

			buf[i] = R_REG(&sprom[wordoff + i]);
		}

	}

	/* bypass crc checking for simulation to allow srom hack */
	if (ISSIM_ENAB(sih))
		return err;

	if (check_crc) {

		if (buf[0] == 0xffff) {
			/* The hardware thinks that an srom that starts with 0xffff
			 * is blank, regardless of the rest of the content, so declare
			 * it bad.
			 */
			return 1;
		}

		/* fixup the endianness so crc8 will pass */
		htol16_buf(buf, nwords * 2);
		if (bcm_crc8((u8 *) buf, nwords * 2, CRC8_INIT_VALUE) !=
		    CRC8_GOOD_VALUE) {
			/* DBG only pci always read srom4 first, then srom8/9 */
			err = 1;
		}
		/* now correct the endianness of the byte array */
		ltoh16_buf(buf, nwords * 2);
	}
	return err;
}

#if defined(BCMNVRAMR)
static int otp_read_pci(si_t *sih, u16 *buf, uint bufsz)
{
	u8 *otp;
	uint sz = OTP_SZ_MAX / 2;	/* size in words */
	int err = 0;

	otp = kzalloc(OTP_SZ_MAX, GFP_ATOMIC);
	if (otp == NULL) {
		return -EBADE;
	}

	err = otp_read_region(sih, OTP_HW_RGN, (u16 *) otp, &sz);

	memcpy(buf, otp, bufsz);

	kfree(otp);

	/* Check CRC */
	if (buf[0] == 0xffff) {
		/* The hardware thinks that an srom that starts with 0xffff
		 * is blank, regardless of the rest of the content, so declare
		 * it bad.
		 */
		return 1;
	}

	/* fixup the endianness so crc8 will pass */
	htol16_buf(buf, bufsz);
	if (bcm_crc8((u8 *) buf, SROM4_WORDS * 2, CRC8_INIT_VALUE) !=
	    CRC8_GOOD_VALUE) {
		err = 1;
	}
	/* now correct the endianness of the byte array */
	ltoh16_buf(buf, bufsz);

	return err;
}
#endif				/* defined(BCMNVRAMR) */
/*
* Create variable table from memory.
* Return 0 on success, nonzero on error.
*/
static int initvars_table(char *start, char *end,
			  char **vars, uint *count)
{
	int c = (int)(end - start);

	/* do it only when there is more than just the null string */
	if (c > 1) {
		char *vp = kmalloc(c, GFP_ATOMIC);
		if (!vp)
			return -ENOMEM;
		memcpy(vp, start, c);
		*vars = vp;
		*count = c;
	} else {
		*vars = NULL;
		*count = 0;
	}

	return 0;
}

/*
 * Find variables with <devpath> from flash. 'base' points to the beginning
 * of the table upon enter and to the end of the table upon exit when success.
 * Return 0 on success, nonzero on error.
 */
static int initvars_flash(si_t *sih, char **base, uint len)
{
	char *vp = *base;
	char *flash;
	int err;
	char *s;
	uint l, dl, copy_len;
	char devpath[SI_DEVPATH_BUFSZ];

	/* allocate memory and read in flash */
	flash = kmalloc(NVRAM_SPACE, GFP_ATOMIC);
	if (!flash)
		return -ENOMEM;
	err = nvram_getall(flash, NVRAM_SPACE);
	if (err)
		goto exit;

	ai_devpath(sih, devpath, sizeof(devpath));

	/* grab vars with the <devpath> prefix in name */
	dl = strlen(devpath);
	for (s = flash; s && *s; s += l + 1) {
		l = strlen(s);

		/* skip non-matching variable */
		if (strncmp(s, devpath, dl))
			continue;

		/* is there enough room to copy? */
		copy_len = l - dl + 1;
		if (len < copy_len) {
			err = -EOVERFLOW;
			goto exit;
		}

		/* no prefix, just the name=value */
		strncpy(vp, &s[dl], copy_len);
		vp += copy_len;
		len -= copy_len;
	}

	/* add null string as terminator */
	if (len < 1) {
		err = -EOVERFLOW;
		goto exit;
	}
	*vp++ = '\0';

	*base = vp;

 exit:	kfree(flash);
	return err;
}

/*
 * Initialize nonvolatile variable table from flash.
 * Return 0 on success, nonzero on error.
 */
static int initvars_flash_si(si_t *sih, char **vars, uint *count)
{
	char *vp, *base;
	int err;

	base = vp = kmalloc(MAXSZ_NVRAM_VARS, GFP_ATOMIC);
	if (!vp)
		return -ENOMEM;

	err = initvars_flash(sih, &vp, MAXSZ_NVRAM_VARS);
	if (err == 0)
		err = initvars_table(base, vp, vars, count);

	kfree(base);

	return err;
}

/* Parse SROM and create name=value pairs. 'srom' points to
 * the SROM word array. 'off' specifies the offset of the
 * first word 'srom' points to, which should be either 0 or
 * SROM3_SWRG_OFF (full SROM or software region).
 */

static uint mask_shift(u16 mask)
{
	uint i;
	for (i = 0; i < (sizeof(mask) << 3); i++) {
		if (mask & (1 << i))
			return i;
	}
	return 0;
}

static uint mask_width(u16 mask)
{
	int i;
	for (i = (sizeof(mask) << 3) - 1; i >= 0; i--) {
		if (mask & (1 << i))
			return (uint) (i - mask_shift(mask) + 1);
	}
	return 0;
}

static void _initvars_srom_pci(u8 sromrev, u16 *srom, uint off, varbuf_t *b)
{
	u16 w;
	u32 val;
	const sromvar_t *srv;
	uint width;
	uint flags;
	u32 sr = (1 << sromrev);

	varbuf_append(b, "sromrev=%d", sromrev);

	for (srv = pci_sromvars; srv->name != NULL; srv++) {
		const char *name;

		if ((srv->revmask & sr) == 0)
			continue;

		if (srv->off < off)
			continue;

		flags = srv->flags;
		name = srv->name;

		/* This entry is for mfgc only. Don't generate param for it, */
		if (flags & SRFL_NOVAR)
			continue;

		if (flags & SRFL_ETHADDR) {
			u8 ea[ETH_ALEN];

			ea[0] = (srom[srv->off - off] >> 8) & 0xff;
			ea[1] = srom[srv->off - off] & 0xff;
			ea[2] = (srom[srv->off + 1 - off] >> 8) & 0xff;
			ea[3] = srom[srv->off + 1 - off] & 0xff;
			ea[4] = (srom[srv->off + 2 - off] >> 8) & 0xff;
			ea[5] = srom[srv->off + 2 - off] & 0xff;

			varbuf_append(b, "%s=%pM", name, ea);
		} else {
			w = srom[srv->off - off];
			val = (w & srv->mask) >> mask_shift(srv->mask);
			width = mask_width(srv->mask);

			while (srv->flags & SRFL_MORE) {
				srv++;
				if (srv->off == 0 || srv->off < off)
					continue;

				w = srom[srv->off - off];
				val +=
				    ((w & srv->mask) >> mask_shift(srv->
								   mask)) <<
				    width;
				width += mask_width(srv->mask);
			}

			if ((flags & SRFL_NOFFS)
			    && ((int)val == (1 << width) - 1))
				continue;

			if (flags & SRFL_CCODE) {
				if (val == 0)
					varbuf_append(b, "ccode=");
				else
					varbuf_append(b, "ccode=%c%c",
						      (val >> 8), (val & 0xff));
			}
			/* LED Powersave duty cycle has to be scaled:
			 *(oncount >> 24) (offcount >> 8)
			 */
			else if (flags & SRFL_LEDDC) {
				u32 w32 = (((val >> 8) & 0xff) << 24) |	/* oncount */
				    (((val & 0xff)) << 8);	/* offcount */
				varbuf_append(b, "leddc=%d", w32);
			} else if (flags & SRFL_PRHEX)
				varbuf_append(b, "%s=0x%x", name, val);
			else if ((flags & SRFL_PRSIGN)
				 && (val & (1 << (width - 1))))
				varbuf_append(b, "%s=%d", name,
					      (int)(val | (~0 << width)));
			else
				varbuf_append(b, "%s=%u", name, val);
		}
	}

	if (sromrev >= 4) {
		/* Do per-path variables */
		uint p, pb, psz;

		if (sromrev >= 8) {
			pb = SROM8_PATH0;
			psz = SROM8_PATH1 - SROM8_PATH0;
		} else {
			pb = SROM4_PATH0;
			psz = SROM4_PATH1 - SROM4_PATH0;
		}

		for (p = 0; p < MAX_PATH_SROM; p++) {
			for (srv = perpath_pci_sromvars; srv->name != NULL;
			     srv++) {
				if ((srv->revmask & sr) == 0)
					continue;

				if (pb + srv->off < off)
					continue;

				/* This entry is for mfgc only. Don't generate param for it, */
				if (srv->flags & SRFL_NOVAR)
					continue;

				w = srom[pb + srv->off - off];
				val = (w & srv->mask) >> mask_shift(srv->mask);
				width = mask_width(srv->mask);

				/* Cheating: no per-path var is more than 1 word */

				if ((srv->flags & SRFL_NOFFS)
				    && ((int)val == (1 << width) - 1))
					continue;

				if (srv->flags & SRFL_PRHEX)
					varbuf_append(b, "%s%d=0x%x", srv->name,
						      p, val);
				else
					varbuf_append(b, "%s%d=%d", srv->name,
						      p, val);
			}
			pb += psz;
		}
	}
}

/*
 * Initialize nonvolatile variable table from sprom.
 * Return 0 on success, nonzero on error.
 */
static int initvars_srom_pci(si_t *sih, void *curmap, char **vars, uint *count)
{
	u16 *srom, *sromwindow;
	u8 sromrev = 0;
	u32 sr;
	varbuf_t b;
	char *vp, *base = NULL;
	bool flash = false;
	int err = 0;

	/*
	 * Apply CRC over SROM content regardless SROM is present or not,
	 * and use variable <devpath>sromrev's existence in flash to decide
	 * if we should return an error when CRC fails or read SROM variables
	 * from flash.
	 */
	srom = kmalloc(SROM_MAX, GFP_ATOMIC);
	if (!srom)
		return -2;

	sromwindow = (u16 *) SROM_OFFSET(sih);
	if (ai_is_sprom_available(sih)) {
		err =
		    sprom_read_pci(sih, sromwindow, 0, srom, SROM_WORDS,
				   true);

		if ((srom[SROM4_SIGN] == SROM4_SIGNATURE) ||
		    (((sih->buscoretype == PCIE_CORE_ID)
		      && (sih->buscorerev >= 6))
		     || ((sih->buscoretype == PCI_CORE_ID)
			 && (sih->buscorerev >= 0xe)))) {
			/* sromrev >= 4, read more */
			err =
			    sprom_read_pci(sih, sromwindow, 0, srom,
					   SROM4_WORDS, true);
			sromrev = srom[SROM4_CRCREV] & 0xff;
		} else if (err == 0) {
			/* srom is good and is rev < 4 */
			/* top word of sprom contains version and crc8 */
			sromrev = srom[SROM_CRCREV] & 0xff;
			/* bcm4401 sroms misprogrammed */
			if (sromrev == 0x10)
				sromrev = 1;
		}
	}
#if defined(BCMNVRAMR)
	/* Use OTP if SPROM not available */
	else {
		err = otp_read_pci(sih, srom, SROM_MAX);
		if (err == 0)
			/* OTP only contain SROM rev8/rev9 for now */
			sromrev = srom[SROM4_CRCREV] & 0xff;
		else
			err = 1;
	}
#else
	else
		err = 1;
#endif

	/*
	 * We want internal/wltest driver to come up with default
	 * sromvars so we can program a blank SPROM/OTP.
	 */
	if (err) {
		char *value;
		u32 val;
		val = 0;

		value = ai_getdevpathvar(sih, "sromrev");
		if (value) {
			sromrev = (u8) simple_strtoul(value, NULL, 0);
			flash = true;
			goto varscont;
		}

		value = ai_getnvramflvar(sih, "sromrev");
		if (value) {
			err = 0;
			goto errout;
		}

		{
			err = -1;
			goto errout;
		}
	}

 varscont:
	/* Bitmask for the sromrev */
	sr = 1 << sromrev;

	/* srom version check: Current valid versions: 1, 2, 3, 4, 5, 8, 9 */
	if ((sr & 0x33e) == 0) {
		err = -2;
		goto errout;
	}

	base = vp = kmalloc(MAXSZ_NVRAM_VARS, GFP_ATOMIC);
	if (!vp) {
		err = -2;
		goto errout;
	}

	/* read variables from flash */
	if (flash) {
		err = initvars_flash(sih, &vp, MAXSZ_NVRAM_VARS);
		if (err)
			goto errout;
		goto varsdone;
	}

	varbuf_init(&b, base, MAXSZ_NVRAM_VARS);

	/* parse SROM into name=value pairs. */
	_initvars_srom_pci(sromrev, srom, 0, &b);

	/* final nullbyte terminator */
	vp = b.buf;
	*vp++ = '\0';

 varsdone:
	err = initvars_table(base, vp, vars, count);

 errout:
	if (base)
		kfree(base);

	kfree(srom);
	return err;
}


static int initvars_srom_si(si_t *sih, void *curmap, char **vars, uint *varsz)
{
	/* Search flash nvram section for srom variables */
	return initvars_flash_si(sih, vars, varsz);
}
