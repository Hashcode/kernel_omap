/*
 * linux/sound/soc-fw.h -- ALSA SoC Firmware Controls and DAPM
 *
 * Copyright:	2012 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Simple file API to load FW that includes mixers, coefficients, DAPM graphs,
 * algorithms, equalisers, DAIs, widgets etc.
 */

#ifndef __LINUX_SND_SOC_FW_H
#define __LINUX_SND_SOC_FW_H

#include <uapi/sound/asoc.h>

struct firmware;

/*
 * Kcontrol operations - used to map handlers onto firmware based controls.
 */
struct snd_soc_fw_kcontrol_ops {
	u32 id;
	int (*get)(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol);
	int (*put)(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol);
	int (*info)(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo);
};

/*
 * Public API - Used by component drivers to load new mixers, DAPM, vendor
 * specific data.
 */
struct snd_soc_fw_codec_ops {

	/* external kcontrol init - can be used to set ext funcs + pdata */
	int (*control_load) (struct snd_soc_codec *, struct snd_kcontrol_new *);

	/* external widget init - can be used to set ext funcs + pdata */
	int (*widget_load) (struct snd_soc_codec *, struct snd_soc_dapm_widget *);

	/* callback to handle vendor data */
	int (*vendor_load) (struct snd_soc_codec *, struct snd_soc_fw_hdr *);
	int (*vendor_unload) (struct snd_soc_codec *, struct snd_soc_fw_hdr *);

	/* completion - called at completion of firmware loading */
	void (*complete) (struct snd_soc_codec *);

	/* kcontrols operations */
	const struct snd_soc_fw_kcontrol_ops *io_ops;
	int io_ops_count;
};

struct snd_soc_fw_platform_ops {

	/* external kcontrol init - can be used to set ext funcs + pdata */
	int (*control_load) (struct snd_soc_platform *, struct snd_kcontrol_new *);

	/* external widget init - can be used to set ext funcs + pdata */
	int (*widget_load) (struct snd_soc_platform *, struct snd_soc_dapm_widget *);

	/* callback to handle vendor data */
	int (*vendor_load) (struct snd_soc_platform *, struct snd_soc_fw_hdr *);
	int (*vendor_unload) (struct snd_soc_platform *, struct snd_soc_fw_hdr *);

	/* completion - called at completion of firmware loading */
	void (*complete) (struct snd_soc_platform *);

	/* kcontrols operations */
	const struct snd_soc_fw_kcontrol_ops *io_ops;
	int io_ops_count;
};

struct snd_soc_fw_card_ops {

	/* external kcontrol init - can be used to set ext funcs + pdata */
	int (*control_load) (struct snd_soc_card *, struct snd_kcontrol_new *);

	/* external widget init - can be used to set ext funcs + pdata */
	int (*widget_load) (struct snd_soc_card *, struct snd_soc_dapm_widget *);

	/* callback to handle vendor data */
	int (*vendor_load) (struct snd_soc_card *, struct snd_soc_fw_hdr *);
	int (*vendor_unload) (struct snd_soc_card *, struct snd_soc_fw_hdr *);

	/* completion */
	void (*complete) (struct snd_soc_card *);

	/* kcontrols operations */
	const struct snd_soc_fw_kcontrol_ops *io_ops;
	int io_ops_count;
};

/* gets a pointer to data from the firmware block header */
static inline const void *snd_soc_fw_get_data(struct snd_soc_fw_hdr *hdr)
{
	const void *ptr = hdr;

	return ptr + sizeof(*hdr);
}

/* Firmware loading for component drivers */
int snd_soc_fw_load_card(struct snd_soc_card *card,
	struct snd_soc_fw_card_ops *ops, const struct firmware *fw,
	u32 index);
int snd_soc_fw_load_platform(struct snd_soc_platform *platform,
	struct snd_soc_fw_platform_ops *ops, const struct firmware *fw,
	u32 index);
int snd_soc_fw_load_codec(struct snd_soc_codec *codec,
	struct snd_soc_fw_codec_ops *ops, const struct firmware *fw,
	u32 index);

#define SND_SOC_FW_INDEX_ALL	0	/* index that matches all FW objects */

/* Firmware based dynamic widget and assoc kcontrol removal */
void snd_soc_fw_dcontrols_remove_widgets(struct snd_soc_dapm_context *dapm,
	u32 index);
void snd_soc_fw_dcontrols_remove_widget(struct snd_soc_dapm_widget *w);

/* Firmware based dynamic kcontrol removal for components */
void snd_soc_fw_dcontrols_remove_codec(struct snd_soc_codec *codec, u32 index);
void snd_soc_fw_dcontrols_remove_platform(struct snd_soc_platform *platform, u32 index);
void snd_soc_fw_dcontrols_remove_card(struct snd_soc_card *soc_card, u32 index);
int snd_soc_fw_dcontrols_remove_all(struct snd_soc_card *soc_card, u32 index);

#endif
