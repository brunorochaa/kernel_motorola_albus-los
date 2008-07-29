/*
 * linux/sound/soc.h -- ALSA SoC Layer
 *
 * Author:		Liam Girdwood
 * Created:		Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_SOC_H
#define __LINUX_SND_SOC_H

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>

#define SND_SOC_VERSION "0.13.2"

/*
 * Convenience kcontrol builders
 */
#define SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .shift = xshift, .max = xmax, .invert = xinvert})
#define SOC_SINGLE_VALUE_EXT(xreg, xmax, xinvert) \
	((unsigned long)&(struct soc_mixer_control) \
	{.reg = xreg, .max = xmax, .invert = xinvert})
#define SOC_SINGLE(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_SINGLE_TLV(xname, reg, shift, max, invert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = snd_soc_put_volsw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }
#define SOC_DOUBLE(xname, xreg, shift_left, shift_right, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right, \
		 .max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R(xname, reg_left, reg_right, xshift, xmax, xinvert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_2r, .put = snd_soc_put_volsw_2r, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_TLV(xname, xreg, shift_left, shift_right, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw, \
	.put = snd_soc_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right,\
		 .max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R_TLV(xname, reg_left, reg_right, xshift, xmax, xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_2r, .put = snd_soc_put_volsw_2r, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_S8_TLV(xname, xreg, xmin, xmax, tlv_array) \
{	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p  = (tlv_array), \
	.info   = snd_soc_info_volsw_s8, .get = snd_soc_get_volsw_s8, \
	.put    = snd_soc_put_volsw_s8, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .min = xmin, .max = xmax} }
#define SOC_ENUM_DOUBLE(xreg, xshift_l, xshift_r, xmax, xtexts) \
{	.reg = xreg, .shift_l = xshift_l, .shift_r = xshift_r, \
	.max = xmax, .texts = xtexts }
#define SOC_ENUM_SINGLE(xreg, xshift, xmax, xtexts) \
	SOC_ENUM_DOUBLE(xreg, xshift, xshift, xmax, xtexts)
#define SOC_ENUM_SINGLE_EXT(xmax, xtexts) \
{	.max = xmax, .texts = xtexts }
#define SOC_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,\
	.info = snd_soc_info_enum_double, \
	.get = snd_soc_get_enum_double, .put = snd_soc_put_enum_double, \
	.private_value = (unsigned long)&xenum }
#define SOC_SINGLE_EXT(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) }
#define SOC_SINGLE_EXT_TLV(xname, xreg, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_SINGLE_VALUE(xreg, xshift, xmax, xinvert) }
#define SOC_SINGLE_BOOL_EXT(xname, xdata, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_bool_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = xdata }
#define SOC_ENUM_EXT(xname, xenum, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&xenum }

/*
 * Bias levels
 *
 * @ON:      Bias is fully on for audio playback and capture operations.
 * @PREPARE: Prepare for audio operations. Called before DAPM switching for
 *           stream start and stop operations.
 * @STANDBY: Low power standby state when no playback/capture operations are
 *           in progress. NOTE: The transition time between STANDBY and ON
 *           should be as fast as possible and no longer than 10ms.
 * @OFF:     Power Off. No restrictions on transition times.
 */
enum snd_soc_bias_level {
	SND_SOC_BIAS_ON,
	SND_SOC_BIAS_PREPARE,
	SND_SOC_BIAS_STANDBY,
	SND_SOC_BIAS_OFF,
};

/*
 * Digital Audio Interface (DAI) types
 */
#define SND_SOC_DAI_AC97	0x1
#define SND_SOC_DAI_I2S		0x2
#define SND_SOC_DAI_PCM		0x4
#define SND_SOC_DAI_AC97_BUS	0x8	/* for custom i.e. non ac97_codec.c */

/*
 * DAI hardware audio formats
 */
#define SND_SOC_DAIFMT_I2S		0	/* I2S mode */
#define SND_SOC_DAIFMT_RIGHT_J	1	/* Right justified mode */
#define SND_SOC_DAIFMT_LEFT_J	2	/* Left Justified mode */
#define SND_SOC_DAIFMT_DSP_A	3	/* L data msb after FRM or LRC */
#define SND_SOC_DAIFMT_DSP_B	4	/* L data msb during FRM or LRC */
#define SND_SOC_DAIFMT_AC97		5	/* AC97 */

#define SND_SOC_DAIFMT_MSB 	SND_SOC_DAIFMT_LEFT_J
#define SND_SOC_DAIFMT_LSB	SND_SOC_DAIFMT_RIGHT_J

/*
 * DAI Gating
 */
#define SND_SOC_DAIFMT_CONT			(0 << 4)	/* continuous clock */
#define SND_SOC_DAIFMT_GATED		(1 << 4)	/* clock is gated when not Tx/Rx */

/*
 * DAI Sync
 * Synchronous LR (Left Right) clocks and Frame signals.
 */
#define SND_SOC_DAIFMT_SYNC		(0 << 5)	/* Tx FRM = Rx FRM */
#define SND_SOC_DAIFMT_ASYNC		(1 << 5)	/* Tx FRM ~ Rx FRM */

/*
 * TDM
 */
#define SND_SOC_DAIFMT_TDM		(1 << 6)

/*
 * DAI hardware signal inversions
 */
#define SND_SOC_DAIFMT_NB_NF		(0 << 8)	/* normal bclk + frm */
#define SND_SOC_DAIFMT_NB_IF		(1 << 8)	/* normal bclk + inv frm */
#define SND_SOC_DAIFMT_IB_NF		(2 << 8)	/* invert bclk + nor frm */
#define SND_SOC_DAIFMT_IB_IF		(3 << 8)	/* invert bclk + frm */

/*
 * DAI hardware clock masters
 * This is wrt the codec, the inverse is true for the interface
 * i.e. if the codec is clk and frm master then the interface is
 * clk and frame slave.
 */
#define SND_SOC_DAIFMT_CBM_CFM	(0 << 12) /* codec clk & frm master */
#define SND_SOC_DAIFMT_CBS_CFM	(1 << 12) /* codec clk slave & frm master */
#define SND_SOC_DAIFMT_CBM_CFS	(2 << 12) /* codec clk master & frame slave */
#define SND_SOC_DAIFMT_CBS_CFS	(3 << 12) /* codec clk & frm slave */

#define SND_SOC_DAIFMT_FORMAT_MASK		0x000f
#define SND_SOC_DAIFMT_CLOCK_MASK		0x00f0
#define SND_SOC_DAIFMT_INV_MASK			0x0f00
#define SND_SOC_DAIFMT_MASTER_MASK		0xf000


/*
 * Master Clock Directions
 */
#define SND_SOC_CLOCK_IN	0
#define SND_SOC_CLOCK_OUT	1

/*
 * AC97 codec ID's bitmask
 */
#define SND_SOC_DAI_AC97_ID0	(1 << 0)
#define SND_SOC_DAI_AC97_ID1	(1 << 1)
#define SND_SOC_DAI_AC97_ID2	(1 << 2)
#define SND_SOC_DAI_AC97_ID3	(1 << 3)

struct snd_soc_device;
struct snd_soc_pcm_stream;
struct snd_soc_ops;
struct snd_soc_dai_mode;
struct snd_soc_pcm_runtime;
struct snd_soc_dai;
struct snd_soc_codec;
struct snd_soc_machine_config;
struct soc_enum;
struct snd_soc_ac97_ops;
struct snd_soc_clock_info;

typedef int (*hw_write_t)(void *,const char* ,int);
typedef int (*hw_read_t)(void *,char* ,int);

extern struct snd_ac97_bus_ops soc_ac97_ops;

/* pcm <-> DAI connect */
void snd_soc_free_pcms(struct snd_soc_device *socdev);
int snd_soc_new_pcms(struct snd_soc_device *socdev, int idx, const char *xid);
int snd_soc_register_card(struct snd_soc_device *socdev);

/* set runtime hw params */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw);

/* codec IO */
#define snd_soc_read(codec, reg) codec->read(codec, reg)
#define snd_soc_write(codec, reg, value) codec->write(codec, reg, value)

/* codec register bit access */
int snd_soc_update_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned short mask, unsigned short value);
int snd_soc_test_bits(struct snd_soc_codec *codec, unsigned short reg,
				unsigned short mask, unsigned short value);

int snd_soc_new_ac97_codec(struct snd_soc_codec *codec,
	struct snd_ac97_bus_ops *ops, int num);
void snd_soc_free_ac97_codec(struct snd_soc_codec *codec);

/* Digital Audio Interface clocking API.*/
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir);

int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div);

int snd_soc_dai_set_pll(struct snd_soc_dai *dai,
	int pll_id, unsigned int freq_in, unsigned int freq_out);

/* Digital Audio interface formatting */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt);

int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int mask, int slots);

int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate);

/* Digital Audio Interface mute */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute);

/*
 *Controls
 */
struct snd_kcontrol *snd_soc_cnew(const struct snd_kcontrol_new *_template,
	void *data, char *long_name);
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_info_enum_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_info_volsw_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
#define snd_soc_info_bool_ext		snd_ctl_boolean_mono_info
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_info_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo);
int snd_soc_get_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int snd_soc_put_volsw_s8(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);

/* SoC PCM stream information */
struct snd_soc_pcm_stream {
	char *stream_name;
	u64 formats;			/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int rate_min;		/* min rate */
	unsigned int rate_max;		/* max rate */
	unsigned int channels_min;	/* min channels */
	unsigned int channels_max;	/* max channels */
	unsigned int active:1;		/* stream is in use */
};

/* SoC audio ops */
struct snd_soc_ops {
	int (*startup)(struct snd_pcm_substream *);
	void (*shutdown)(struct snd_pcm_substream *);
	int (*hw_params)(struct snd_pcm_substream *, struct snd_pcm_hw_params *);
	int (*hw_free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*trigger)(struct snd_pcm_substream *, int);
};

/* ASoC DAI ops */
struct snd_soc_dai_ops {
	/* DAI clocking configuration */
	int (*set_sysclk)(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir);
	int (*set_pll)(struct snd_soc_dai *dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out);
	int (*set_clkdiv)(struct snd_soc_dai *dai, int div_id, int div);

	/* DAI format configuration */
	int (*set_fmt)(struct snd_soc_dai *dai, unsigned int fmt);
	int (*set_tdm_slot)(struct snd_soc_dai *dai,
		unsigned int mask, int slots);
	int (*set_tristate)(struct snd_soc_dai *dai, int tristate);

	/* digital mute */
	int (*digital_mute)(struct snd_soc_dai *dai, int mute);
};

/* SoC  DAI (Digital Audio Interface) */
struct snd_soc_dai {
	/* DAI description */
	char *name;
	unsigned int id;
	unsigned char type;

	/* DAI callbacks */
	int (*probe)(struct platform_device *pdev,
		     struct snd_soc_dai *dai);
	void (*remove)(struct platform_device *pdev,
		       struct snd_soc_dai *dai);
	int (*suspend)(struct platform_device *pdev,
		struct snd_soc_dai *dai);
	int (*resume)(struct platform_device *pdev,
		struct snd_soc_dai *dai);

	/* ops */
	struct snd_soc_ops ops;
	struct snd_soc_dai_ops dai_ops;

	/* DAI capabilities */
	struct snd_soc_pcm_stream capture;
	struct snd_soc_pcm_stream playback;

	/* DAI runtime info */
	struct snd_pcm_runtime *runtime;
	struct snd_soc_codec *codec;
	unsigned int active;
	unsigned char pop_wait:1;
	void *dma_data;

	/* DAI private data */
	void *private_data;
};

/* SoC Audio Codec */
struct snd_soc_codec {
	char *name;
	struct module *owner;
	struct mutex mutex;

	/* callbacks */
	int (*set_bias_level)(struct snd_soc_codec *,
			      enum snd_soc_bias_level level);

	/* runtime */
	struct snd_card *card;
	struct snd_ac97 *ac97;  /* for ad-hoc ac97 devices */
	unsigned int active;
	unsigned int pcm_devs;
	void *private_data;

	/* codec IO */
	void *control_data; /* codec control (i2c/3wire) data */
	unsigned int (*read)(struct snd_soc_codec *, unsigned int);
	int (*write)(struct snd_soc_codec *, unsigned int, unsigned int);
	int (*display_register)(struct snd_soc_codec *, char *,
				size_t, unsigned int);
	hw_write_t hw_write;
	hw_read_t hw_read;
	void *reg_cache;
	short reg_cache_size;
	short reg_cache_step;

	/* dapm */
	struct list_head dapm_widgets;
	struct list_head dapm_paths;
	enum snd_soc_bias_level bias_level;
	enum snd_soc_bias_level suspend_bias_level;
	struct delayed_work delayed_work;

	/* codec DAI's */
	struct snd_soc_dai *dai;
	unsigned int num_dai;
};

/* codec device */
struct snd_soc_codec_device {
	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);
	int (*suspend)(struct platform_device *pdev, pm_message_t state);
	int (*resume)(struct platform_device *pdev);
};

/* SoC platform interface */
struct snd_soc_platform {
	char *name;

	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);
	int (*suspend)(struct platform_device *pdev,
		struct snd_soc_dai *dai);
	int (*resume)(struct platform_device *pdev,
		struct snd_soc_dai *dai);

	/* pcm creation and destruction */
	int (*pcm_new)(struct snd_card *, struct snd_soc_dai *,
		struct snd_pcm *);
	void (*pcm_free)(struct snd_pcm *);

	/* platform stream ops */
	struct snd_pcm_ops *pcm_ops;
};

/* SoC machine DAI configuration, glues a codec and cpu DAI together */
struct snd_soc_dai_link  {
	char *name;			/* Codec name */
	char *stream_name;		/* Stream name */

	/* DAI */
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;

	/* machine stream operations */
	struct snd_soc_ops *ops;

	/* codec/machine specific init - e.g. add machine controls */
	int (*init)(struct snd_soc_codec *codec);

	/* DAI pcm */
	struct snd_pcm *pcm;
};

/* SoC machine */
struct snd_soc_machine {
	char *name;

	int (*probe)(struct platform_device *pdev);
	int (*remove)(struct platform_device *pdev);

	/* the pre and post PM functions are used to do any PM work before and
	 * after the codec and DAI's do any PM work. */
	int (*suspend_pre)(struct platform_device *pdev, pm_message_t state);
	int (*suspend_post)(struct platform_device *pdev, pm_message_t state);
	int (*resume_pre)(struct platform_device *pdev);
	int (*resume_post)(struct platform_device *pdev);

	/* callbacks */
	int (*set_bias_level)(struct snd_soc_machine *,
			      enum snd_soc_bias_level level);

	/* CPU <--> Codec DAI links  */
	struct snd_soc_dai_link *dai_link;
	int num_links;
};

/* SoC Device - the audio subsystem */
struct snd_soc_device {
	struct device *dev;
	struct snd_soc_machine *machine;
	struct snd_soc_platform *platform;
	struct snd_soc_codec *codec;
	struct snd_soc_codec_device *codec_dev;
	struct delayed_work delayed_work;
	struct work_struct deferred_resume_work;
	void *codec_data;
};

/* runtime channel data */
struct snd_soc_pcm_runtime {
	struct snd_soc_dai_link *dai;
	struct snd_soc_device *socdev;
};

/* mixer control */
struct soc_mixer_control {
	int min, max;
	uint reg, rreg, shift, rshift, invert;
};

/* enumerated kcontrol */
struct soc_enum {
	unsigned short reg;
	unsigned short reg2;
	unsigned char shift_l;
	unsigned char shift_r;
	unsigned int max;
	const char **texts;
	void *dapm;
};

#endif
