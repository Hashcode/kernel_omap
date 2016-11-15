
/*
 * ti_hdmi_5xxx_ip.c
 *
 * HDMI TI OMAP5 IP driver Library
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Mythri pk <mythripk@ti.com>
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#if defined(CONFIG_OMAP5_DSS_HDMI_AUDIO)
#include <sound/asound.h>
#include <sound/asoundef.h>
#endif

#include "ti_hdmi_5xxx_ip.h"
#include "dss.h"

static inline void hdmi_write_reg(void __iomem *base_addr,
		const unsigned long idx, u32 val)
{
	__raw_writel(val, base_addr + idx);
}

static inline u32 hdmi_read_reg(void __iomem *base_addr,
		const unsigned long idx)
{
	return __raw_readl(base_addr + idx);
}

static inline void __iomem *hdmi_core_sys_base(struct hdmi_ip_data *ip_data)
{
	return ip_data->base_core;
}

static inline int hdmi_wait_for_bit_change(void __iomem *base_addr,
			const unsigned long idx,
			int b2, int b1, u32 val)
{
	u32 t = 0;
	while (val != REG_GET(base_addr, idx, b2, b1)) {
		udelay(1);
		if (t++ > 10000)
			return !val;
	}
	return val;
}

static inline void hdmi_core_ddc_req_addr(struct hdmi_ip_data *ip_data,
						u8 addr, int ext)
{
	u8 seg_ptr = ext / 2;
	u8 edidaddr = ((ext % 2) * 0x80) + addr;
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_ADDRESS, edidaddr, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SEGPTR, seg_ptr, 7, 0);

	if (seg_ptr)
		REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_OPERATION, 1, 1, 1);
	else
		REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_OPERATION, 1, 0, 0);
}

static void hdmi_core_ddc_init(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	/*Mask the interrupts*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_CTLINT, 0x0, 2, 2);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_CTLINT, 0x0, 6, 6);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_INT, 0x0, 2, 2);

	/* Master clock division */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_DIV, 0x5, 3, 0);

	/* Standard speed counter */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SS_SCL_HCNT_1_ADDR, 0x0,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SS_SCL_HCNT_0_ADDR, 0x79,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SS_SCL_LCNT_1_ADDR, 0x0,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SS_SCL_LCNT_0_ADDR, 0x91,
		7, 0);

	/* Fast speed counter*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_FS_SCL_HCNT_1_ADDR, 0x0,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_FS_SCL_HCNT_0_ADDR, 0x0F,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_FS_SCL_LCNT_1_ADDR, 0x0,
		7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_FS_SCL_LCNT_0_ADDR, 0x21,
		7, 0);

	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SLAVE, 0x50, 6, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_SEGADDR, 0x30, 6, 0);
}

static int hdmi_core_ddc_edid(struct hdmi_ip_data *ip_data,
					u8 *pedid, int ext)
{
	u8 cur_addr = 0;
	char checksum = 0;
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	hdmi_core_ddc_req_addr(ip_data, cur_addr, ext);

	/* Unmask the interrupts*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_CTLINT, 0x1, 2, 2);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_CTLINT, 0x1, 6, 6);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_INT, 0x1, 2, 2);

	/* FIXME:This is a hack to  read only 128 bytes data with a mdelay
	 * Ideally the read has to be based on the done interrupt and
	 * status which is not received thus it is ignored for now
	 */
	while (cur_addr < 128) {
	#if 0
		if (hdmi_wait_for_bit_change(HDMI_CORE_I2CM_INT,
						0, 0, 1) != 1) {
			DSSERR("Failed to recieve done interrupt\n");
			return -ETIMEDOUT;
		}
	#endif
		mdelay(1);
		pedid[cur_addr] = REG_GET(core_sys_base,
					HDMI_CORE_I2CM_DATAI, 7, 0);
		DSSDBG("pedid[%d] = %d", cur_addr, pedid[cur_addr]);
		checksum += pedid[cur_addr++];
		hdmi_core_ddc_req_addr(ip_data, cur_addr, ext);
	}
	return 0;

}

int ti_hdmi_5xxx_read_edid(struct hdmi_ip_data *ip_data,
				u8 *edid, int len)
{
	int r, l;

	if (len < 128)
		return -EINVAL;

	hdmi_core_ddc_init(ip_data);

	r = hdmi_core_ddc_edid(ip_data, edid, 0);
	if (r)
		return r;

	l = 128;

	if (len >= 128 * 2 && edid[0x7e] > 0) {
		r = hdmi_core_ddc_edid(ip_data, edid + 0x80, 1);
		if (r)
			return r;
		l += 128;
	}

	return l;
}

void ti_hdmi_5xxx_core_dump(struct hdmi_ip_data *ip_data, struct seq_file *s)
{

#define DUMPCORE(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(hdmi_core_sys_base(ip_data), r))

	DUMPCORE(HDMI_CORE_FC_INVIDCONF);
	DUMPCORE(HDMI_CORE_FC_INHACTIV0);
	DUMPCORE(HDMI_CORE_FC_INHACTIV1);
	DUMPCORE(HDMI_CORE_FC_INHBLANK0);
	DUMPCORE(HDMI_CORE_FC_INHBLANK1);
	DUMPCORE(HDMI_CORE_FC_INVACTIV0);
	DUMPCORE(HDMI_CORE_FC_INVACTIV1);
	DUMPCORE(HDMI_CORE_FC_INVBLANK);
	DUMPCORE(HDMI_CORE_FC_HSYNCINDELAY0);
	DUMPCORE(HDMI_CORE_FC_HSYNCINDELAY1);
	DUMPCORE(HDMI_CORE_FC_HSYNCINWIDTH0);
	DUMPCORE(HDMI_CORE_FC_HSYNCINWIDTH1);
	DUMPCORE(HDMI_CORE_FC_VSYNCINDELAY);
	DUMPCORE(HDMI_CORE_FC_VSYNCINWIDTH);
	DUMPCORE(HDMI_CORE_FC_CTRLDUR);
	DUMPCORE(HDMI_CORE_FC_EXCTRLDUR);
	DUMPCORE(HDMI_CORE_FC_EXCTRLSPAC);
	DUMPCORE(HDMI_CORE_FC_CH0PREAM);
	DUMPCORE(HDMI_CORE_FC_CH1PREAM);
	DUMPCORE(HDMI_CORE_FC_CH2PREAM);
	DUMPCORE(HDMI_CORE_FC_AVICONF0);
	DUMPCORE(HDMI_CORE_FC_AVICONF1);
	DUMPCORE(HDMI_CORE_FC_AVICONF2);
	DUMPCORE(HDMI_CORE_FC_AVIVID);
	DUMPCORE(HDMI_CORE_FC_PRCONF);

	DUMPCORE(HDMI_CORE_MC_CLKDIS);
	DUMPCORE(HDMI_CORE_MC_SWRSTZREQ);
	DUMPCORE(HDMI_CORE_MC_FLOWCTRL);
	DUMPCORE(HDMI_CORE_MC_PHYRSTZ);
	DUMPCORE(HDMI_CORE_MC_LOCKONCLOCK);

	DUMPCORE(HDMI_CORE_I2CM_SLAVE);
	DUMPCORE(HDMI_CORE_I2CM_ADDRESS);
	DUMPCORE(HDMI_CORE_I2CM_DATAO);
	DUMPCORE(HDMI_CORE_I2CM_DATAI);
	DUMPCORE(HDMI_CORE_I2CM_OPERATION);
	DUMPCORE(HDMI_CORE_I2CM_INT);
	DUMPCORE(HDMI_CORE_I2CM_CTLINT);
	DUMPCORE(HDMI_CORE_I2CM_DIV);
	DUMPCORE(HDMI_CORE_I2CM_SEGADDR);
	DUMPCORE(HDMI_CORE_I2CM_SOFTRSTZ);
	DUMPCORE(HDMI_CORE_I2CM_SEGPTR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_HCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_HCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_LCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_SS_SCL_LCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_HCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_HCNT_0_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_LCNT_1_ADDR);
	DUMPCORE(HDMI_CORE_I2CM_FS_SCL_LCNT_0_ADDR);
}

static void hdmi_core_init(struct hdmi_core_vid_config *video_cfg,
			struct hdmi_core_infoframe_avi *avi_cfg,
			struct hdmi_config *cfg)
{
	printk(KERN_INFO "Enter hdmi_core_init\n");

	/* video core */
	video_cfg->data_enable_pol = 1; /* It is always 1*/
	video_cfg->v_fc_config.timings.hsync_level = cfg->timings.hsync_level;
	video_cfg->v_fc_config.timings.x_res = cfg->timings.x_res;
	video_cfg->v_fc_config.timings.hsw = cfg->timings.hsw;
	video_cfg->v_fc_config.timings.hbp = cfg->timings.hbp;
	video_cfg->v_fc_config.timings.hfp = cfg->timings.hfp;
	video_cfg->hblank = cfg->timings.hfp +
				cfg->timings.hbp + cfg->timings.hsw;
	video_cfg->v_fc_config.timings.vsync_level = cfg->timings.vsync_level;
	video_cfg->v_fc_config.timings.y_res = cfg->timings.y_res;
	video_cfg->v_fc_config.timings.vsw = cfg->timings.vsw;
	video_cfg->v_fc_config.timings.vfp = cfg->timings.vfp;
	video_cfg->v_fc_config.timings.vbp = cfg->timings.vbp;
	video_cfg->vblank_osc = 0; /* Always 0 - need to confirm */
	video_cfg->vblank = cfg->timings.vsw +
				cfg->timings.vfp + cfg->timings.vbp;
	video_cfg->v_fc_config.cm.mode = cfg->cm.mode;
	video_cfg->v_fc_config.timings.interlace = cfg->timings.interlace;

	/* info frame */
	avi_cfg->db1_format = 0;
	avi_cfg->db1_active_info = 0;
	avi_cfg->db1_bar_info_dv = 0;
	avi_cfg->db1_scan_info = 0;
	avi_cfg->db2_colorimetry = 0;
	avi_cfg->db2_aspect_ratio = 0;
	avi_cfg->db2_active_fmt_ar = 0;
	avi_cfg->db3_itc = 0;
	avi_cfg->db3_ec = 0;
	avi_cfg->db3_q_range = 0;
	avi_cfg->db3_nup_scaling = 0;
	avi_cfg->db4_videocode = 0;
	avi_cfg->db5_pixel_repeat = 0;
	avi_cfg->db6_7_line_eoftop = 0 ;
	avi_cfg->db8_9_line_sofbottom = 0;
	avi_cfg->db10_11_pixel_eofleft = 0;
	avi_cfg->db12_13_pixel_sofright = 0;

}

/* DSS_HDMI_CORE_VIDEO_CONFIG */
static void hdmi_core_video_config(struct hdmi_ip_data *ip_data,
				struct hdmi_core_vid_config *cfg)
{
	unsigned char r = 0;
	bool vsync_pol, hsync_pol;
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	vsync_pol = cfg->v_fc_config.timings.vsync_level
		== OMAPDSS_SIG_ACTIVE_HIGH;
	hsync_pol = cfg->v_fc_config.timings.hsync_level
		== OMAPDSS_SIG_ACTIVE_HIGH;

	/* Set hsync, vsync and data-enable polarity  */
	r = hdmi_read_reg(core_sys_base, HDMI_CORE_FC_INVIDCONF);

	r = FLD_MOD(r, vsync_pol, 6, 6);
	r = FLD_MOD(r, hsync_pol, 5, 5);
	r = FLD_MOD(r, cfg->data_enable_pol, 4, 4);
	r = FLD_MOD(r, cfg->vblank_osc, 1, 1);
	r = FLD_MOD(r, cfg->v_fc_config.timings.interlace, 0, 0);
	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_INVIDCONF, r);

	/* set x resolution */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INHACTIV1,
			(cfg->v_fc_config.timings.x_res >> 8), 4, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INHACTIV0,
			(cfg->v_fc_config.timings.x_res & 0xFF), 7, 0);

	/* set y resolution */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INVACTIV1,
			(cfg->v_fc_config.timings.y_res >> 8), 4, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INVACTIV0,
			(cfg->v_fc_config.timings.y_res & 0xFF), 7, 0);

	/* set horizontal blanking pixels */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INHBLANK1,
			(cfg->hblank >> 8), 4, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INHBLANK0,
			(cfg->hblank & 0xFF), 7, 0);

	/* set vertial blanking pixels */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INVBLANK, cfg->vblank, 7, 0);

	/* set horizontal sync offset */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_HSYNCINDELAY1,
			(cfg->v_fc_config.timings.hfp >> 8), 4, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_HSYNCINDELAY0,
			(cfg->v_fc_config.timings.hfp & 0xFF), 7, 0);

	/* set vertical sync offset */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_VSYNCINDELAY,
			cfg->v_fc_config.timings.vfp, 7, 0);

	/* set horizontal sync pulse width */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_HSYNCINWIDTH1,
			(cfg->v_fc_config.timings.hsw >> 8), 1, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_HSYNCINWIDTH0,
			(cfg->v_fc_config.timings.hsw & 0xFF), 7, 0);

	/*  set vertical sync pulse width */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_VSYNCINWIDTH,
			cfg->v_fc_config.timings.vsw, 5, 0);

	/* select DVI mode */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_INVIDCONF,
		cfg->v_fc_config.cm.mode, 3, 3);
}

static void hdmi_core_aux_infoframe_avi_config(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);
	struct hdmi_core_infoframe_avi info_avi = ip_data->avi_cfg;

	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF0,
				info_avi.db1_format, 1, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF0,
				info_avi.db1_active_info, 6, 6);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF0,
				info_avi.db1_bar_info_dv, 3, 2);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF0,
				info_avi.db1_scan_info, 5, 4);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF1,
				info_avi.db2_colorimetry, 7, 6);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF1,
				info_avi.db2_aspect_ratio, 5, 4);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF1,
				info_avi.db2_active_fmt_ar, 3, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF2,
				info_avi.db3_itc, 7, 7);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF2,
				info_avi.db3_ec, 6, 4);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF2,
				info_avi.db3_q_range, 3, 2);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVICONF2,
				info_avi.db3_nup_scaling, 1, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AVIVID,
				info_avi.db4_videocode, 6, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_PRCONF,
				info_avi.db5_pixel_repeat, 3, 0);
}

static void hdmi_enable_video_path(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	printk(KERN_INFO "Enable video_path\n");

	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_CTRLDUR, 0x0C, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_EXCTRLDUR, 0x20, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_EXCTRLSPAC, 0x01, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_CH0PREAM, 0x0B, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_CH1PREAM, 0x16, 5, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_CH2PREAM, 0x21, 5, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_MC_CLKDIS, 0x00, 0, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_MC_CLKDIS, 0x00, 1, 1);
}

static void hdmi_core_mask_interrupts(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	REG_FLD_MOD(core_sys_base, HDMI_CORE_VP_MASK, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_MASK0, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_MASK1, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_MASK2, 0x0, 1, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_PHY_MASK0, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_PHY_I2CM_INT_ADDR, 0x8, 3, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_PHY_I2CM_CTLINT_ADDR, 0x88, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_INT, 0xa3, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CC08, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_D010, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_GP_MASK, 0x3, 1, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_HDCP_MASK, 0x0, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_CEC_MASK, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_INT, 0x1, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_I2CM_CTLINT, 0xff, 7, 0);
}

static void hdmi_core_enable_interrupts(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);
	/* Unmute interrupts */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_MUTE, 0x0, 1, 0);
}

int ti_hdmi_5xxx_core_irq_handler(struct hdmi_ip_data *ip_data)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_FC_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_FC_STAT1, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_FC_STAT2, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_AS_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_PHY_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_I2CM_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_CEC_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_VP_STAT0, 0xff, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_IH_I2CMPHY_STAT0, 0xff, 7, 0);

	return 0;
}

void ti_hdmi_5xxx_basic_configure(struct hdmi_ip_data *ip_data)
{
	/* HDMI */
	struct omap_video_timings video_timing;
	struct hdmi_video_format video_format;
	/* HDMI core */
	struct hdmi_core_vid_config v_core_cfg;
	struct hdmi_core_infoframe_avi *avi_cfg = &ip_data->avi_cfg;
	struct hdmi_config *cfg = &ip_data->cfg;
	struct hdmi_irq_vector irq_enable;

	hdmi_core_mask_interrupts(ip_data);

	hdmi_wp_init(&video_timing, &video_format, &irq_enable);

	hdmi_core_init(&v_core_cfg, avi_cfg, cfg);

	hdmi_wp_video_init_format(&video_format, &video_timing, cfg);

	hdmi_wp_video_config_timing(ip_data, &video_timing);

	/* video config */
	video_format.packing_mode = HDMI_PACK_24b_RGB_YUV444_YUV422;

	hdmi_wp_video_config_format(ip_data, &video_format);

	hdmi_wp_video_config_interface(ip_data);

	/* Enable pll and core interrupts */
	irq_enable.pll_recal = 1;
	irq_enable.pll_unlock = 1;
	irq_enable.pll_lock = 1;
	irq_enable.phy_disconnect = 1;
	irq_enable.phy_connect = 1;
	irq_enable.phy_short_5v = 1;
	irq_enable.video_end_fr = 1;
	/* irq_enable.video_vsync = 1; */
	irq_enable.fifo_sample_req = 1;
	irq_enable.fifo_overflow = 1;
	irq_enable.fifo_underflow = 1;
	irq_enable.ocp_timeout = 1;

	hdmi_wp_irq_enable(ip_data, &irq_enable);

	/*
	 * configure core video part
	 * set software reset in the core
	 */
	v_core_cfg.packet_mode = HDMI_PACKETMODE24BITPERPIXEL;

	hdmi_core_video_config(ip_data, &v_core_cfg);

	/*
	 * configure packet
	 * info frame video see doc CEA861-D page 65
	 */
	avi_cfg->db1_format = HDMI_INFOFRAME_AVI_DB1Y_RGB;
	avi_cfg->db1_active_info =
			HDMI_INFOFRAME_AVI_DB1A_ACTIVE_FORMAT_OFF;
	avi_cfg->db1_bar_info_dv = HDMI_INFOFRAME_AVI_DB1B_NO;
	avi_cfg->db1_scan_info = HDMI_INFOFRAME_AVI_DB1S_0;
	avi_cfg->db2_colorimetry = HDMI_INFOFRAME_AVI_DB2C_NO;
	avi_cfg->db2_aspect_ratio = HDMI_INFOFRAME_AVI_DB2M_NO;
	avi_cfg->db2_active_fmt_ar = HDMI_INFOFRAME_AVI_DB2R_SAME;
	avi_cfg->db3_itc = HDMI_INFOFRAME_AVI_DB3ITC_NO;
	avi_cfg->db3_ec = HDMI_INFOFRAME_AVI_DB3EC_XVYUV601;
	avi_cfg->db3_q_range = HDMI_INFOFRAME_AVI_DB3Q_DEFAULT;
	avi_cfg->db3_nup_scaling = HDMI_INFOFRAME_AVI_DB3SC_NO;
	avi_cfg->db4_videocode = cfg->cm.code;
	avi_cfg->db5_pixel_repeat = HDMI_INFOFRAME_AVI_DB5PR_NO;
	avi_cfg->db6_7_line_eoftop = 0;
	avi_cfg->db8_9_line_sofbottom = 0;
	avi_cfg->db10_11_pixel_eofleft = 0;
	avi_cfg->db12_13_pixel_sofright = 0;

	hdmi_core_aux_infoframe_avi_config(ip_data);

	hdmi_enable_video_path(ip_data);

	hdmi_core_enable_interrupts(ip_data);
}


#if defined(CONFIG_OMAP5_DSS_HDMI_AUDIO)
static void ti_hdmi_5xxx_wp_audio_config_format(struct hdmi_ip_data *ip_data,
					struct hdmi_audio_format *aud_fmt)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_format\n");
	r = hdmi_read_reg(ip_data->base_wp, HDMI_WP_AUDIO_CFG);
	r = FLD_MOD(r, aud_fmt->en_sig_blk_strt_end, 5, 5);
	r = FLD_MOD(r, aud_fmt->type, 4, 4);
	r = FLD_MOD(r, aud_fmt->justification, 3, 3);
	r = FLD_MOD(r, aud_fmt->samples_per_word, 1, 1);
	r = FLD_MOD(r, aud_fmt->sample_size, 0, 0);
	hdmi_write_reg(ip_data->base_wp, HDMI_WP_AUDIO_CFG, r);
}

static void ti_hdmi_5xxx_core_audio_config(struct hdmi_ip_data *ip_data,
					struct hdmi_core_audio_config *cfg)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);
	u8 val;

	/* Mute audio before configuring */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCONF, 0xf, 7, 4);

	/* Set the N parameter */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_N1, cfg->n, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_N2, cfg->n >> 8, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_N3, cfg->n >> 16, 3, 0);

	/*
	 * CTS manual mode. Automatic mode is not supported
	 * when using audio parallel interface.
	 */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CTS3, 1, 4, 4);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CTS1, cfg->cts, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CTS2, cfg->cts >> 8, 7, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CTS3,
		cfg->cts >> 16, 3, 0);

	/* Layout of Audio Sample Packets: 2-channel */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCONF, cfg->layout, 0, 0);

	/* Configure IEC-609580 Validity bits */
	/* Channel 0 is valid */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 0, 0, 0);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 0, 4, 4);
	/* Channels 1, 2, 3 are not valid */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 1, 1);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 5, 5);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 2, 2);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 6, 6);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 3, 3);
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSV, 1, 7, 7);

	/* Configure IEC-60958 User bits */
	/* TODO: should be set by user. */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSU, 0, 7, 0);

	/* Configure IEC-60958 Channel Status word */
	/* CGMSA */
	val = cfg->iec60958_cfg->status[5] & IEC958_AES5_CON_CGMSA;
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(0), val, 5, 4);

	/* Copyright */
	val = (cfg->iec60958_cfg->status[0] &
			IEC958_AES0_CON_NOT_COPYRIGHT)>>2;
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(0), val, 0, 0);

	/* Category */
	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(1),
		cfg->iec60958_cfg->status[1]);

	/* PCM audio mode */
	val = (cfg->iec60958_cfg->status[0] & IEC958_AES0_CON_MODE)>>6;
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(2), val, 6, 4);

	/* Source number */
	val = cfg->iec60958_cfg->status[2] & IEC958_AES2_CON_SOURCE;
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(2), val, 3, 4);

	/* Channel number right 0  */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(3), 2, 3, 0);
	/* Channel number right 1*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(3), 4, 7, 4);
	/* Channel number right 2  */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(4), 6, 3, 0);
	/* Channel number right 3*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(4), 8, 7, 4);
	/* Channel number left 0  */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(5), 1, 3, 0);
	/* Channel number left 1*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(5), 3, 7, 4);
	/* Channel number left 2  */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(6), 5, 3, 0);
	/* Channel number left 3*/
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(6), 7, 7, 4);

	/* Clock accuracy and sample rate */
	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(7),
		cfg->iec60958_cfg->status[3]);

	/* Original sample rate and word length */
	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDSCHNLS(8),
		cfg->iec60958_cfg->status[4]);

	/* Enable FIFO empty and full interrupts */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_INT, 3, 3, 2);

	/* Configure GPA */
	/* select HBR/SPDIF interfaces */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_CONF0, 0, 5, 5);
	/* enable two channels in GPA */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_GP_CONF1, 3, 7, 0);
	/* disable HBR */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_GP_CONF2, 0, 0, 0);
	/* Enable GPA FIFO full and empty mask */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_GP_MASK, 3, 1, 0);
	/* Set polarity of GPA FIFO empty interrupts */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_AUD_GP_POL, 1, 0, 0);

	/*Unmute audio */
	REG_FLD_MOD(core_sys_base, HDMI_CORE_FC_AUDSCONF, 0, 7, 4);
}

static void ti_hdmi_5xxx_core_audio_infoframe_cfg
	(struct hdmi_ip_data *ip_data,
	 struct snd_cea_861_aud_if *info_aud)
{
	void __iomem *core_sys_base = hdmi_core_sys_base(ip_data);

	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDICONF0,
		info_aud->db1_ct_cc);

	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDICONF1,
		info_aud->db2_sf_ss);

	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDICONF2,
		info_aud->db4_ca);

	hdmi_write_reg(core_sys_base, HDMI_CORE_FC_AUDICONF3,
		info_aud->db5_dminh_lsv);
}

int ti_hdmi_5xxx_audio_config(struct hdmi_ip_data *ip_data,
		struct omap_dss_audio *audio)
{
	struct hdmi_audio_format audio_format;
	struct hdmi_audio_dma audio_dma;
	struct hdmi_core_audio_config core;
	int err, n, cts, channel_count;
	unsigned int fs_nr;
	bool word_length_16b = false;

	if (!audio || !audio->iec || !audio->cea || !ip_data)
		return -EINVAL;

	core.iec60958_cfg = audio->iec;

	if (!(audio->iec->status[4] & IEC958_AES4_CON_MAX_WORDLEN_24))
		if (audio->iec->status[4] & IEC958_AES4_CON_WORDLEN_20_16)
			word_length_16b = true;

	/* only 16-bit word length supported atm */
	if (!word_length_16b)
		return -EINVAL;

	/* only 44.1kHz supported atm */
	switch (audio->iec->status[3] & IEC958_AES3_CON_FS) {
	case IEC958_AES3_CON_FS_44100:
		fs_nr = 44100;
		break;
	default:
		return -EINVAL;
	}

	err = hdmi_compute_acr(fs_nr, &n, &cts);
	core.n = n;
	core.cts = cts;

	/* Audio channels settings */
	channel_count = (audio->cea->db1_ct_cc & CEA861_AUDIO_INFOFRAME_DB1CC) + 1;

	/* only 2 channels supported atm */
	if (channel_count != 2)
		return -EINVAL;

	core.layout = HDMI_AUDIO_LAYOUT_2CH;

	/* DMA settings */
	if (word_length_16b)
		audio_dma.transfer_size = 0x10;
	else
		audio_dma.transfer_size = 0x20;
	audio_dma.block_size = 0xC0;
	audio_dma.mode = HDMI_AUDIO_TRANSF_DMA;
	audio_dma.fifo_threshold = 0x20; /* in number of samples */

	/* audio FIFO format settings for 16-bit samples*/
	audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_TWOSAMPLES;
	audio_format.sample_size = HDMI_AUDIO_SAMPLE_16BITS;
	audio_format.justification = HDMI_AUDIO_JUSTIFY_LEFT;

	/* only LPCM atm */
	audio_format.type = HDMI_AUDIO_TYPE_LPCM;

	/* disable start/stop signals of IEC 60958 blocks */
	audio_format.en_sig_blk_strt_end = HDMI_AUDIO_BLOCK_SIG_STARTEND_ON;

	/* configure DMA and audio FIFO format*/
	ti_hdmi_4xxx_wp_audio_config_dma(ip_data, &audio_dma);
	ti_hdmi_5xxx_wp_audio_config_format(ip_data, &audio_format);

	/* configure the core*/
	ti_hdmi_5xxx_core_audio_config(ip_data, &core);

	/* configure CEA 861 audio infoframe*/
	ti_hdmi_5xxx_core_audio_infoframe_cfg(ip_data, audio->cea);

	return 0;
}

int ti_hdmi_5xxx_audio_start(struct hdmi_ip_data *ip_data)
{
	REG_FLD_MOD(ip_data->base_wp,
		HDMI_WP_AUDIO_CTRL, 1, 30, 30);
	return 0;
}

void ti_hdmi_5xxx_audio_stop(struct hdmi_ip_data *ip_data)
{
	REG_FLD_MOD(ip_data->base_wp,
		HDMI_WP_AUDIO_CTRL, 0, 30, 30);
}

#endif
