/*
 * drivers/amlogic/amports/vpp.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/amlogic/vout/vinfo.h>
/* #include <mach/am_regs.h> */
#include "amports_config.h"

#include <linux/amlogic/vpu.h>

#include <linux/amlogic/amports/vframe.h>
#include "video.h"
#include "vpp.h"

#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/video_prot.h>
#include <linux/amlogic/gpio-amlogic.h>

#include <linux/amlogic/iomap.h>

#include "videolog.h"
/* #define CONFIG_VIDEO_LOG */
#ifdef CONFIG_VIDEO_LOG
#define AMLOG
#endif
#include "amlog.h"
#include "vdec_reg.h"
#include "arch/register.h"
#include "amports_priv.h"

/* vpp filter coefficients */
#define COEF_BICUBIC         0
#define COEF_3POINT_TRIANGLE 1
#define COEF_4POINT_TRIANGLE 2
#define COEF_BILINEAR        3
#define COEF_2POINT_BILINEAR 4
#define COEF_BICUBIC_SHARP   5
#define COEF_3POINT_TRIANGLE_SHARP   6
#define COEF_3POINT_BSPLINE  7
#define COEF_4POINT_BSPLINE  8
#define COEF_3D_FILTER       9
#define COEF_NULL            0xff
#define TOTAL_FILTERS        10

#define MAX_NONLINEAR_FACTOR    0x40

#define VPP_SPEED_FACTOR 0x110ULL
#define SUPER_SCALER_V_FACTOR  100

const u32 vpp_filter_coefs_bicubic_sharp[] = {
	3,
	33 | 0x8000,
	/* 0x01f80090, 0x01f80100, 0xff7f0200, 0xfe7f0300, */
	0x01fa008c, 0x01fa0100, 0xff7f0200, 0xfe7f0300,
	0xfd7e0500, 0xfc7e0600, 0xfb7d0800, 0xfb7c0900,
	0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff,
	0xf87613ff, 0xf87416fe, 0xf87218fe, 0xf8701afe,
	0xf76f1dfd, 0xf76d1ffd, 0xf76b21fd, 0xf76824fd,
	0xf76627fc, 0xf76429fc, 0xf7612cfc, 0xf75f2ffb,
	0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa,
	0xf8523cfa, 0xf8503ff9, 0xf84d42f9, 0xf84a45f9,
	0xf84848f8
};

const u32 vpp_filter_coefs_bicubic[] = {
	4,
	33,
	0x00800000, 0x007f0100, 0xff7f0200, 0xfe7f0300,
	0xfd7e0500, 0xfc7e0600, 0xfb7d0800, 0xfb7c0900,
	0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff,
	0xf87613ff, 0xf87416fe, 0xf87218fe, 0xf8701afe,
	0xf76f1dfd, 0xf76d1ffd, 0xf76b21fd, 0xf76824fd,
	0xf76627fc, 0xf76429fc, 0xf7612cfc, 0xf75f2ffb,
	0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa,
	0xf8523cfa, 0xf8503ff9, 0xf84d42f9, 0xf84a45f9,
	0xf84848f8
};

const u32 vpp_filter_coefs_bilinear[] = {
	4,
	33,
	0x00800000, 0x007e0200, 0x007c0400, 0x007a0600,
	0x00780800, 0x00760a00, 0x00740c00, 0x00720e00,
	0x00701000, 0x006e1200, 0x006c1400, 0x006a1600,
	0x00681800, 0x00661a00, 0x00641c00, 0x00621e00,
	0x00602000, 0x005e2200, 0x005c2400, 0x005a2600,
	0x00582800, 0x00562a00, 0x00542c00, 0x00522e00,
	0x00503000, 0x004e3200, 0x004c3400, 0x004a3600,
	0x00483800, 0x00463a00, 0x00443c00, 0x00423e00,
	0x00404000
};

const u32 vpp_3d_filter_coefs_bilinear[] = {
	2,
	33,
	0x80000000, 0x7e020000, 0x7c040000, 0x7a060000,
	0x78080000, 0x760a0000, 0x740c0000, 0x720e0000,
	0x70100000, 0x6e120000, 0x6c140000, 0x6a160000,
	0x68180000, 0x661a0000, 0x641c0000, 0x621e0000,
	0x60200000, 0x5e220000, 0x5c240000, 0x5a260000,
	0x58280000, 0x562a0000, 0x542c0000, 0x522e0000,
	0x50300000, 0x4e320000, 0x4c340000, 0x4a360000,
	0x48380000, 0x463a0000, 0x443c0000, 0x423e0000,
	0x40400000
};

const u32 vpp_filter_coefs_3point_triangle[] = {
	3,
	33,
	0x40400000, 0x3f400100, 0x3d410200, 0x3c410300,
	0x3a420400, 0x39420500, 0x37430600, 0x36430700,
	0x35430800, 0x33450800, 0x32450900, 0x31450a00,
	0x30450b00, 0x2e460c00, 0x2d460d00, 0x2c470d00,
	0x2b470e00, 0x29480f00, 0x28481000, 0x27481100,
	0x26491100, 0x25491200, 0x24491300, 0x234a1300,
	0x224a1400, 0x214a1500, 0x204a1600, 0x1f4b1600,
	0x1e4b1700, 0x1d4b1800, 0x1c4c1800, 0x1b4c1900,
	0x1a4c1a00
};

/* point_num =4, filt_len =4, group_num = 64, [1 2 1] */
const u32 vpp_filter_coefs_4point_triangle[] = {
	4,
	33,
	0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
	0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
	0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
	0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
	0x18382808, 0x18382808, 0x17372909, 0x17372909,
	0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
	0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
	0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
	0x10303010
};
/*4th order (cubic) b-spline
filt_cubic point_num =4, filt_len =4, group_num = 64, [1 5 1] */
const u32 vpp_filter_coefs_4point_bspline[] = {
	4,
	33,
	0x15561500, 0x14561600, 0x13561700, 0x12561800,
	0x11551a00, 0x11541b00, 0x10541c00, 0x0f541d00,
	0x0f531e00, 0x0e531f00, 0x0d522100, 0x0c522200,
	0x0b522300, 0x0b512400, 0x0a502600, 0x0a4f2700,
	0x094e2900, 0x084e2a00, 0x084d2b00, 0x074c2c01,
	0x074b2d01, 0x064a2f01, 0x06493001, 0x05483201,
	0x05473301, 0x05463401, 0x04453601, 0x04433702,
	0x04423802, 0x03413a02, 0x03403b02, 0x033f3c02,
	0x033d3d03
};
/*3rd order (quadratic) b-spline
filt_quadratic, point_num =3, filt_len =3, group_num = 64, [1 6 1] */
const u32 vpp_filter_coefs_3point_bspline[] = {
	3,
	33,
	0x40400000, 0x3e420000, 0x3c440000, 0x3a460000,
	0x38480000, 0x364a0000, 0x344b0100, 0x334c0100,
	0x314e0100, 0x304f0100, 0x2e500200, 0x2c520200,
	0x2a540200, 0x29540300, 0x27560300, 0x26570300,
	0x24580400, 0x23590400, 0x215a0500, 0x205b0500,
	0x1e5c0600, 0x1d5c0700, 0x1c5d0700, 0x1a5e0800,
	0x195e0900, 0x185e0a00, 0x175f0a00, 0x15600b00,
	0x14600c00, 0x13600d00, 0x12600e00, 0x11600f00,
	0x10601000
};
/*filt_triangle, point_num =3, filt_len =2.6, group_num = 64, [1 7 1] */
const u32 vpp_filter_coefs_3point_triangle_sharp[] = {
	3,
	33,
	0x40400000, 0x3e420000, 0x3d430000, 0x3b450000,
	0x3a460000, 0x38480000, 0x37490000, 0x354b0000,
	0x344c0000, 0x324e0000, 0x314f0000, 0x2f510000,
	0x2e520000, 0x2c540000, 0x2b550000, 0x29570000,
	0x28580000, 0x265a0000, 0x245c0000, 0x235d0000,
	0x215f0000, 0x20600000, 0x1e620000, 0x1d620100,
	0x1b620300, 0x19630400, 0x17630600, 0x15640700,
	0x14640800, 0x12640a00, 0x11640b00, 0x0f650c00,
	0x0d660d00
};

const u32 vpp_filter_coefs_2point_binilear[] = {
	2,
	33,
	0x80000000, 0x7e020000, 0x7c040000, 0x7a060000,
	0x78080000, 0x760a0000, 0x740c0000, 0x720e0000,
	0x70100000, 0x6e120000, 0x6c140000, 0x6a160000,
	0x68180000, 0x661a0000, 0x641c0000, 0x621e0000,
	0x60200000, 0x5e220000, 0x5c240000, 0x5a260000,
	0x58280000, 0x562a0000, 0x542c0000, 0x522e0000,
	0x50300000, 0x4e320000, 0x4c340000, 0x4a360000,
	0x48380000, 0x463a0000, 0x443c0000, 0x423e0000,
	0x40400000
};

static const u32 *filter_table[] = {
	vpp_filter_coefs_bicubic,
	vpp_filter_coefs_3point_triangle,
	vpp_filter_coefs_4point_triangle,
	vpp_filter_coefs_bilinear,
	vpp_filter_coefs_2point_binilear,
	vpp_filter_coefs_bicubic_sharp,
	vpp_filter_coefs_3point_triangle_sharp,
	vpp_filter_coefs_3point_bspline,
	vpp_filter_coefs_4point_bspline,
	vpp_3d_filter_coefs_bilinear
};

static int chroma_filter_table[] = {
	COEF_BICUBIC, /* bicubic */
	COEF_3POINT_TRIANGLE,
	COEF_4POINT_TRIANGLE,
	COEF_4POINT_TRIANGLE, /* bilinear */
	COEF_2POINT_BILINEAR,
	COEF_3POINT_TRIANGLE, /* bicubic_sharp */
	COEF_3POINT_TRIANGLE, /* 3point_triangle_sharp */
	COEF_3POINT_TRIANGLE, /* 3point_bspline */
	COEF_4POINT_TRIANGLE, /* 4point_bspline */
	COEF_3D_FILTER		  /* can not change */
};

static unsigned int sharpness1_sr2_ctrl_32d7 = 0x00181008;
MODULE_PARM_DESC(sharpness1_sr2_ctrl_32d7, "sharpness1_sr2_ctrl_32d7");
module_param(sharpness1_sr2_ctrl_32d7, uint, 0664);
/*0x3280 default val: 1920x1080*/
static unsigned int sharpness1_sr2_ctrl_3280 = 0xffffffff;
MODULE_PARM_DESC(sharpness1_sr2_ctrl_3280, "sharpness1_sr2_ctrl_3280");
module_param(sharpness1_sr2_ctrl_3280, uint, 0664);

static unsigned int vpp_filter_fix;
MODULE_PARM_DESC(vpp_filter_fix, "vpp_filter_fix");
module_param(vpp_filter_fix, uint, 0664);

#define MAX_COEFF_LEVEL 5
uint num_coeff_level = MAX_COEFF_LEVEL;
uint vert_coeff_settings[MAX_COEFF_LEVEL] = {
	/* in:out */
	COEF_BICUBIC,
	/* ratio < 1 */
	COEF_BICUBIC_SHARP,
	/* ratio = 1 and phase = 0, */
	/* use for MBX without sharpness HW module, */
	/* TV use COEF_BICUBIC in function coeff */
	COEF_BICUBIC,
	/* ratio in (1~0.5) */
	COEF_3POINT_BSPLINE,
	/* ratio in [0.5~0.333) with pre-scaler on */
	/* this setting is sharpness/smooth balanced */
	/* if need more smooth(less sharp, could use */
	/* COEF_4POINT_BSPLINE or COEF_4POINT_TRIANGLE */
	COEF_4POINT_TRIANGLE,
	/* ratio <= 0.333 with pre-scaler on */
	/* this setting is most smooth */
};

uint horz_coeff_settings[MAX_COEFF_LEVEL] = {
	/* in:out */
	COEF_BICUBIC,
	/* ratio < 1 */
	COEF_BICUBIC_SHARP,
	/* ratio = 1 and phase = 0, */
	/* use for MBX without sharpness HW module, */
	/* TV use COEF_BICUBIC in function coeff */
	COEF_BICUBIC,
	/* ratio in (1~0.5) */
	COEF_3POINT_BSPLINE,
	/* ratio in [0.5~0.333) with pre-scaler on */
	/* this setting is sharpness/smooth balanced */
	/* if need more smooth(less sharp, could use */
	/* COEF_4POINT_BSPLINE or COEF_4POINT_TRIANGLE */
	COEF_4POINT_TRIANGLE,
	/* ratio <= 0.333 with pre-scaler on */
	/* this setting is most smooth */
};

static uint coeff(uint *settings, uint ratio, uint phase,
	bool interlace, int combing_lev)
{
	uint coeff_select = 0;
	uint coeff_type = 0;
	if (ratio >= (3 << 24))
		coeff_select = 4;
	else if (ratio >= (2 << 24))
		coeff_select = 3;
	else if (ratio > (1 << 24))
		coeff_select = 2;
	else if (ratio == (1 << 24)) {
		if (phase == 0)
			coeff_select = 1;
		else
			coeff_select = 2;
	}
	coeff_type = settings[coeff_select];
	/* TODO: add future TV chips */
	if (is_meson_gxtvbb_cpu() || is_meson_txl_cpu() ||
		is_meson_txlx_cpu()) {
		if (coeff_type == COEF_BICUBIC_SHARP)
			coeff_type = COEF_BICUBIC;
	} else {
		/* gxtvbb use dejaggy in SR0 to reduce intelace combing
		   other chip no dejaggy, need swtich to more blur filter */
		if (interlace && (coeff_select < 3) && vpp_filter_fix)
			coeff_type = COEF_4POINT_BSPLINE;
		/* use bicubic for static scene */
		if (combing_lev == 0)
			coeff_type = COEF_BICUBIC;
	}
	return coeff_type;
}

/* vertical and horizontal coeff settings */
module_param_array(vert_coeff_settings, uint, &num_coeff_level, 0664);
MODULE_PARM_DESC(vert_coeff_settings, "\n vert_coeff_settings\n");

module_param_array(horz_coeff_settings, uint, &num_coeff_level, 0664);
MODULE_PARM_DESC(horz_coeff_settings, "\n horz_coeff_settings\n");

bool vert_chroma_filter_en = true;
module_param(vert_chroma_filter_en, bool, 0664);
MODULE_PARM_DESC(vert_chroma_filter_en, "enable vertical chromafilter\n");

bool vert_chroma_filter_force_en;
module_param(vert_chroma_filter_force_en, bool, 0664);
MODULE_PARM_DESC(vert_chroma_filter_force_en,
	"force enable vertical chromafilter\n");

uint vert_chroma_filter_limit = 480;
module_param(vert_chroma_filter_limit, uint, 0664);
MODULE_PARM_DESC(vert_chroma_filter_limit, "vertical chromafilter limit\n");

uint num_chroma_filter = TOTAL_FILTERS;
module_param_array(chroma_filter_table, uint, &num_chroma_filter, 0664);
MODULE_PARM_DESC(chroma_filter_table, "\n chroma_filter_table\n");

uint cur_vert_chroma_filter;
MODULE_PARM_DESC(cur_vert_chroma_filter, "cur_vert_chroma_filter");
module_param(cur_vert_chroma_filter, int, 0444);

uint cur_vert_filter;
MODULE_PARM_DESC(cur_vert_filter, "cur_vert_filter");
module_param(cur_vert_filter, int, 0444);
uint cur_horz_filter;

MODULE_PARM_DESC(cur_horz_filter, "cur_horz_filter");
module_param(cur_horz_filter, int, 0444);
uint cur_skip_line;

MODULE_PARM_DESC(cur_skip_line, "cur_skip_line");
module_param(cur_skip_line, int, 0444);

unsigned int super_scaler_v_ratio = 133;
MODULE_PARM_DESC(super_scaler_v_ratio, "super_scaler_v_ratio");
module_param(super_scaler_v_ratio, uint, 0664);

static u32 skip_policy = 0x81;
module_param(skip_policy, uint, 0664);
MODULE_PARM_DESC(skip_policy, "\n skip_policy\n");

unsigned int scaler_filter_cnt_limit = 10;
MODULE_PARM_DESC(scaler_filter_cnt_limit, "scaler_filter_cnt_limit");
module_param(scaler_filter_cnt_limit, uint, 0664);

static uint last_vert_filter;
static uint last_horz_filter;
static uint scaler_filter_cnt;

static u32 vpp_wide_mode;
static u32 vpp_zoom_ratio = 100;
static s32 vpp_zoom_center_x, vpp_zoom_center_y;
static u32 nonlinear_factor = MAX_NONLINEAR_FACTOR / 2;
static u32 osd_layer_preblend;
static s32 video_layer_top, video_layer_left, video_layer_width,
	   video_layer_height;
static u32 video_source_crop_top, video_source_crop_left,
	   video_source_crop_bottom, video_source_crop_right;
static u32 video_crop_top_resv, video_crop_left_resv,
	   video_crop_bottom_resv, video_crop_right_resv;
static s32 video_layer_global_offset_x, video_layer_global_offset_y;
static s32 osd_layer_top, osd_layer_left, osd_layer_width, osd_layer_height;
static u32 video_speed_check_width = 1800, video_speed_check_height = 1400;

#ifdef TV_3D_FUNCTION_OPEN
static bool vpp_3d_scale;
static int force_filter_mode = 1;
MODULE_PARM_DESC(force_filter_mode, "force_filter_mode");
module_param(force_filter_mode, int, 0664);
#endif

static unsigned int super_debug;
module_param(super_debug, uint, 0664);
MODULE_PARM_DESC(super_debug, "super_debug");

unsigned int super_scaler = 1;
module_param(super_scaler, uint, 0664);
MODULE_PARM_DESC(super_scaler, "super_scaler");

static unsigned int scaler_path_sel;
module_param(scaler_path_sel, uint, 0664);
MODULE_PARM_DESC(scaler_path_sel, "scaler_path_sel");

static unsigned int bypass_spscl0;
module_param(bypass_spscl0, uint, 0664);
MODULE_PARM_DESC(bypass_spscl0, "bypass_spscl0");

static unsigned int bypass_spscl1;
module_param(bypass_spscl1, uint, 0664);
MODULE_PARM_DESC(bypass_spscl1, "bypass_spscl1");

static unsigned int vert_scaler_filter = 0xff;
module_param(vert_scaler_filter, uint, 0664);
MODULE_PARM_DESC(vert_scaler_filter, "vert_scaler_filter");

static unsigned int vert_chroma_scaler_filter = 0xff;
module_param(vert_chroma_scaler_filter, uint, 0664);
MODULE_PARM_DESC(vert_chroma_scaler_filter, "vert_chroma_scaler_filter");

static unsigned int horz_scaler_filter = 0xff;
module_param(horz_scaler_filter, uint, 0664);
MODULE_PARM_DESC(horz_scaler_filter, "horz_scaler_filter");

/*need check this value,*/
static unsigned int bypass_ratio = 205;
module_param(bypass_ratio, uint, 0664);
MODULE_PARM_DESC(bypass_ratio, "bypass_ratio");

static unsigned int sr0_sr1_refresh = 1;
module_param(sr0_sr1_refresh, uint, 0664);
MODULE_PARM_DESC(sr0_sr1_refresh, "sr0_sr1_refresh");

bool pre_scaler_en = true;
module_param(pre_scaler_en, bool, 0664);
MODULE_PARM_DESC(pre_scaler_en, "pre_scaler_en");

unsigned int force_vskip_cnt;
MODULE_PARM_DESC(force_vskip_cnt, "force_vskip_cnt");
module_param(force_vskip_cnt, uint, 0664);

#if 0
#define DECL_PARM(name)\
static int name;\
MODULE_PARM_DESC(name, #name);\
module_param(name, int, 0664);

DECL_PARM(debug_wide_mode)
DECL_PARM(debug_video_left)
DECL_PARM(debug_video_top)
DECL_PARM(debug_video_width)
DECL_PARM(debug_video_height)
DECL_PARM(debug_ratio_x)
DECL_PARM(debug_ratio_y)
#endif
#define ZOOM_BITS       18
#define PHASE_BITS      8
/*
 *   when ratio for Y is 1:1
 *   Line #   In(P)   In(I)       Out(P)      Out(I)            Out(P)  Out(I)
 *   0        P_Y     IT_Y        P_Y          IT_Y
 *   1                                                          P_Y     IT_Y
 *   2                IB_Y                     IB_Y
 *   3                                                                  IB_Y
 *   4        P_Y     IT_Y        P_Y          IT_Y
 *   5                                                          P_Y     IT_Y
 *   6                IB_Y                     IB_Y
 *   7                                                                  IB_Y
 *   8        P_Y     IT_Y        P_Y          IT_Y
 *   9                                                          P_Y     IT_Y
 *  10                IB_Y                     IB_Y
 *  11                                                                  IB_Y
 *  12        P_Y     IT_Y        P_Y          IT_Y
 *                                                              P_Y     IT_Y
 */
/* The table data sequence here is arranged according to
 * enum f2v_vphase_type_e enum,
 *  IT2IT, IB2IB, T2IB, IB2IT, P2IT, P2IB, IT2P, IB2P, P2P
 */
static const u8 f2v_420_in_pos[F2V_TYPE_MAX] = { 0, 2, 0, 2, 0, 0, 0, 2, 0 };
static const u8 f2v_420_out_pos1[F2V_TYPE_MAX] = { 0, 2, 2, 0, 0, 2, 0, 0, 0 };
static const u8 f2v_420_out_pos2[F2V_TYPE_MAX] = { 1, 3, 3, 1, 1, 3, 1, 1, 1 };

static void f2v_get_vertical_phase(u32 zoom_ratio,
			u32 phase_adj,
			struct f2v_vphase_s vphase[F2V_TYPE_MAX],
			u32 interlace)
{
	enum f2v_vphase_type_e type;
	s32 offset_in, offset_out;
	s32 phase;
	const u8 *f2v_420_out_pos;

	if ((interlace == 0) && (zoom_ratio > (1 << ZOOM_BITS)))
		f2v_420_out_pos = f2v_420_out_pos2;
	else
		f2v_420_out_pos = f2v_420_out_pos1;

	for (type = F2V_IT2IT; type < F2V_TYPE_MAX; type++) {
		offset_in = f2v_420_in_pos[type] << PHASE_BITS;
		offset_out =
			(f2v_420_out_pos[type] * zoom_ratio) >> (ZOOM_BITS -
					PHASE_BITS);

		if (offset_in > offset_out) {
			vphase[type].repeat_skip = -1;	/* repeat line */
			vphase[type].phase =
			((4 << PHASE_BITS) + offset_out - offset_in) >> 2;

		} else {
			vphase[type].repeat_skip = 0;	/* skip line */

			while ((offset_in + (4 << PHASE_BITS)) <=
				offset_out) {
				vphase[type].repeat_skip++;
				offset_in += 4 << PHASE_BITS;
			}

			vphase[type].phase = (offset_out - offset_in) >> 2;
		}

		phase = vphase[type].phase + phase_adj;

		if (phase > 0x100)
			vphase[type].repeat_skip++;

		vphase[type].phase = phase & 0xff;

		if (vphase[type].repeat_skip > 5)
			vphase[type].repeat_skip = 5;
	}
}

/*
 * V-shape non-linear mode
 */
static void
calculate_non_linear_ratio(unsigned middle_ratio,
		unsigned width_out,
		struct vpp_frame_par_s *next_frame_par)
{
	unsigned diff_ratio;
	struct vppfilter_mode_s *vpp_filter = &next_frame_par->vpp_filter;

	diff_ratio = middle_ratio * nonlinear_factor;
	vpp_filter->vpp_hf_start_phase_step = (middle_ratio << 6) - diff_ratio;
	vpp_filter->vpp_hf_start_phase_slope = diff_ratio * 4 / width_out;
	vpp_filter->vpp_hf_end_phase_slope =
		vpp_filter->vpp_hf_start_phase_slope | 0x1000000;

	return;
}

/* We find that the minimum line the video can be scaled
 * down without skip line for different modes as below:
 * 4k2k mode:
 * video source		minimus line		ratio(height_in/height_out)
 * 3840 * 2160		1690			1.278
 * 1920 * 1080		860			1.256
 * 1280 * 720		390			1.846
 * 720 * 480		160			3.000
 * 1080p mode:
 * video source		minimus line		ratio(height_in/height_out)
 * 3840 * 2160		840			2.571
 * 1920 * 1080		430			2.511
 * 1280 * 720		200			3.600
 * 720 * 480		80			6.000
 * So the safe scal ratio is 1.25 for 4K2K mode and 2.5
 * (1.25 * 3840 / 1920) for 1080p mode.
 */
#define MIN_RATIO_1000	1250
unsigned int cur_skip_ratio;
MODULE_PARM_DESC(cur_skip_ratio, "cur_skip_ratio");
module_param(cur_skip_ratio, uint, 0444);
unsigned int cur_vf_type;
MODULE_PARM_DESC(cur_vf_type, "cur_vf_type");
module_param(cur_vf_type, uint, 0444);

/*
test on txlx:
Time_out = (V_out/V_screen_total)/FPS_out;
if di bypas:
Time_in = (H_in * V_in)/Clk_vpu;
if di work; for di clk is less than vpu usually;
Time_in = (H_in * V_in)/Clk_di;
if Time_in < Time_out,need do vskip;
but in effect,test result may have some error.
ratio1:V_out test result may larger than calc result;
--after test is large ratio is 1.09;
--so wo should choose the largest ratio_v_out = 110/100;
ratio2:use clk_di or clk_vpu;
--txlx di clk is 250M or 500M;
--befor txlx di clk is 333M;
So need adjust bypass_ratio;
*/

static int
vpp_process_speed_check(s32 width_in,
		s32 height_in,
		s32 height_out,
		s32 height_screen,
		struct vpp_frame_par_s *next_frame_par,
		const struct vinfo_s *vinfo, struct vframe_s *vf)
{
	u32 cur_ratio, bpp = 1;
	int min_ratio_1000 = 0;
	u32 vtotal, clk_in_pps = 0;

	if (vf)
		cur_vf_type = vf->type;
	if (force_vskip_cnt == 0xff)/*for debug*/
		return SPEED_CHECK_DONE;
	if (next_frame_par->vscale_skip_count < force_vskip_cnt)
		return SPEED_CHECK_VSKIP;

	if (vf->type & VIDTYPE_PRE_INTERLACE) {
		if (is_meson_txlx_cpu())
			clk_in_pps = 250000000;
		else
			clk_in_pps = 333000000;
	} else {
		clk_in_pps = get_vpu_clk();
	}
	if (vf->type & VIDTYPE_COMPRESS) {
		if (vf->width > 720)
			min_ratio_1000 = (MIN_RATIO_1000 * 1400)/1000;
		else
			min_ratio_1000 = (1750 * 1400)/1000;
	} else {
		if (vf->width > 720)
			min_ratio_1000 =  MIN_RATIO_1000;
		else
			min_ratio_1000 = 1750;
	}
	if (vinfo->field_height < vinfo->height)
		vtotal = vinfo->vtotal/2;
	else
		vtotal = vinfo->vtotal;
	/* #if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8) */
	if ((get_cpu_type() >= MESON_CPU_MAJOR_ID_M8) && !is_meson_mtvd_cpu()) {
		if ((width_in <= 0) || (height_in <= 0) || (height_out <= 0)
			|| (height_screen <= 0))
			return SPEED_CHECK_DONE;

		if ((next_frame_par->vscale_skip_count > 0)
			&& (vf->type & VIDTYPE_VIU_444))
			bpp = 2;
		if (height_in * bpp > height_out) {
			/*don't need do skip for under 5% scaler down
			*reason:for 1080p input,4k output, if di clk is 250M,
			*the clac height is 1119;which is bigger than 1080!
			*/
			if (height_in > height_out &&
				((height_in - height_out) < height_in/20))
				return SPEED_CHECK_DONE;
			if (get_cpu_type() >=
				MESON_CPU_MAJOR_ID_GXBB) {
				cur_ratio = div_u64((u64)height_in *
						(u64)vinfo->height *
						1000,
						height_out * 2160);
				/* di process first, need more a bit of ratio */
				if (vf->type & VIDTYPE_PRE_INTERLACE)
					cur_ratio = (cur_ratio * 105) / 100;
				if ((next_frame_par->vscale_skip_count > 0)
					&& (vf->type & VIDTYPE_VIU_444))
					cur_ratio = cur_ratio * 2;
				cur_skip_ratio = cur_ratio;
				if ((cur_ratio > min_ratio_1000) &&
				(vf->source_type != VFRAME_SOURCE_TYPE_TUNER) &&
				(vf->source_type != VFRAME_SOURCE_TYPE_CVBS))
					return SPEED_CHECK_VSKIP;
			}
			if (vf->type & VIDTYPE_VIU_422) {
				/*TODO vpu */
				if (height_out == 0
					|| div_u64((u64)VPP_SPEED_FACTOR *
						(u64)width_in *
						(u64)height_in *
						(u64)vinfo->sync_duration_num *
						(u64)vtotal,
						height_out *
						vinfo->sync_duration_den *
						bypass_ratio) > clk_in_pps)
					return SPEED_CHECK_VSKIP;
				else
					return SPEED_CHECK_DONE;
			} else {
				/*TODO vpu */
				if (height_out == 0
					|| div_u64((u64)VPP_SPEED_FACTOR *
					(u64)width_in *
					(u64)height_in *
					(u64)vinfo->sync_duration_num *
					(u64)vtotal,
					height_out *
					vinfo->sync_duration_den * 256)
					> clk_in_pps)
					return SPEED_CHECK_VSKIP;
				/* 4K down scaling to non 4K > 30hz,
				   skip lines for memory bandwidth */
				else if ((((vf->type & VIDTYPE_COMPRESS)
					   == 0) || (next_frame_par->nocomp))
					&&
					 (height_in > 2048) &&
					 (height_out < 2048) &&
					 (vinfo->sync_duration_num >
					  (30 * vinfo->sync_duration_den)) &&
					 (get_cpu_type() !=
						MESON_CPU_MAJOR_ID_GXTVBB) &&
					 (get_cpu_type() !=
						MESON_CPU_MAJOR_ID_GXM))
					return SPEED_CHECK_VSKIP;
				else
					return SPEED_CHECK_DONE;
			}
		} else if (next_frame_par->hscale_skip_count == 0) {
			/*TODO vpu */
			if (div_u64(VPP_SPEED_FACTOR * width_in *
				 vinfo->sync_duration_num * height_screen,
				 vinfo->sync_duration_den * 256)
				> get_vpu_clk())
				return SPEED_CHECK_HSKIP;
			else
				return SPEED_CHECK_DONE;
		}
		return SPEED_CHECK_DONE;
	}
	/* #else */
	/* return okay if vpp preblend enabled */

	if ((READ_VCBUS_REG(VPP_MISC) & VPP_PREBLEND_EN)
		&& (READ_VCBUS_REG(VPP_MISC) & VPP_OSD1_PREBLEND))
		return SPEED_CHECK_DONE;

	/* #if (MESON_CPU_TYPE > MESON_CPU_TYPE_MESON6) */
	if (get_cpu_type() > MESON_CPU_MAJOR_ID_M6) {
		if ((height_out + 1) > height_in)
			return SPEED_CHECK_DONE;
	} else {
		/* #else */
		if (video_speed_check_width * video_speed_check_height *
			height_out > height_screen * width_in * height_in)
			return SPEED_CHECK_DONE;
	}
	/* #endif */

	amlog_mask(LOG_MASK_VPP, "vpp_process_speed_check failed\n");
	return SPEED_CHECK_VSKIP;
}

static void
vpp_set_filters2(u32 process_3d_type, u32 width_in,
	u32 height_in,
	u32 wid_out,
	u32 hei_out,
	const struct vinfo_s *vinfo,
	u32 vpp_flags,
	struct vpp_frame_par_s *next_frame_par, struct vframe_s *vf)
{
	u32 screen_width, screen_height;
	s32 start, end;
	s32 video_top, video_left, temp;
	u32 video_width, video_height;
	u32 ratio_x = 0;
	u32 ratio_y = 0;
	u32 tmp_ratio_y = 0;
	int temp_width;
	int temp_height;
	struct vppfilter_mode_s *filter = &next_frame_par->vpp_filter;
	u32 wide_mode;
	s32 height_shift = 0;
	u32 height_after_ratio;
	u32 aspect_factor;
	s32 ini_vphase;
	u32 w_in = width_in;
	u32 h_in = height_in;
	bool h_crop_enable = false, v_crop_enable = false;
	u32 width_out = wid_out;	/* vinfo->width; */
	u32 height_out = hei_out;	/* vinfo->height; */
	u32 aspect_ratio_out =
		(vinfo->aspect_ratio_den << 8) / vinfo->aspect_ratio_num;
	bool fill_match = true;
	u32 orig_aspect = 0;
	u32 screen_aspect = 0;
	bool skip_policy_check = true;

	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXTVBB) {
		if ((likely(w_in >
			(video_source_crop_left + video_source_crop_right)))
			&& (super_scaler == 0)) {
			w_in -= video_source_crop_left;
			w_in -= video_source_crop_right;
			h_crop_enable = true;
		}

		if ((likely(h_in >
			(video_source_crop_top + video_source_crop_bottom)))
			&& (super_scaler == 0)) {
			h_in -= video_source_crop_top;
			h_in -= video_source_crop_bottom;
			v_crop_enable = true;
		}
	} else {
		if (likely(w_in >
			(video_source_crop_left + video_source_crop_right))) {
			w_in -= video_source_crop_left;
			w_in -= video_source_crop_right;
			h_crop_enable = true;
		}

		if (likely(h_in >
			(video_source_crop_top + video_source_crop_bottom))) {
			h_in -= video_source_crop_top;
			h_in -= video_source_crop_bottom;
			v_crop_enable = true;
		}
	}

	if (is_meson_txlx_cpu()) {
		next_frame_par->vpp_postblend_out_width = vinfo->width;
		next_frame_par->vpp_postblend_out_height = vinfo->height;
	}
#ifndef TV_3D_FUNCTION_OPEN
	next_frame_par->vscale_skip_count = 0;
	next_frame_par->hscale_skip_count = 0;
#endif
	next_frame_par->nocomp = false;
	if (vpp_flags & VPP_FLAG_INTERLACE_IN)
		next_frame_par->vscale_skip_count++;
	if (vpp_flags & VPP_FLAG_INTERLACE_OUT)
		height_shift++;

RESTART:

	aspect_factor = (vpp_flags & VPP_FLAG_AR_MASK) >> VPP_FLAG_AR_BITS;
	wide_mode = vpp_flags & VPP_FLAG_WIDEMODE_MASK;

	/* keep 8 bits resolution for aspect conversion */
	if (wide_mode == VIDEO_WIDEOPTION_4_3) {
		if (vpp_flags & VPP_FLAG_PORTRAIT_MODE)
			aspect_factor = 0x155;
		else
			aspect_factor = 0xc0;
		wide_mode = VIDEO_WIDEOPTION_NORMAL;
	} else if (wide_mode == VIDEO_WIDEOPTION_16_9) {
		if (vpp_flags & VPP_FLAG_PORTRAIT_MODE)
			aspect_factor = 0x1c7;
		else
			aspect_factor = 0x90;
		wide_mode = VIDEO_WIDEOPTION_NORMAL;
	} else if ((wide_mode >= VIDEO_WIDEOPTION_4_3_IGNORE)
			   && (wide_mode <= VIDEO_WIDEOPTION_4_3_COMBINED)) {
		if (aspect_factor != 0xc0)
			fill_match = false;

		orig_aspect = aspect_factor;
		screen_aspect = 0xc0;
	} else if ((wide_mode >= VIDEO_WIDEOPTION_16_9_IGNORE)
			   && (wide_mode <= VIDEO_WIDEOPTION_16_9_COMBINED)) {
		if (aspect_factor != 0x90)
			fill_match = false;

		orig_aspect = aspect_factor;
		screen_aspect = 0x90;
	}

	if ((aspect_factor == 0)
		|| (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH)
		|| (wide_mode == VIDEO_WIDEOPTION_NONLINEAR))
		aspect_factor = 0x100;
	else {
		aspect_factor =
			div_u64((unsigned long long)w_in * height_out *
					(aspect_factor << 8),
					width_out * h_in * aspect_ratio_out);
	}

	if (osd_layer_preblend)
		aspect_factor = 0x100;

	height_after_ratio = (h_in * aspect_factor) >> 8;

	/* if we have ever set a cropped display area for video layer
	 * (by checking video_layer_width/video_height), then
	 * it will override the input width_out/height_out for
	 * ratio calculations, a.k.a we have a window for video content
	 */
	if (osd_layer_preblend) {
		if ((osd_layer_width == 0) || (osd_layer_height == 0)) {
			video_top = 0;
			video_left = 0;
			video_width = width_out;
			video_height = height_out;

		} else {
			video_top = osd_layer_top;
			video_left = osd_layer_left;
			video_width = osd_layer_width;
			video_height = osd_layer_height;
		}
	} else {
		if ((get_cpu_type() >= MESON_CPU_MAJOR_ID_GXTVBB) &&
			next_frame_par->supscl_path == sup0_pp_sp1_scpath) {
			video_top = (video_layer_top >> next_frame_par->
				 supsc1_vert_ratio);
			video_height = (video_layer_height >> next_frame_par->
				 supsc1_vert_ratio);
			video_left = (video_layer_left >> next_frame_par->
				 supsc1_hori_ratio);
			video_width = (video_layer_width >> next_frame_par->
				 supsc1_hori_ratio);
		} else {
			video_top = video_layer_top;
			video_left = video_layer_left;
			video_width = video_layer_width;
			video_height = video_layer_height;
		}
		if ((video_top == 0) && (video_left == 0) && (video_width <= 1)
			&& (video_height <= 1)) {
			/* special case to do full screen display */
			video_width = width_out;
			video_height = height_out;
		} else {
			if ((video_layer_width < 16)
				&& (video_layer_height < 16)) {
				/* sanity check to move
				video out when the target size is too small */
				video_width = width_out;
				video_height = height_out;
				video_left = width_out * 2;
			}
			video_top += video_layer_global_offset_y;
			video_left += video_layer_global_offset_x;
		}
	}

	/*aspect ratio match */
	if ((wide_mode >= VIDEO_WIDEOPTION_4_3_IGNORE)
		&& (wide_mode <= VIDEO_WIDEOPTION_16_9_COMBINED)
		&& orig_aspect) {
		if (vinfo->width && vinfo->height)
			aspect_ratio_out = (vinfo->height << 8) / vinfo->width;

		if ((video_height << 8) > (video_width * aspect_ratio_out)) {
			u32 real_video_height =
				(video_width * aspect_ratio_out) >> 8;

			video_top += (video_height - real_video_height) >> 1;
			video_height = real_video_height;
		} else {
			u32 real_video_width =
				(video_height << 8) / aspect_ratio_out;

			video_left += (video_width - real_video_width) >> 1;
			video_width = real_video_width;
		}

		if (!fill_match) {
			u32 screen_ratio_x, screen_ratio_y;

			screen_ratio_x = 1 << 18;
			screen_ratio_y = (orig_aspect << 18) / screen_aspect;

			switch (wide_mode) {
			case VIDEO_WIDEOPTION_4_3_LETTER_BOX:
			case VIDEO_WIDEOPTION_16_9_LETTER_BOX:
				screen_ratio_x = screen_ratio_y =
					max(screen_ratio_x, screen_ratio_y);
				break;
			case VIDEO_WIDEOPTION_4_3_PAN_SCAN:
			case VIDEO_WIDEOPTION_16_9_PAN_SCAN:
				screen_ratio_x = screen_ratio_y =
				min(screen_ratio_x, screen_ratio_y);
				break;
			case VIDEO_WIDEOPTION_4_3_COMBINED:
			case VIDEO_WIDEOPTION_16_9_COMBINED:
				screen_ratio_x = screen_ratio_y =
				((screen_ratio_x + screen_ratio_y) >> 1);
				break;
			default:
				break;
			}

			ratio_x = screen_ratio_x * w_in / video_width;
			ratio_y =
				screen_ratio_y * h_in / orig_aspect *
				screen_aspect / video_height;
		} else {
			screen_width = video_width * vpp_zoom_ratio / 100;
			screen_height = video_height * vpp_zoom_ratio / 100;

			ratio_x = (w_in << 18) / screen_width;
			ratio_y = (h_in << 18) / screen_height;
		}
	} else {
		screen_width = video_width * vpp_zoom_ratio / 100;
		screen_height = video_height * vpp_zoom_ratio / 100;

		ratio_x = (w_in << 18) / screen_width;
		if (ratio_x * screen_width < (w_in << 18))
			ratio_x++;

		ratio_y = (height_after_ratio << 18) / screen_height;
		if (super_debug)
			pr_info("height_after_ratio=%d,%d,%d,%d,%d\n",
				   height_after_ratio, ratio_x, ratio_y,
				   aspect_factor, wide_mode);

		if (wide_mode == VIDEO_WIDEOPTION_NORMAL) {
			ratio_x = ratio_y = max(ratio_x, ratio_y);
			ratio_y = (ratio_y << 8) / aspect_factor;
		} else if (wide_mode == VIDEO_WIDEOPTION_NORMAL_NOSCALEUP) {
			u32 r1, r2;
			r1 = max(ratio_x, ratio_y);
			r2 = (r1 << 8) / aspect_factor;

			if ((r1 < (1 << 18)) || (r2 < (1 << 18))) {
				if (r1 < r2) {
					ratio_x = 1 << 18;
					ratio_y =
					(ratio_x << 8) / aspect_factor;
				} else {
					ratio_y = 1 << 18;
					ratio_x = aspect_factor << 10;
				}
			} else {
				ratio_x = r1;
				ratio_y = r2;
			}
		}
	}

#if 0
	debug_video_left = video_left;
	debug_video_top = video_top;
	debug_video_width = video_width;
	debug_video_height = video_height;
	debug_ratio_x = ratio_x;
	debug_ratio_y = ratio_y;
	debug_wide_mode = wide_mode;
#endif

	/* vertical */
	ini_vphase = vpp_zoom_center_y & 0xff;

	next_frame_par->VPP_pic_in_height_ =
		h_in / (next_frame_par->vscale_skip_count + 1);

	/* screen position for source */
#ifdef TV_REVERSE
	start =
		video_top + (video_height + 1) / 2 - ((h_in << 17) +
				(vpp_zoom_center_y << 10) +
				(ratio_y >> 1)) / ratio_y;
	end = ((h_in << 18) + (ratio_y >> 1)) / ratio_y + start - 1;
	if (super_debug)
		pr_info("top:start =%d,%d,%d,%d  %d,%d,%d\n",
			start, end, video_top,
			video_height, h_in, ratio_y, vpp_zoom_center_y);
#else
	start =
		video_top + video_height / 2 - ((h_in << 17) +
		(vpp_zoom_center_y << 10)) / ratio_y;
	end = (h_in << 18) / ratio_y + start - 1;
	if (super_debug)
		pr_info("top:start =%d,%d,%d,%d  %d,%d,%d\n",
			start, end, video_top,
			video_height, h_in, ratio_y, vpp_zoom_center_y);
#endif

#ifdef TV_REVERSE
	if (reverse) {
		/* calculate source vertical clip */
		if (video_top < 0) {
			if (start < 0) {
				temp = (-start * ratio_y) >> 18;
				next_frame_par->VPP_vd_end_lines_ =
					h_in - 1 - temp;

			} else
				next_frame_par->VPP_vd_end_lines_ = h_in - 1;

		} else {
			if (start < video_top) {
				temp = ((video_top - start) * ratio_y) >> 18;
				next_frame_par->VPP_vd_end_lines_ =
					h_in - 1 - temp;
			} else
				next_frame_par->VPP_vd_end_lines_ = h_in - 1;
		}
		temp =
			next_frame_par->VPP_vd_end_lines_ -
			(video_height * ratio_y >> 18);
		next_frame_par->VPP_vd_start_lines_ = (temp >= 0) ? temp : 0;
	} else
#endif
	{
		if (video_top < 0) {
			if (start < 0) {
				temp = (-start * ratio_y) >> 18;
				next_frame_par->VPP_vd_start_lines_ = temp;

			} else
				next_frame_par->VPP_vd_start_lines_ = 0;
			temp_height = min((video_top + video_height - 1),
			(vinfo->height - 1));
		} else {
			if (start < video_top) {
				temp = ((video_top - start) * ratio_y) >> 18;
				next_frame_par->VPP_vd_start_lines_ = temp;

			} else
				next_frame_par->VPP_vd_start_lines_ = 0;
			temp_height = min((video_top + video_height - 1),
			(vinfo->height - 1)) - video_top + 1;
		}

		temp =
			next_frame_par->VPP_vd_start_lines_ +
			(temp_height * ratio_y >> 18);
		next_frame_par->VPP_vd_end_lines_ =
			(temp <= (h_in - 1)) ? temp : (h_in - 1);
	}

	if (v_crop_enable) {
		next_frame_par->VPP_vd_start_lines_ += video_source_crop_top;
		next_frame_par->VPP_vd_end_lines_ += video_source_crop_top;
	}

	if (vpp_flags & VPP_FLAG_INTERLACE_IN)
		next_frame_par->VPP_vd_start_lines_ &= ~1;

	/* find overlapped region between
	[start, end], [0, height_out-1],
	[video_top, video_top+video_height-1]
	*/
	start = max(start, max(0, video_top));
	end = min(end, min((s32)(vinfo->height - 1),
		(s32)(video_top + video_height - 1)));

	if (start >= end) {
		/* nothing to display */
		next_frame_par->VPP_vsc_startp = 0;

		next_frame_par->VPP_vsc_endp = 0;

	} else {
		next_frame_par->VPP_vsc_startp =
		(vpp_flags & VPP_FLAG_INTERLACE_OUT) ? (start >> 1) : start;

		next_frame_par->VPP_vsc_endp =
		(vpp_flags & VPP_FLAG_INTERLACE_OUT) ? (end >> 1) : end;
	}

	/* set filter co-efficients */
	tmp_ratio_y = ratio_y;
	ratio_y <<= height_shift;
	ratio_y = ratio_y / (next_frame_par->vscale_skip_count + 1);

	filter->vpp_vsc_start_phase_step = ratio_y << 6;

	f2v_get_vertical_phase(ratio_y, ini_vphase,
		next_frame_par->VPP_vf_ini_phase_,
		vpp_flags & VPP_FLAG_INTERLACE_OUT);

	/* horizontal */
	filter->vpp_hf_start_phase_slope = 0;
	filter->vpp_hf_end_phase_slope = 0;
	filter->vpp_hf_start_phase_step = ratio_x << 6;

	next_frame_par->VPP_hsc_linear_startp = next_frame_par->VPP_hsc_startp;
	next_frame_par->VPP_hsc_linear_endp = next_frame_par->VPP_hsc_endp;

	filter->vpp_hsc_start_phase_step = ratio_x << 6;
	next_frame_par->VPP_hf_ini_phase_ = vpp_zoom_center_x & 0xff;

	/* screen position for source */
#ifdef TV_REVERSE
	start =
		video_left + (video_width + 1) / 2 - ((w_in << 17) +
				(vpp_zoom_center_x << 10) +
				(ratio_x >> 1)) / ratio_x;
	end = ((w_in << 18) + (ratio_x >> 1)) / ratio_x + start - 1;
	if (super_debug)
		pr_info("left:start =%d,%d,%d,%d  %d,%d,%d\n",
			start, end, video_left,
			video_width, w_in, ratio_x, vpp_zoom_center_x);
#else
	start =
		video_left + video_width / 2 - ((w_in << 17) +
		(vpp_zoom_center_x << 10)) /
		ratio_x;
	end = (w_in << 18) / ratio_x + start - 1;
	if (super_debug)
		pr_info("left:start =%d,%d,%d,%d  %d,%d,%d\n",
			start, end, video_left,
			video_width, w_in, ratio_x, vpp_zoom_center_x);
#endif
	/* calculate source horizontal clip */
#ifdef TV_REVERSE
	if (reverse) {
		if (video_left < 0) {
			if (start < 0) {
				temp = (-start * ratio_x) >> 18;
				next_frame_par->VPP_hd_end_lines_ =
					w_in - 1 - temp;
			} else
				next_frame_par->VPP_hd_end_lines_ = w_in - 1;
		} else {
			if (start < video_left) {
				temp = ((video_left - start) * ratio_x) >> 18;
				next_frame_par->VPP_hd_end_lines_ =
					w_in - 1 - temp;
			} else
				next_frame_par->VPP_hd_end_lines_ = w_in - 1;
		}
		temp = next_frame_par->VPP_hd_end_lines_ -
			(video_width * ratio_x >> 18);
		next_frame_par->VPP_hd_start_lines_ = (temp >= 0) ? temp : 0;
	} else
#endif
	{
		if (video_left < 0) {
			if (start < 0) {
				temp = (-start * ratio_x) >> 18;
				next_frame_par->VPP_hd_start_lines_ = temp;

			} else
				next_frame_par->VPP_hd_start_lines_ = 0;
			temp_width = min((video_left + video_width - 1),
			(vinfo->width - 1));
		} else {
			if (start < video_left) {
				temp = ((video_left - start) * ratio_x) >> 18;
				next_frame_par->VPP_hd_start_lines_ = temp;

			} else
				next_frame_par->VPP_hd_start_lines_ = 0;
			temp_width = min((video_left + video_width - 1),
			(vinfo->width - 1)) - video_left + 1;
		}
		temp =
			next_frame_par->VPP_hd_start_lines_ +
			(temp_width * ratio_x >> 18);
		next_frame_par->VPP_hd_end_lines_ =
			(temp <= (w_in - 1)) ? temp : (w_in - 1);
	}

	if (h_crop_enable) {
		next_frame_par->VPP_hd_start_lines_ += video_source_crop_left;
		next_frame_par->VPP_hd_end_lines_ += video_source_crop_left;
	}

	next_frame_par->VPP_line_in_length_ =
		next_frame_par->VPP_hd_end_lines_ -
		next_frame_par->VPP_hd_start_lines_ + 1;
	/* find overlapped region between
	 * [start, end], [0, width_out-1],
	 * [video_left, video_left+video_width-1]
	 */
	start = max(start, max(0, video_left));
	end = min(end,
		min((s32)(vinfo->width - 1),
		(s32)(video_left + video_width - 1)));

	if (start >= end) {
		/* nothing to display */
		next_frame_par->VPP_hsc_startp = 0;
		next_frame_par->VPP_hsc_endp = 0;
		/* avoid mif set wrong or di out size overflow */
		next_frame_par->VPP_hd_start_lines_ = 0;
		next_frame_par->VPP_hd_end_lines_ = 0;
	} else {
		next_frame_par->VPP_hsc_startp = start;

		next_frame_par->VPP_hsc_endp = end;
	}

	if ((wide_mode == VIDEO_WIDEOPTION_NONLINEAR) && (end > start)) {
		calculate_non_linear_ratio(ratio_x, end - start,
				next_frame_par);

		next_frame_par->VPP_hsc_linear_startp =
		next_frame_par->VPP_hsc_linear_endp = (start + end) / 2;
	}

	/* check the painful bandwidth limitation and see
	 * if we need skip half resolution on source side for progressive
	 * frames.
	 */
	if ((next_frame_par->vscale_skip_count < 4)
		&& (!(vpp_flags & VPP_FLAG_VSCALE_DISABLE))) {
		int skip = vpp_process_speed_check(
			(next_frame_par->VPP_hd_end_lines_ -
			 next_frame_par->VPP_hd_start_lines_ + 1) /
			(next_frame_par->hscale_skip_count + 1),
			(next_frame_par->VPP_vd_end_lines_ -
			next_frame_par->VPP_vd_start_lines_ + 1) /
			(next_frame_par->vscale_skip_count + 1),
			(next_frame_par->VPP_vsc_endp -
			next_frame_par->VPP_vsc_startp + 1),
			vinfo->height >>
			((vpp_flags & VPP_FLAG_INTERLACE_OUT) ? 1 : 0),
			next_frame_par,
			vinfo,
			vf);

		if (skip == SPEED_CHECK_VSKIP) {
			if (vpp_flags & VPP_FLAG_INTERLACE_IN)
				next_frame_par->vscale_skip_count += 2;
			else {
#ifdef TV_3D_FUNCTION_OPEN
				if ((next_frame_par->vpp_3d_mode ==
					VPP_3D_MODE_LA)
					&& (process_3d_type & MODE_3D_ENABLE))
					next_frame_par->vscale_skip_count += 2;
				else
#endif
					next_frame_par->vscale_skip_count++;
			}
			goto RESTART;

		} else if (skip == SPEED_CHECK_HSKIP)
			next_frame_par->hscale_skip_count = 1;
	}

	if ((vf->type & VIDTYPE_COMPRESS) &&
		(vf->canvas0Addr != 0) &&
		(next_frame_par->vscale_skip_count > 1) &&
		(!next_frame_par->nocomp)) {
		pr_info("Try DW buffer for compressed frame scaling.\n");

		/* for VIDTYPE_COMPRESS, check if we can use double write
		 * buffer when primary frame can not be scaled.
		 */
		next_frame_par->nocomp = true;
		w_in = width_in = vf->width;
		h_in = height_in = vf->height;
		next_frame_par->hscale_skip_count = 0;
		next_frame_par->vscale_skip_count = 0;

		goto RESTART;
	}

	if ((skip_policy & 0xf0) && (skip_policy_check == true)) {
		skip_policy_check = false;
		if (skip_policy & 0x40) {
			next_frame_par->vscale_skip_count = skip_policy & 0xf;
			goto RESTART;
		} else if (skip_policy & 0x80) {
			if ((((vf->width >= 4096) &&
			(!(vf->type & VIDTYPE_COMPRESS))) ||
			(vf->flag & VFRAME_FLAG_HIGH_BANDWITH))
			&& (next_frame_par->vscale_skip_count == 0)) {
				next_frame_par->vscale_skip_count =
				skip_policy & 0xf;
				goto RESTART;
			}
		}
	}

	filter->vpp_hsc_start_phase_step = ratio_x << 6;

	/* coeff selection before skip and apply pre_scaler */
	filter->vpp_vert_filter =
		coeff(vert_coeff_settings,
			filter->vpp_vsc_start_phase_step *
				(next_frame_par->vscale_skip_count + 1),
			1,
			((vf->type_original & VIDTYPE_TYPEMASK)
				!= VIDTYPE_PROGRESSIVE),
			vf->combing_cur_lev);
	filter->vpp_vert_coeff =
		filter_table[filter->vpp_vert_filter];

	/* when local interlace or AV or ATV */
	/* TODO: add 420 check for local */
	if (vert_chroma_filter_force_en || (vert_chroma_filter_en
	&& (((vf->source_type == VFRAME_SOURCE_TYPE_OTHERS)
	 && (((vf->type_original & VIDTYPE_TYPEMASK) != VIDTYPE_PROGRESSIVE) ||
	 (vf->height < vert_chroma_filter_limit)))
	|| (vf->source_type == VFRAME_SOURCE_TYPE_CVBS)
	|| (vf->source_type == VFRAME_SOURCE_TYPE_TUNER)))) {
		cur_vert_chroma_filter
			= chroma_filter_table[filter->vpp_vert_filter];
		filter->vpp_vert_chroma_coeff
			= filter_table[cur_vert_chroma_filter];
		filter->vpp_vert_chroma_filter_en = true;
	} else {
		cur_vert_chroma_filter = COEF_NULL;
		filter->vpp_vert_chroma_filter_en = false;
	}
	/* avoid hscaler fitler adjustion affect on picture shift*/
	filter->vpp_horz_filter =
		coeff(horz_coeff_settings,
			filter->vpp_hf_start_phase_step,
			next_frame_par->VPP_hf_ini_phase_,
			((vf->type_original & VIDTYPE_TYPEMASK)
				!= VIDTYPE_PROGRESSIVE),
			vf->combing_cur_lev);
	/*for gxl cvbs out index*/
	if ((vinfo->mode == VMODE_576CVBS) &&
		(filter->vpp_hf_start_phase_step == (1 << 24)))
		filter->vpp_horz_filter = COEF_BICUBIC_SHARP;
	filter->vpp_horz_coeff =
		filter_table[filter->vpp_horz_filter];

	/* apply line skip */
	if (next_frame_par->hscale_skip_count) {
		filter->vpp_hf_start_phase_step >>= 1;
		filter->vpp_hsc_start_phase_step >>= 1;
		next_frame_par->VPP_line_in_length_ >>= 1;
	}

	/*pre hsc&vsc in pps for scaler down*/
	if ((filter->vpp_hf_start_phase_step >= 0x2000000) &&
		(filter->vpp_vsc_start_phase_step >= 0x2000000) &&
		(get_cpu_type() != MESON_CPU_MAJOR_ID_GXBB) &&
		pre_scaler_en) {
		filter->vpp_pre_vsc_en = 1;
		filter->vpp_vsc_start_phase_step >>= 1;
		ratio_y >>= 1;
		f2v_get_vertical_phase(ratio_y, ini_vphase,
		next_frame_par->VPP_vf_ini_phase_,
		vpp_flags & VPP_FLAG_INTERLACE_OUT);

	} else
		filter->vpp_pre_vsc_en = 0;

	if ((filter->vpp_hf_start_phase_step >= 0x2000000) &&
		(get_cpu_type() != MESON_CPU_MAJOR_ID_GXBB) &&
		pre_scaler_en) {
		filter->vpp_pre_hsc_en = 1;
		filter->vpp_hf_start_phase_step >>= 1;
		filter->vpp_hsc_start_phase_step >>= 1;
	} else
		filter->vpp_pre_hsc_en = 0;

	next_frame_par->VPP_hf_ini_phase_ = vpp_zoom_center_x & 0xff;

	/* overwrite filter setting for interlace output*/
	/* TODO: not reasonable when 4K input to 480i output */
	if (vpp_flags & VPP_FLAG_INTERLACE_OUT) {
		filter->vpp_vert_coeff = filter_table[COEF_BILINEAR];
		filter->vpp_vert_filter = COEF_BILINEAR;
	}

	/* force overwrite filter setting */
	if ((vert_scaler_filter >= COEF_BICUBIC) &&
		(vert_scaler_filter <= COEF_3D_FILTER)) {
		filter->vpp_vert_coeff = filter_table[vert_scaler_filter];
		filter->vpp_vert_filter = vert_scaler_filter;
	}
	if (vert_chroma_filter_force_en &&
		(vert_chroma_scaler_filter >= COEF_BICUBIC) &&
		(vert_chroma_scaler_filter <= COEF_3D_FILTER)) {
		cur_vert_chroma_filter = vert_chroma_scaler_filter;
			filter->vpp_vert_chroma_coeff
				= filter_table[cur_vert_chroma_filter];
			filter->vpp_vert_chroma_filter_en = true;
	} else {
		cur_vert_chroma_filter = COEF_NULL;
		filter->vpp_vert_chroma_filter_en = false;
	}

	if ((horz_scaler_filter >= COEF_BICUBIC) &&
		(horz_scaler_filter <= COEF_3D_FILTER)) {
		filter->vpp_horz_coeff = filter_table[horz_scaler_filter];
		filter->vpp_horz_filter = horz_scaler_filter;
	}

#ifdef	TV_3D_FUNCTION_OPEN
	/* final stage for 3D filter overwrite */
	if ((next_frame_par->vpp_3d_scale) && force_filter_mode) {
		filter->vpp_vert_coeff = filter_table[COEF_3D_FILTER];
		filter->vpp_vert_filter = COEF_3D_FILTER;
	}
#endif
	if ((last_vert_filter != filter->vpp_vert_filter) ||
		(last_horz_filter != filter->vpp_horz_filter)) {
		last_vert_filter = filter->vpp_vert_filter;
		last_horz_filter = filter->vpp_horz_filter;
		scaler_filter_cnt = 0;
	} else {
		scaler_filter_cnt++;
	}
	if ((scaler_filter_cnt >= scaler_filter_cnt_limit) &&
		((cur_vert_filter != filter->vpp_vert_filter) ||
		(cur_horz_filter != filter->vpp_horz_filter))) {
		video_property_notify(1);
		cur_vert_filter = filter->vpp_vert_filter;
		cur_horz_filter = filter->vpp_horz_filter;
		scaler_filter_cnt = scaler_filter_cnt_limit;
	}
	cur_skip_line = next_frame_par->vscale_skip_count;

#if HAS_VPU_PROT
	if (has_vpu_prot()) {
		if (get_prot_status()) {
			s32 tmp_height =
				(((s32) next_frame_par->VPP_vd_end_lines_ +
				  1) << 18) / tmp_ratio_y;
			s32 tmp_top = 0;
			s32 tmp_bottom = 0;

/* pr_info("height_out %d video_height %d\n", height_out, video_height); */
/* pr_info("vf1 %d %d %d %d vs %d %d\n", next_frame_par->VPP_hd_start_lines_,*/
/* next_frame_par->VPP_hd_end_lines_, */
/* next_frame_par->VPP_vd_start_lines_, next_frame_par->VPP_vd_end_lines_, */
/* next_frame_par->hscale_skip_count, next_frame_par->vscale_skip_count); */
			if ((s32) video_height > tmp_height) {
				tmp_top = (s32) video_top +
				(((s32) video_height - tmp_height) >> 1);
			} else
				tmp_top = (s32) video_top;
			tmp_bottom = tmp_top +
			(((s32) next_frame_par->VPP_vd_end_lines_ + 1) << 18) /
			(s32) tmp_ratio_y;
			if (tmp_bottom > (s32) height_out
				&& tmp_top < (s32) height_out) {
				s32 tmp_end =
				(s32) next_frame_par->VPP_vd_end_lines_ -
				((tmp_bottom -
				(s32) height_out) *
				(s32) tmp_ratio_y >> 18);
				if (tmp_end <
				(s32) next_frame_par->VPP_vd_end_lines_) {
					next_frame_par->VPP_vd_end_lines_ =
						tmp_end;
				}

			} else if (tmp_bottom > (s32) height_out
					   && tmp_top >= (s32) height_out)
				next_frame_par->VPP_vd_end_lines_ = 1;
			next_frame_par->VPP_vd_end_lines_ =
				next_frame_par->VPP_vd_end_lines_ -
				h_in / height_out;
			if ((s32) next_frame_par->VPP_vd_end_lines_ <
				(s32) next_frame_par->VPP_vd_start_lines_) {
				next_frame_par->VPP_vd_end_lines_ =
					next_frame_par->VPP_vd_start_lines_;
			}
			if ((s32) next_frame_par->VPP_hd_end_lines_ <
				(s32) next_frame_par->VPP_hd_start_lines_) {
				next_frame_par->VPP_hd_end_lines_ =
					next_frame_par->VPP_hd_start_lines_;
			}
/* pr_info("tmp_top %d tmp_bottom %d tmp_height %d\n",*/
/* tmp_top, tmp_bottom, tmp_height); */
/* pr_info("vf2 %d %d %d %d\n", next_frame_par->VPP_hd_start_lines_,*/
/* next_frame_par->VPP_hd_end_lines_, */
/* next_frame_par->VPP_vd_start_lines_, next_frame_par->VPP_vd_end_lines_); */
		}
	}
#endif
}
/*
VPP_SRSHARP0_CTRL:0x1d91
[0]srsharp0 enable for sharpness module reg r/w
[1]if sharpness is enable or vscaler is enable,must set to 1,
sharpness1;reg can only to be w
*/
int vpp_set_super_scaler_regs(int scaler_path_sel,
		int reg_srscl0_enable,
		int reg_srscl0_hsize,
		int reg_srscl0_vsize,
		int reg_srscl0_hori_ratio,
		int reg_srscl0_vert_ratio,
		int reg_srscl1_enable,
		int reg_srscl1_hsize,
		int reg_srscl1_vsize,
		int reg_srscl1_hori_ratio,
		int reg_srscl1_vert_ratio,
		int vpp_postblend_out_width,
		int vpp_postblend_out_height)
{

	int tmp_data = 0;
	int tmp_data2 = 0;

	/* top config */
	tmp_data = READ_VCBUS_REG(VPP_SRSHARP0_CTRL);
	if (sr0_sr1_refresh) {
		if (reg_srscl0_hsize > SUPER_CORE0_WIDTH_MAX) {
			if (((tmp_data >> 1)&0x1) != 0)
				VSYNC_WR_MPEG_REG_BITS(VPP_SRSHARP0_CTRL,
					0, 1, 1);
		} else {
			if (((tmp_data >> 1)&0x1) != 1)
				VSYNC_WR_MPEG_REG_BITS(VPP_SRSHARP0_CTRL,
					1, 1, 1);
		}
		if ((tmp_data&0x1) != 1)
			VSYNC_WR_MPEG_REG_BITS(VPP_SRSHARP0_CTRL, 1, 0, 1);
	}
	tmp_data = READ_VCBUS_REG(VPP_SRSHARP1_CTRL);
	if (sr0_sr1_refresh) {
		if (((tmp_data >> 1)&0x1) != 1)
			VSYNC_WR_MPEG_REG_BITS(VPP_SRSHARP1_CTRL, 1, 1, 1);
		if ((tmp_data&0x1) != 1)
			VSYNC_WR_MPEG_REG_BITS(VPP_SRSHARP1_CTRL, 1, 0, 1);
	}
	/* core0 config */
	tmp_data = READ_VCBUS_REG(SRSHARP0_SHARP_SR2_CTRL);
	if (sr0_sr1_refresh) {
		if (((tmp_data >> 5)&0x1) != (reg_srscl0_vert_ratio&0x1))
			VSYNC_WR_MPEG_REG_BITS(SRSHARP0_SHARP_SR2_CTRL,
				reg_srscl0_vert_ratio&0x1, 5, 1);
		if (((tmp_data >> 4)&0x1) != (reg_srscl0_hori_ratio&0x1))
			VSYNC_WR_MPEG_REG_BITS(SRSHARP0_SHARP_SR2_CTRL,
				reg_srscl0_hori_ratio&0x1, 4, 1);

		if (reg_srscl0_hsize > SUPER_CORE0_WIDTH_MAX) {
			if (((tmp_data >> 2)&0x1) != 0)
				VSYNC_WR_MPEG_REG_BITS(SRSHARP0_SHARP_SR2_CTRL,
					0, 2, 1);
		} else {
			if (((tmp_data >> 2)&0x1) != 1)
				VSYNC_WR_MPEG_REG_BITS(SRSHARP0_SHARP_SR2_CTRL,
					1, 2, 1);
		}

		if ((tmp_data & 0x1) == (reg_srscl0_hori_ratio&0x1))
			VSYNC_WR_MPEG_REG_BITS(SRSHARP0_SHARP_SR2_CTRL,
				((~(reg_srscl0_hori_ratio&0x1))&0x1), 0, 1);
	}
	/* core1 config */
	tmp_data = sharpness1_sr2_ctrl_32d7;
	/*if ((((tmp_data >> 5)&0x1) != (reg_srscl1_vert_ratio&0x1)) ||
		(((tmp_data >> 4)&0x1) != (reg_srscl1_hori_ratio&0x1)) ||
		((tmp_data & 0x1) == (reg_srscl1_hori_ratio&0x1)) ||
		(((tmp_data >> 2)&0x1) != 1)) {*/
	if (1) {/* modify for avoid reg not be write@20160505 */
		tmp_data = tmp_data & (~(1 << 5));
		tmp_data = tmp_data & (~(1 << 4));
		tmp_data = tmp_data & (~(1 << 2));
		tmp_data = tmp_data & (~(1 << 0));
		tmp_data |= ((reg_srscl1_vert_ratio&0x1) << 5);
		tmp_data |= ((reg_srscl1_hori_ratio&0x1) << 4);
		tmp_data |= (1 << 2);
		tmp_data |= (((~(reg_srscl1_hori_ratio&0x1))&0x1) << 0);
		if (sr0_sr1_refresh) {
			VSYNC_WR_MPEG_REG(SRSHARP1_SHARP_SR2_CTRL, tmp_data);
			sharpness1_sr2_ctrl_32d7 = tmp_data;
		}
	}

	/* size config */
	tmp_data = ((reg_srscl0_hsize & 0x1fff) << 16) |
			   (reg_srscl0_vsize & 0x1fff);
	tmp_data2 = READ_VCBUS_REG(SRSHARP0_SHARP_SR2_CTRL);
	if (tmp_data != tmp_data2)
		VSYNC_WR_MPEG_REG(SRSHARP0_SHARP_HVSIZE, tmp_data);

	tmp_data = ((reg_srscl1_hsize & 0x1fff) << 16) |
			   (reg_srscl1_vsize & 0x1fff);
	if (1) {/*(sharpness1_sr2_ctrl_3280 != tmp_data) {*/
		VSYNC_WR_MPEG_REG(SRSHARP1_SHARP_HVSIZE, tmp_data);
		sharpness1_sr2_ctrl_3280 = tmp_data;
	}
	/*ve input size setting*/
	tmp_data = ((reg_srscl1_hsize & 0x1fff) << 16) |
		(reg_srscl1_vsize & 0x1fff);
	tmp_data2 = READ_VCBUS_REG(VPP_VE_H_V_SIZE);
	if (tmp_data != tmp_data2)
		VSYNC_WR_MPEG_REG(VPP_VE_H_V_SIZE, tmp_data);
	/*chroma blue stretch size setting*/
	if (is_meson_txlx_cpu()) {
		tmp_data = (((vpp_postblend_out_width & 0x1fff) << 16) |
			(vpp_postblend_out_height & 0x1fff));
		VSYNC_WR_MPEG_REG(VPP_OUT_H_V_SIZE, tmp_data);
	} else {
		if (scaler_path_sel == sup0_pp_sp1_scpath) {
			tmp_data = (((reg_srscl1_hsize & 0x1fff) <<
				reg_srscl1_hori_ratio) << 16) |
				((reg_srscl1_vsize & 0x1fff) <<
				reg_srscl1_vert_ratio);
			tmp_data2 = READ_VCBUS_REG(VPP_PSR_H_V_SIZE);
			if (tmp_data != tmp_data2)
				VSYNC_WR_MPEG_REG(VPP_PSR_H_V_SIZE, tmp_data);
		} else if (scaler_path_sel == sup0_pp_post_blender) {
			tmp_data = ((reg_srscl1_hsize & 0x1fff) << 16) |
					   (reg_srscl1_vsize & 0x1fff);
			tmp_data2 = READ_VCBUS_REG(VPP_PSR_H_V_SIZE);
			if (tmp_data != tmp_data2)
				VSYNC_WR_MPEG_REG(VPP_PSR_H_V_SIZE, tmp_data);
		}
	}

	/* path config */
	tmp_data2 = (READ_VCBUS_REG(VPP_VE_ENABLE_CTRL) >> 5)&0x1;
	if (tmp_data2 != scaler_path_sel)
		VSYNC_WR_MPEG_REG_BITS(VPP_VE_ENABLE_CTRL,
			scaler_path_sel, 5, 1);

	return 0;
}

static void vpp_set_scaler(u32 process_3d_type, u32 src_width,
			u32 src_height,
			const struct vinfo_s *vinfo,
			u32 vpp_flags,
			struct vpp_frame_par_s *next_frame_par,
			struct vframe_s *vf)
{
	unsigned int spsc1_h_out, spsc1_w_out;
	unsigned int ppsc_h_in, ppsc_w_in;
	unsigned int ppsc_h_out, ppsc_w_out;
	unsigned int hor_sc_multiple_num, ver_sc_multiple_num;
	bool h_crop_enable = false, v_crop_enable = false;
	u32 width_out = vinfo->width;
	u32 height_out = vinfo->height;

	if (video_layer_width > 0 && video_layer_width <= vinfo->width
	&& ((video_layer_width + video_layer_left) <= vinfo->width))
		width_out = video_layer_width;
	if (video_layer_height > 0 && video_layer_height <= vinfo->height
	&& ((video_layer_height + video_layer_top) <= vinfo->height))
		height_out = video_layer_height;

	if ((likely(src_width >
		(video_source_crop_left + video_source_crop_right)))
		&& (super_scaler == 1)) {
		src_width -= video_source_crop_left + video_source_crop_right;
		h_crop_enable = true;
	}

	if ((likely(
		src_height >
		(video_source_crop_top + video_source_crop_bottom)))
		&& (super_scaler == 1)) {
		src_height -= video_source_crop_top + video_source_crop_bottom;
		v_crop_enable = true;
	}
	hor_sc_multiple_num = width_out / src_width;
	ver_sc_multiple_num = height_out*SUPER_SCALER_V_FACTOR / src_height;

	/* just calcuate the enable sclaer module */
	/* note:if first check h may cause v can't do scaling;
	* for example: 1920x1080(input),3840x2160(ouput);
	* todo:if you have better idea,you can improve it*/
	/* step1: judge core0&core1 vertical enable or disable*/
	if (ver_sc_multiple_num >= 2*SUPER_SCALER_V_FACTOR) {
		next_frame_par->supsc0_vert_ratio =
			(src_width < SUPER_CORE0_WIDTH_MAX/2) ? 1 : 0;
		next_frame_par->supsc1_vert_ratio =
			((width_out < SUPER_CORE1_WIDTH_MAX) &&
			(src_width < SUPER_CORE1_WIDTH_MAX/2)) ? 1 : 0;
		if (next_frame_par->supsc0_vert_ratio &&
			(ver_sc_multiple_num < 4*SUPER_SCALER_V_FACTOR))
			next_frame_par->supsc1_vert_ratio = 0;
		next_frame_par->supsc0_enable =
			next_frame_par->supsc0_vert_ratio ? 1 : 0;
		next_frame_par->supsc1_enable =
			next_frame_par->supsc1_vert_ratio ? 1 : 0;
	} else {
		next_frame_par->supsc0_enable = 0;
		next_frame_par->supsc0_vert_ratio = 0;
		next_frame_par->supsc1_enable = 0;
		next_frame_par->supsc1_vert_ratio = 0;
	}
	/* step2: judge core0&core1 horizontal enable or disable*/
	if (hor_sc_multiple_num >= 2) {
		if ((src_width > SUPER_CORE0_WIDTH_MAX) ||
			((src_width > SUPER_CORE0_WIDTH_MAX/2) &&
			next_frame_par->supsc0_vert_ratio) ||
			(((src_width << 1) > SUPER_CORE1_WIDTH_MAX/2) &&
			next_frame_par->supsc1_vert_ratio))
			next_frame_par->supsc0_hori_ratio = 0;
		else
			next_frame_par->supsc0_hori_ratio = 1;
		if (((width_out >> 1) > SUPER_CORE1_WIDTH_MAX) ||
			(((width_out >> 1) > SUPER_CORE1_WIDTH_MAX/2) &&
			next_frame_par->supsc1_vert_ratio) ||
			(next_frame_par->supsc0_hori_ratio &&
			(hor_sc_multiple_num < 4)))
			next_frame_par->supsc1_hori_ratio = 0;
		else
			next_frame_par->supsc1_hori_ratio = 1;
		next_frame_par->supsc0_enable =
			(next_frame_par->supsc0_hori_ratio ||
			next_frame_par->supsc0_enable) ? 1 : 0;
		next_frame_par->supsc1_enable =
			(next_frame_par->supsc1_hori_ratio ||
			next_frame_par->supsc1_enable) ? 1 : 0;
	} else {
		next_frame_par->supsc0_enable =
			next_frame_par->supsc0_vert_ratio ? 1 : 0;
		next_frame_par->supsc0_hori_ratio = 0;
		next_frame_par->supsc1_enable =
			next_frame_par->supsc1_vert_ratio ? 1 : 0;
		next_frame_par->supsc1_hori_ratio = 0;
	}
	/*double check core1 input width for core1_vert_ratio!!!*/
	if (next_frame_par->supsc1_vert_ratio &&
		(width_out >> next_frame_par->supsc1_hori_ratio >
		SUPER_CORE1_WIDTH_MAX/2)) {
		next_frame_par->supsc1_vert_ratio = 0;
		if (next_frame_par->supsc1_hori_ratio == 0)
			next_frame_par->supsc1_enable = 0;
	}
	/* option add patch */
	if ((ver_sc_multiple_num <= super_scaler_v_ratio) &&
		(src_height >= SUPER_CORE0_WIDTH_MAX/2) &&
		(src_height <= 1088) &&
		(ver_sc_multiple_num > SUPER_SCALER_V_FACTOR) &&
		(vinfo->height >= 2000)) {
		next_frame_par->supsc0_enable = 0;
		next_frame_par->supsc1_enable = 1;
		next_frame_par->supsc0_hori_ratio = 0;
		next_frame_par->supsc1_hori_ratio = 1;
		next_frame_par->supsc0_vert_ratio = 0;
		next_frame_par->supsc1_vert_ratio = 1;
	}
	if (bypass_spscl0) {
		next_frame_par->supsc0_enable = 0;
		next_frame_par->supsc0_hori_ratio = 0;
		next_frame_par->supsc0_vert_ratio = 0;
	}
	if (bypass_spscl1) {
		next_frame_par->supsc1_enable = 0;
		next_frame_par->supsc1_hori_ratio = 0;
		next_frame_par->supsc1_vert_ratio = 0;
	}
	next_frame_par->spsc0_h_in = src_height;
	next_frame_par->spsc0_w_in = src_width;
	if (super_debug)
		pr_info(
		"supsc0_hori=%d,supsc1_hori=%d,supsc0_v=%d,supsc1_v=%d\n",
		 next_frame_par->supsc0_hori_ratio,
		 next_frame_par->supsc1_hori_ratio,
		 next_frame_par->supsc0_vert_ratio,
		 next_frame_par->supsc1_vert_ratio);
	/* select the scaler path:[core0 =>>
	ppscaler =>> core1]  or
	[core0  =>> ppscaler =>> postblender =>> core1]*/
	ppsc_h_in = (next_frame_par->spsc0_h_in <<
		next_frame_par->supsc0_vert_ratio);
	ppsc_w_in = (next_frame_par->spsc0_w_in <<
		next_frame_par->supsc0_hori_ratio);
	spsc1_h_out = height_out;
	spsc1_w_out = width_out;
	ppsc_h_out =
		(spsc1_h_out >> next_frame_par->supsc1_vert_ratio);
	ppsc_w_out =
		(spsc1_w_out >> next_frame_par->supsc1_hori_ratio);
	next_frame_par->spsc1_h_in = ppsc_h_out;
	next_frame_par->spsc1_w_in = ppsc_w_out;

	vpp_set_filters2(process_3d_type, ppsc_w_in,
	ppsc_h_in, ppsc_w_out, ppsc_h_out, vinfo,
	vpp_flags, next_frame_par, vf);
	if (next_frame_par->supscl_path == sup0_pp_sp1_scpath) {
		next_frame_par->spsc1_h_in = next_frame_par->VPP_vsc_endp -
			next_frame_par->VPP_vsc_startp + 1;
		/* (ppsc_h_in<<18)/
		(next_frame_par->vpp_filter.vpp_vsc_start_phase_step>>6); */
		next_frame_par->spsc1_w_in = next_frame_par->VPP_hsc_endp -
			next_frame_par->VPP_hsc_startp + 1;
		/* (ppsc_w_in<<18)/
		(next_frame_par->vpp_filter.vpp_hsc_start_phase_step>>6); */
	}
	/*vpp_set_super_sclaer_regs(next_frame_par->supscl_path,
		next_frame_par->supsc0_enable,
		next_frame_par->spsc0_w_in,
		next_frame_par->spsc0_h_in,
		next_frame_par->supsc0_hori_ratio,
		next_frame_par->supsc0_vert_ratio,
		next_frame_par->supsc1_enable,
		next_frame_par->spsc1_w_in,
		next_frame_par->spsc1_h_in,
		next_frame_par->supsc1_hori_ratio,
		next_frame_par->supsc1_vert_ratio);*/
	if (super_debug) {
		pr_info
		("ppsc_w_in=%u, ppsc_h_in=%u, ppsc_w_out=%u, ppsc_h_out=%u.\n",
		 ppsc_w_in, ppsc_h_in, ppsc_w_out, ppsc_h_out);
		pr_info("spsc0_w_in=%u, spsc0_h_in=%u, spsc1_w_in=%u, spsc1_h_in=%u.\n",
		 next_frame_par->spsc0_w_in, next_frame_par->spsc0_h_in,
		 next_frame_par->spsc1_w_in, next_frame_par->spsc1_h_in);
	}
	/* vpp_set_ppsclaer(src_width,src_height,ppsc_w_in,
	* ppsc_h_in,vinfo,vpp_flags,next_frame_par); */
	/* cause the next_frame_par amlost were set at ppsclaer,
	and new supper scaler maybe need to change the param . */
	if ((next_frame_par->supscl_path == sup0_pp_post_blender)
		&& (next_frame_par->supsc1_enable)) {
		next_frame_par->VPP_hd_start_lines_ >>=
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_hd_end_lines_ >>=
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_vd_start_lines_ >>=
			next_frame_par->supsc1_vert_ratio;
		next_frame_par->VPP_vd_end_lines_ >>=
			next_frame_par->supsc1_vert_ratio;

	}
	if (next_frame_par->supsc0_enable) {
		/* zoom out the under parm because
		the ppscaler according to the parm that  zoom in. */
		next_frame_par->VPP_hd_start_lines_ >>=
			next_frame_par->supsc0_hori_ratio;
		next_frame_par->VPP_hd_end_lines_ >>=
			next_frame_par->supsc0_hori_ratio;
		next_frame_par->VPP_vd_start_lines_ >>=
			next_frame_par->supsc0_vert_ratio;
		next_frame_par->VPP_vd_end_lines_ >>=
			next_frame_par->supsc0_vert_ratio;
	}
	if (next_frame_par->supscl_path == sup0_pp_sp1_scpath) {
		/* zoom in the under parm because super scaler1 is open */
		next_frame_par->VPP_hsc_startp <<=
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_hsc_endp = (next_frame_par->VPP_hsc_endp <<
			next_frame_par->supsc1_hori_ratio) +
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_hsc_linear_startp =
			next_frame_par->VPP_hsc_linear_startp <<
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_hsc_linear_endp =
			(next_frame_par->VPP_hsc_linear_endp <<
			next_frame_par->supsc1_hori_ratio) +
			next_frame_par->supsc1_hori_ratio;
		next_frame_par->VPP_vsc_startp <<=
			next_frame_par->supsc1_vert_ratio;
		next_frame_par->VPP_vsc_endp =
			(next_frame_par->VPP_vsc_endp <<
			next_frame_par->supsc1_vert_ratio) +
			next_frame_par->supsc1_vert_ratio;
	}

	if (h_crop_enable) {
		next_frame_par->VPP_hd_start_lines_ += video_source_crop_left;
		next_frame_par->VPP_hd_end_lines_ += video_source_crop_left;
	}

	if (v_crop_enable) {
		next_frame_par->VPP_vd_start_lines_ += video_source_crop_top;
		next_frame_par->VPP_vd_end_lines_ += video_source_crop_top;
	}

}

#ifdef TV_3D_FUNCTION_OPEN
void get_vpp_3d_mode(u32 process_3d_type, u32 trans_fmt, u32 *vpp_3d_mode)
{
	switch (trans_fmt) {
	case TVIN_TFMT_3D_LRH_OLOR:
	case TVIN_TFMT_3D_LRH_OLER:
	case TVIN_TFMT_3D_LRH_ELOR:
	case TVIN_TFMT_3D_LRH_ELER:
	case TVIN_TFMT_3D_DET_LR:
		*vpp_3d_mode = VPP_3D_MODE_LR;
		break;
	case TVIN_TFMT_3D_FP:
	case TVIN_TFMT_3D_TB:
	case TVIN_TFMT_3D_DET_TB:
	case TVIN_TFMT_3D_FA:
		*vpp_3d_mode = VPP_3D_MODE_TB;
		if (process_3d_type & MODE_3D_MVC)
			*vpp_3d_mode = VPP_3D_MODE_FA;
		break;
	case TVIN_TFMT_3D_LA:
	case TVIN_TFMT_3D_DET_INTERLACE:
		*vpp_3d_mode = VPP_3D_MODE_LA;
		break;
	case TVIN_TFMT_3D_DET_CHESSBOARD:
	default:
		*vpp_3d_mode = VPP_3D_MODE_NULL;
		break;
	}
}
static void
vpp_get_video_source_size(u32 *src_width, u32 *src_height,
	u32 process_3d_type, struct vframe_s *vf,
	struct vpp_frame_par_s *next_frame_par)
{

	if ((process_3d_type & MODE_3D_AUTO) ||
	(((process_3d_type & MODE_3D_TO_2D_R) ||
	(process_3d_type & MODE_3D_TO_2D_L) ||
	(process_3d_type & MODE_3D_LR_SWITCH) ||
	(process_3d_type & MODE_FORCE_3D_TO_2D_TB) ||
	(process_3d_type & MODE_FORCE_3D_TO_2D_LR)) &&
	(process_3d_type & MODE_3D_ENABLE))) {
		if (vf->trans_fmt) {
			if (process_3d_type & MODE_3D_TO_2D_MASK)
				*src_height = vf->left_eye.height;
			else {
				*src_height = vf->left_eye.height << 1;
				next_frame_par->vpp_2pic_mode = 1;
			}
			*src_width = vf->left_eye.width;
		}

		switch (vf->trans_fmt) {
		case TVIN_TFMT_3D_LRH_OLOR:
		case TVIN_TFMT_3D_LRH_OLER:
		case TVIN_TFMT_3D_LRH_ELOR:
		case TVIN_TFMT_3D_LRH_ELER:
		case TVIN_TFMT_3D_DET_LR:
			next_frame_par->vpp_3d_mode = VPP_3D_MODE_LR;
			break;
		case TVIN_TFMT_3D_FP:
		case TVIN_TFMT_3D_TB:
		case TVIN_TFMT_3D_DET_TB:
		case TVIN_TFMT_3D_FA:
			next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
			/*just for mvc 3d file */
			if (process_3d_type & MODE_3D_MVC) {
				next_frame_par->vpp_2pic_mode = 2;
				next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
			}
			break;
		case TVIN_TFMT_3D_LA:
		case TVIN_TFMT_3D_DET_INTERLACE:
			next_frame_par->vpp_3d_mode = VPP_3D_MODE_LA;
			next_frame_par->vpp_2pic_mode = 0;
			break;
		case TVIN_TFMT_3D_DET_CHESSBOARD:
		default:
			*src_width = vf->width;
			*src_height = vf->height;
			next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
			next_frame_par->vpp_3d_scale = 0;
			next_frame_par->vpp_2pic_mode = 0;
			break;
		}

	} else if ((process_3d_type & MODE_3D_LR) ||
	(process_3d_type & MODE_FORCE_3D_LR)) {
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_LR;
		if (process_3d_type & MODE_3D_TO_2D_MASK) {
			*src_width = vf->width >> 1;
			*src_height = vf->height;
		} else if (process_3d_type & MODE_3D_OUT_LR) {
			*src_width = vf->width;
			*src_height = vf->height;
			next_frame_par->vpp_2pic_mode = 1;
		} else {
			*src_width = vf->width >> 1;
			*src_height = vf->height << 1;
			next_frame_par->vpp_2pic_mode = 1;
		}

	} else if ((process_3d_type & MODE_3D_TB) ||
	(process_3d_type & MODE_FORCE_3D_TB)) {
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
		if (process_3d_type & MODE_3D_TO_2D_MASK) {
			*src_width = vf->width;
			*src_height = vf->height >> 1;
		} else if (process_3d_type & MODE_3D_OUT_LR) {
			*src_width = vf->width << 1;
			*src_height = vf->height >> 1;
			next_frame_par->vpp_2pic_mode = 1;
		} else {
			*src_width = vf->width;
			*src_height = vf->height;
			next_frame_par->vpp_2pic_mode = 1;
		}
		if (process_3d_type & MODE_3D_MVC) {
			*src_width = vf->width;
			*src_height = vf->height << 1;
			next_frame_par->vpp_2pic_mode = 2;
			next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
		}
	} else if (process_3d_type & MODE_3D_LA) {
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_LA;
		*src_height = vf->height - 1;
		*src_width = vf->width;
		next_frame_par->vpp_2pic_mode = 0;
		next_frame_par->vpp_3d_scale = 1;
		if (process_3d_type & MODE_3D_TO_2D_MASK) {
			next_frame_par->vscale_skip_count = 1;
			next_frame_par->vpp_3d_scale = 0;
		} else if (process_3d_type & MODE_3D_OUT_TB) {
			*src_height = vf->height << 1;
			next_frame_par->vscale_skip_count = 1;
			next_frame_par->vpp_3d_scale = 0;
		} else if (process_3d_type & MODE_3D_OUT_LR) {
			*src_width = vf->width << 1;
			next_frame_par->vscale_skip_count = 1;
			next_frame_par->vpp_3d_scale = 0;
		}
	} else if ((process_3d_type & MODE_3D_FA)
			|| (process_3d_type & MODE_FORCE_3D_FA_LR)
			|| (process_3d_type & MODE_FORCE_3D_FA_TB)) {

		next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
		if (process_3d_type & MODE_3D_TO_2D_MASK) {

			if (process_3d_type & MODE_FORCE_3D_FA_TB) {
				next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
				*src_width = vf->width;
				*src_height = vf->height >> 1;
			}
			if (process_3d_type & MODE_FORCE_3D_FA_LR) {
				next_frame_par->vpp_3d_mode = VPP_3D_MODE_LR;
				*src_width = vf->width >> 1;
				*src_height = vf->height;
			}
			if (process_3d_type & MODE_3D_MVC) {
				*src_width = vf->width;
				*src_height = vf->height;
				next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
			}
			if (vf->trans_fmt == TVIN_TFMT_3D_FP) {
				next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
				*src_width = vf->width;
				*src_height = vf->left_eye.height;
			}
			next_frame_par->vpp_2pic_mode = 0;
		} else if (process_3d_type & MODE_3D_OUT_LR) {
			*src_width = vf->width << 1;
			*src_height = vf->height;
			next_frame_par->vpp_2pic_mode = 2;
		} else {
			*src_width = vf->width;
			*src_height = vf->height << 1;
			next_frame_par->vpp_2pic_mode = 2;
		}
	} else {
		*src_width = vf->width;
		*src_height = vf->height;
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
		next_frame_par->vpp_2pic_mode = 0;
		next_frame_par->vpp_3d_scale = 0;
	}
	/*process 3d->2d or l/r switch case */
	if ((VPP_3D_MODE_NULL != next_frame_par->vpp_3d_mode) &&
		(VPP_3D_MODE_LA != next_frame_par->vpp_3d_mode)
		&& (process_3d_type & MODE_3D_ENABLE)) {
		if (process_3d_type & MODE_3D_TO_2D_R)
			next_frame_par->vpp_2pic_mode = VPP_SELECT_PIC1;
		else if (process_3d_type & MODE_3D_TO_2D_L)
			next_frame_par->vpp_2pic_mode = VPP_SELECT_PIC0;
		else if (process_3d_type & MODE_3D_LR_SWITCH)
			next_frame_par->vpp_2pic_mode |= VPP_PIC1_FIRST;
		if ((process_3d_type & MODE_FORCE_3D_TO_2D_LR) ||
		(process_3d_type & MODE_FORCE_3D_TO_2D_TB))
			next_frame_par->vpp_2pic_mode = VPP_SELECT_PIC0;

		/*only display one pic */
		if ((next_frame_par->vpp_2pic_mode & 0x3) == 0)
			next_frame_par->vpp_3d_scale = 0;
		else
			next_frame_par->vpp_3d_scale = 1;
	}
	/*avoid dividing 0 error */
	if (*src_width == 0 || *src_height == 0) {
		*src_width = vf->width;
		*src_height = vf->height;
	}
}
#endif
void
vpp_set_filters(u32 process_3d_type, u32 wide_mode,
	struct vframe_s *vf,
	struct vpp_frame_par_s *next_frame_par,
	const struct vinfo_s *vinfo)
{
	u32 src_width = 0;
	u32 src_height = 0;
	u32 vpp_flags = 0;
	u32 aspect_ratio = 0;

	BUG_ON(vinfo == NULL);

	next_frame_par->VPP_post_blend_vd_v_start_ = 0;
	next_frame_par->VPP_post_blend_vd_h_start_ = 0;

	next_frame_par->VPP_postproc_misc_ = 0x200;
#ifdef TV_3D_FUNCTION_OPEN
	next_frame_par->vscale_skip_count = 0;
	next_frame_par->hscale_skip_count = 0;
	/*
	 *check 3d mode change in display buffer or 3d type
	 *get the source size according to 3d mode
	 */
	if (process_3d_type & MODE_3D_ENABLE) {
		vpp_get_video_source_size(&src_width, &src_height,
			process_3d_type, vf, next_frame_par);
	} else {
		if (vf->type & VIDTYPE_COMPRESS) {
			src_width = vf->compWidth;
			src_height = vf->compHeight;
		} else {
			src_width = vf->width;
			src_height = vf->height;
		}
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
		next_frame_par->vpp_2pic_mode = 0;
		next_frame_par->vpp_3d_scale = 0;
	}
	next_frame_par->trans_fmt = vf->trans_fmt;
	get_vpp_3d_mode(process_3d_type, next_frame_par->trans_fmt,
		&next_frame_par->vpp_3d_mode);
	if (vpp_3d_scale)
		next_frame_par->vpp_3d_scale = 1;
	amlog_mask(LOG_MASK_VPP, "%s: src_width %u,src_height %u.\n", __func__,
		src_width, src_height);
#endif
	/* check force ratio change flag in display buffer also
	 * if it exist then it will override the settings in display side
	 */
	if (vf->ratio_control & DISP_RATIO_FORCECONFIG) {
		if ((vf->ratio_control & DISP_RATIO_CTRL_MASK) ==
			DISP_RATIO_KEEPRATIO) {
			if (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH)
				wide_mode = VIDEO_WIDEOPTION_NORMAL;
		} else {
			if (wide_mode == VIDEO_WIDEOPTION_NORMAL)
				wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
		}
		if (vf->ratio_control & DISP_RATIO_FORCE_NORMALWIDE)
			wide_mode = VIDEO_WIDEOPTION_NORMAL;
		else if (vf->ratio_control & DISP_RATIO_FORCE_FULL_STRETCH)
			wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
	}

	aspect_ratio = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK)
				   >> DISP_RATIO_ASPECT_RATIO_BIT;

	if (vf->type & VIDTYPE_INTERLACE)
		vpp_flags = VPP_FLAG_INTERLACE_IN;

	if (vf->ratio_control & DISP_RATIO_PORTRAIT_MODE)
		vpp_flags |= VPP_FLAG_PORTRAIT_MODE;

	if (vf->type & VIDTYPE_VSCALE_DISABLE)
		vpp_flags |= VPP_FLAG_VSCALE_DISABLE;
#ifndef TV_3D_FUNCTION_OPEN
	if (vf->type & VIDTYPE_COMPRESS) {
		src_width = vf->compWidth;
		src_height = vf->compHeight;
	} else {
		src_width = vf->width;
		src_height = vf->height;
	}
#endif
	if (vf->type & VIDTYPE_MVC) {
		video_source_crop_top = 0;
		video_source_crop_left = 0;
		video_source_crop_bottom = 0;
		video_source_crop_right = 0;
	} else {
		video_source_crop_top = video_crop_top_resv;
		video_source_crop_left = video_crop_left_resv;
		video_source_crop_bottom = video_crop_bottom_resv;
		video_source_crop_right = video_crop_right_resv;
	}
	vpp_wide_mode = wide_mode;
	vpp_flags |= wide_mode | (aspect_ratio << VPP_FLAG_AR_BITS);

	if (vinfo->field_height != vinfo->height)
		vpp_flags |= VPP_FLAG_INTERLACE_OUT;

	next_frame_par->VPP_post_blend_vd_v_end_ = vinfo->field_height - 1;
	next_frame_par->VPP_post_blend_vd_h_end_ = vinfo->width - 1;
	next_frame_par->VPP_post_blend_h_size_ = vinfo->width;

	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXTVBB) {
		if (super_scaler &&
		(vpp_wide_mode != VIDEO_WIDEOPTION_NORMAL_NOSCALEUP)
		&& (!(vf->type & VIDTYPE_PIC))
		&& ((video_layer_width + video_layer_left) <= vinfo->width)) {
			next_frame_par->supscl_path = scaler_path_sel;
			vpp_set_scaler(process_3d_type, src_width, src_height,
				vinfo, vpp_flags, next_frame_par, vf);
		} else {
			next_frame_par->supsc0_enable = 0;
			next_frame_par->supsc0_hori_ratio = 0;
			next_frame_par->supsc0_vert_ratio = 0;

		next_frame_par->supsc1_enable = 0;
		next_frame_par->supsc1_hori_ratio = 0;
		next_frame_par->supsc1_vert_ratio = 0;

		next_frame_par->spsc0_w_in = src_width;
		next_frame_par->spsc0_h_in = src_height;
		next_frame_par->spsc1_w_in = vinfo->width;
		next_frame_par->spsc1_h_in = vinfo->height;

		vpp_set_filters2(process_3d_type, src_width,
		src_height, vinfo->width,
		vinfo->height, vinfo, vpp_flags,
		next_frame_par, vf);
		}
		if (super_debug)
			pr_info("VPP_hd_start_lines= %d,%d,%d,%d, %d,%d,%d,%d, %d,%d\n",
			 next_frame_par->VPP_hd_start_lines_,
			 next_frame_par->VPP_hd_end_lines_,
			 next_frame_par->VPP_vd_start_lines_,
			 next_frame_par->VPP_vd_end_lines_,
			 next_frame_par->VPP_hsc_startp,
			 next_frame_par->VPP_hsc_endp,
			 next_frame_par->VPP_hsc_linear_startp,
			 next_frame_par->VPP_hsc_linear_endp,
			 next_frame_par->VPP_vsc_startp,
			 next_frame_par->VPP_vsc_endp);
	} else {
		vpp_set_filters2(process_3d_type, src_width, src_height,
			vinfo->width, vinfo->height,
			vinfo, vpp_flags, next_frame_par, vf);
	}
}

void prot_get_parameter(u32 wide_mode,
		struct vframe_s *vf,
		struct vpp_frame_par_s *next_frame_par,
		const struct vinfo_s *vinfo)
{
	u32 src_width = 0;
	u32 src_height = 0;
	u32 vpp_flags = 0;
	u32 aspect_ratio = 0;
	u32 process_3d_type = VPP_3D_MODE_NULL;

	BUG_ON(vinfo == NULL);

	next_frame_par->VPP_post_blend_vd_v_start_ = 0;
	next_frame_par->VPP_post_blend_vd_h_start_ = 0;

	next_frame_par->VPP_postproc_misc_ = 0x200;

	/* check force ratio change flag in display buffer also
	 * if it exist then it will override the settings in display side
	 */
	if (vf->ratio_control & DISP_RATIO_FORCECONFIG) {
		if ((vf->ratio_control & DISP_RATIO_CTRL_MASK) ==
			DISP_RATIO_KEEPRATIO) {
			if (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH)
				wide_mode = VIDEO_WIDEOPTION_NORMAL;
		} else {
			if (wide_mode == VIDEO_WIDEOPTION_NORMAL)
				wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
		}
		if (vf->ratio_control & DISP_RATIO_FORCE_NORMALWIDE)
			wide_mode = VIDEO_WIDEOPTION_NORMAL;
		else if (vf->ratio_control & DISP_RATIO_FORCE_FULL_STRETCH)
			wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
	}

	aspect_ratio = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK)
				   >> DISP_RATIO_ASPECT_RATIO_BIT;

	if (vf->type & VIDTYPE_INTERLACE)
		vpp_flags = VPP_FLAG_INTERLACE_IN;

	if (vf->ratio_control & DISP_RATIO_PORTRAIT_MODE)
		vpp_flags |= VPP_FLAG_PORTRAIT_MODE;

	if (vf->type & VIDTYPE_VSCALE_DISABLE)
		vpp_flags |= VPP_FLAG_VSCALE_DISABLE;

	src_width = vf->width;
	src_height = vf->height;

	vpp_wide_mode = wide_mode;
	vpp_flags |= wide_mode | (aspect_ratio << VPP_FLAG_AR_BITS);

	if (vinfo->field_height != vinfo->height)
		vpp_flags |= VPP_FLAG_INTERLACE_OUT;

	next_frame_par->VPP_post_blend_vd_v_end_ =
	vinfo->field_height - 1;
	next_frame_par->VPP_post_blend_vd_h_end_ =
	vinfo->width - 1;
	next_frame_par->VPP_post_blend_h_size_ = vinfo->width;

	vpp_set_filters2(process_3d_type, src_width, src_height,
	vinfo->width, vinfo->height,
	vinfo, vpp_flags, next_frame_par, vf);
}

void vpp_set_osd_layer_preblend(u32 *enable)
{
	osd_layer_preblend = *enable;
}

void vpp_set_osd_layer_position(s32 *para)
{
	if (IS_ERR_OR_NULL(&para[3])) {
		pr_info("para[3] is null\n");
		return;
	}
	if (para[2] < 2 || para[3] < 2)
		return;

	osd_layer_left = para[0];
	osd_layer_top = para[1];
	osd_layer_width = para[2];
	osd_layer_height = para[3];
}

void vpp_set_video_source_crop(u32 t, u32 l, u32 b, u32 r)
{
	video_crop_top_resv = t;
	video_crop_left_resv = l;
	video_crop_bottom_resv = b;
	video_crop_right_resv = r;
}

void vpp_get_video_source_crop(u32 *t, u32 *l, u32 *b, u32 *r)
{
	*t = video_crop_top_resv;
	*l = video_crop_left_resv;
	*b = video_crop_bottom_resv;
	*r = video_crop_right_resv;
}

void vpp_set_video_layer_position(s32 x, s32 y, s32 w, s32 h)
{
	if ((w < 0) || (h < 0))
		return;

	video_layer_left = x;
	video_layer_top = y;
	video_layer_width = w;
	video_layer_height = h;
}
EXPORT_SYMBOL(vpp_set_video_layer_position);
void vpp_get_video_layer_position(s32 *x, s32 *y, s32 *w, s32 *h)
{
	*x = video_layer_left;
	*y = video_layer_top;
	*w = video_layer_width;
	*h = video_layer_height;
}
EXPORT_SYMBOL(vpp_get_video_layer_position);
void vpp_set_global_offset(s32 x, s32 y)
{
	video_layer_global_offset_x = x;
	video_layer_global_offset_y = y;
}

void vpp_get_global_offset(s32 *x, s32 *y)
{
	*x = video_layer_global_offset_x;
	*y = video_layer_global_offset_y;
}

s32 vpp_set_nonlinear_factor(u32 f)
{
	if (f < MAX_NONLINEAR_FACTOR) {
		nonlinear_factor = f;
		return 0;
	}
	return -1;
}

u32 vpp_get_nonlinear_factor(void)
{
	return nonlinear_factor;
}

void vpp_set_zoom_ratio(u32 r)
{
	vpp_zoom_ratio = r;
}

u32 vpp_get_zoom_ratio(void)
{
	return vpp_zoom_ratio;
}

void vpp_set_video_speed_check(u32 h, u32 w)
{
	video_speed_check_height = h;
	video_speed_check_width = w;
}

void vpp_get_video_speed_check(u32 *h, u32 *w)
{
	*h = video_speed_check_height;
	*w = video_speed_check_width;
}

#ifdef TV_3D_FUNCTION_OPEN
void vpp_set_3d_scale(bool enable)
{
	vpp_3d_scale = enable;
}
#endif

void vpp_super_scaler_support(void)
{
	if (is_meson_gxtvbb_cpu() || is_meson_txl_cpu() ||
		is_meson_txlx_cpu())
		super_scaler = 1;
	else
		super_scaler = 0;
}

void vpp_bypass_ratio_config(void)
{
	if (is_meson_gxbb_cpu() || is_meson_gxl_cpu() ||
		is_meson_gxm_cpu())
		bypass_ratio = 125;
	else if (is_meson_txlx_cpu() || is_meson_txl_cpu())
		bypass_ratio = 247;/*0x110 * (100/110)=0xf7*/
	else
		bypass_ratio = 205;
}


