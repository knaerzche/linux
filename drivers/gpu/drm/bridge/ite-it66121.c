// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Alex Bee <knaerzche@gmail.com>
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Phong LE <ple@baylibre.com>
 * Copyright (C) 2018-2019, Artem Mygaiev
 * Copyright (C) 2017, Fresco Logic, Incorporated.
 *
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <sound/asoundef.h>
#include <sound/hdmi-codec.h>

#define IT66121_MASTER_SEL_REG			0x10
#define IT66121_MASTER_SEL_HOST			BIT(0)

#define IT66121_AFE_DRV_REG			0x61
#define IT66121_AFE_DRV_RST			BIT(4)
#define IT66121_AFE_DRV_PWD			BIT(5)

#define IT66121_INPUT_MODE_REG			0x70
#define IT66121_INPUT_MODE_RGB			(0 << 6)
#define IT66121_INPUT_MODE_YUV422		BIT(6)
#define IT66121_INPUT_MODE_YUV444		(2 << 6)
#define IT66121_INPUT_MODE_CCIR656		BIT(4)
#define IT66121_INPUT_MODE_SYNCEMB		BIT(3)
#define IT66121_INPUT_MODE_DDR			BIT(2)

#define IT66121_INPUT_CSC_REG			0x72
#define IT66121_INPUT_CSC_ENDITHER		BIT(7)
#define IT66121_INPUT_CSC_ENUDFILTER		BIT(6)
#define IT66121_INPUT_CSC_DNFREE_GO		BIT(5)
#define IT66121_INPUT_CSC_RGB_TO_YUV		0x02
#define IT66121_INPUT_CSC_YUV_TO_RGB		0x03
#define IT66121_INPUT_CSC_NO_CONV		0x00

#define IT66121_AFE_XP_REG			0x62
#define IT66121_AFE_XP_GAINBIT			BIT(7)
#define IT66121_AFE_XP_PWDPLL			BIT(6)
#define IT66121_AFE_XP_ENI			BIT(5)
#define IT66121_AFE_XP_ENO			BIT(4)
#define IT66121_AFE_XP_RESETB			BIT(3)
#define IT66121_AFE_XP_PWDI			BIT(2)

#define IT66121_AFE_IP_REG			0x64
#define IT66121_AFE_IP_GAINBIT			BIT(7)
#define IT66121_AFE_IP_PWDPLL			BIT(6)
#define IT66121_AFE_IP_CKSEL_05			(0 << 4)
#define IT66121_AFE_IP_CKSEL_1			BIT(4)
#define IT66121_AFE_IP_CKSEL_2			(2 << 4)
#define IT66121_AFE_IP_CKSEL_2OR4		(3 << 4)
#define IT66121_AFE_IP_ER0			BIT(3)
#define IT66121_AFE_IP_RESETB			BIT(2)
#define IT66121_AFE_IP_ENC			BIT(1)
#define IT66121_AFE_IP_EC1			BIT(0)

#define IT66121_AFE_XP_EC1_REG			0x68
#define IT66121_AFE_XP_EC1_LOWCLK		BIT(4)

#define IT66121_AFE_XP_PLL_CTRL		0x6a
#define IT66121_AFE_XP_PLL_HIGH_CLK_MASK	(BIT(4) | BIT(5) | \
						 BIT(6))

#define IT66121_SW_RST_REG			0x04
#define IT66121_SW_RST_REF			BIT(5)
#define IT66121_SW_RST_AREF			BIT(4)
#define IT66121_SW_RST_VID			BIT(3)
#define IT66121_SW_RST_AUD			BIT(2)
#define IT66121_SW_RST_HDCP			BIT(0)

#define IT66121_DDC_COMMAND_REG			0x15
#define IT66121_DDC_COMMAND_BURST_READ		0x0
#define IT66121_DDC_COMMAND_EDID_READ		0x3
#define IT66121_DDC_COMMAND_FIFO_CLR		0x9
#define IT66121_DDC_COMMAND_SCL_PULSE		0xA
#define IT66121_DDC_COMMAND_ABORT		0xF

#define IT66121_HDCP_REG			0x20
#define IT66121_HDCP_CPDESIRED			BIT(0)
#define IT66121_HDCP_EN1P1FEAT			BIT(1)

#define IT66121_INT_STATUS1_REG			0x06
#define IT66121_INT_STATUS1_AUD_OVF			BIT(7)
#define IT66121_INT_STATUS1_DDC_NOACK			BIT(5)
#define IT66121_INT_STATUS1_DDC_FIFOERR			BIT(4)
#define IT66121_INT_STATUS1_DDC_BUSHANG			BIT(2)
#define IT66121_INT_STATUS1_RX_SENS_STATUS		BIT(1)
#define IT66121_INT_STATUS1_HPD_STATUS			BIT(0)

#define IT66121_INT_STATUS2_REG			0x07
#define IT66121_INT_STATUS2_PKT_3D			BIT(7)
#define IT66121_INT_STATUS2_VID_UNSTABLE		BIT(6)
#define IT66121_INT_STATUS2_PKT_ACP			BIT(5)
#define IT66121_INT_STATUS2_PKT_NULL			BIT(4)
#define IT66121_INT_STATUS2_PKT_GEN			BIT(3)
#define IT66121_INT_STATUS2_CHK_KSV_LIST		BIT(2)
#define IT66121_INT_STATUS2_AUTH_DONE			BIT(1)
#define IT66121_INT_STATUS2_AUTH_FAIL			BIT(0)

#define IT66121_INT_STATUS3_REG			0x08
#define IT66121_INT_STATUS3_AUD_CTS			BIT(6)
#define IT66121_INT_STATUS3_VSYNC			BIT(5)
#define IT66121_INT_STATUS3_VID_STABLE			BIT(4)
#define IT66121_INT_STATUS2_PKT_MPEG			BIT(3)
#define IT66121_INT_STATUS3_PKT_AUD			BIT(1)
#define IT66121_INT_STATUS3_PKT_AVI			BIT(0)

#define IT66121_DDC_HEADER_REG			0x11
#define IT66121_DDC_HEADER_HDCP			0x74
#define IT66121_DDC_HEADER_EDID			0xA0

#define IT66121_DDC_OFFSET_REG			0x12
#define IT66121_DDC_BYTE_REG			0x13
#define IT66121_DDC_SEGMENT_REG			0x14
#define IT66121_DDC_RD_FIFO_REG			0x17

#define IT66121_CLK_BANK_REG			0x0F
#define IT66121_CLK_BANK_PWROFF_RCLK		BIT(6)
#define IT66121_CLK_BANK_PWROFF_ACLK		BIT(5)
#define IT66121_CLK_BANK_PWROFF_TXCLK		BIT(4)
#define IT66121_CLK_BANK_PWROFF_CRCLK		BIT(3)
#define IT66121_CLK_BANK_0			0
#define IT66121_CLK_BANK_1			1

#define IT66121_INT_REG				0x05
#define IT66121_INT_ACTIVE_HIGH			BIT(7)
#define IT66121_INT_OPEN_DRAIN			BIT(6)
#define IT66121_INT_TX_CLK_OFF			BIT(0)

#define IT66121_INT_MASK1_REG			0x09
#define IT66121_INT_MASK1_AUD_OVF		BIT(7)
#define IT66121_INT_MASK1_DDC_NOACK		BIT(5)
#define IT66121_INT_MASK1_DDC_FIFOERR		BIT(4)
#define IT66121_INT_MASK1_DDC_BUSHANG		BIT(2)
#define IT66121_INT_MASK1_RX_SENS		BIT(1)
#define IT66121_INT_MASK1_HPD			BIT(0)

#define IT66121_INT_MASK2_REG			0x0a
#define IT66121_INT_MASK2_PKT_AVI		BIT(7)
#define IT66121_INT_MASK2_VID_UNSTABLE	BIT(6)
#define IT66121_INT_MASK2_PKT_ACP		BIT(5)
#define IT66121_INT_MASK2_PKT_NULL		BIT(4)
#define IT66121_INT_MASK2_PKT_GEN		BIT(3)
#define IT66121_INT_MASK2_CHK_KSV_LIST	BIT(2)
#define IT66121_INT_MASK2_AUTH_DONE		BIT(1)
#define IT66121_INT_MASK2_AUTH_FAIL		BIT(0)

#define IT66121_INT_MASK3_REG			0x0b
#define IT66121_INT_MASK3_PKT_3D		BIT(6)
#define IT66121_INT_MASK3_AUD_CTS		BIT(5)
#define IT66121_INT_MASK3_VSYNC		BIT(4)
#define IT66121_INT_MASK3_VID_STABLE		BIT(3)
#define IT66121_INT_MASK3_PKT_MPEG		BIT(2)
#define IT66121_INT_MASK3_PKT_AUD		BIT(0)

#define IT66121_INT_CLR1_REG			0x0C
#define IT66121_INT_CLR1_PKT_ACP			BIT(7)
#define IT66121_INT_CLR1_PKT_NULL		BIT(6)
#define IT66121_INT_CLR1_PKT_GEN			BIT(5)
#define IT66121_INT_CLR1_CHK_KSV_LIST		BIT(4)
#define IT66121_INT_CLR1_AUTH_DONE		BIT(3)
#define IT66121_INT_CLR1_AUTH_FAIL		BIT(2)
#define IT66121_INT_CLR1_RX_SENS		BIT(1)
#define IT66121_INT_CLR1_HPD			BIT(0)

#define IT66121_INT_CLR2_REG			0x0d
#define IT66121_INT_CLR2_VSYNC		BIT(7)
#define IT66121_INT_CLR2_VID_STABLE		BIT(6)
#define IT66121_INT_CLR2_PKT_MPEG		BIT(5)
#define IT66121_INT_CLR2_PKT_AUD		BIT(3)
#define IT66121_INT_CLR2_PKT_AVI		BIT(2)
#define IT66121_INT_CLR2_PKT_3D		BIT(1)
#define IT66121_INT_CLR2_VID_UNSTABLE		BIT(0)

#define IT66121_AV_MUTE_REG			0xC1
#define IT66121_AV_MUTE_ON			BIT(0)
#define IT66121_AV_MUTE_BLUESCR			BIT(1)

#define IT66121_PKT_GEN_CTRL_REG		0xC6
#define IT66121_PKT_GEN_CTRL_ON			BIT(0)
#define IT66121_PKT_GEN_CTRL_RPT		BIT(1)

#define IT66121_AVIINFO_DB1_REG			0x158
#define IT66121_AVIINFO_DB2_REG			0x159
#define IT66121_AVIINFO_DB3_REG			0x15A
#define IT66121_AVIINFO_DB4_REG			0x15B
#define IT66121_AVIINFO_DB5_REG			0x15C
#define IT66121_AVIINFO_CSUM_REG		0x15D
#define IT66121_AVIINFO_DB6_REG			0x15E
#define IT66121_AVIINFO_DB7_REG			0x15F
#define IT66121_AVIINFO_DB8_REG			0x160
#define IT66121_AVIINFO_DB9_REG			0x161
#define IT66121_AVIINFO_DB10_REG		0x162
#define IT66121_AVIINFO_DB11_REG		0x163
#define IT66121_AVIINFO_DB12_REG		0x164
#define IT66121_AVIINFO_DB13_REG		0x165

#define IT66121_AVI_INFO_PKT_REG		0xCD
#define IT66121_AVI_INFO_PKT_ON			BIT(0)
#define IT66121_AVI_INFO_PKT_RPT		BIT(1)

#define IT66121_HDMI_MODE_REG			0xC0
#define IT66121_HDMI_MODE_HDMI			BIT(0)

#define IT66121_SYS_STATUS_REG			0x0E
#define IT66121_SYS_STATUS_ACTIVE_IRQ		BIT(7)
#define IT66121_SYS_STATUS_HPDETECT		BIT(6)
#define IT66121_SYS_STATUS_SENDECTECT		BIT(5)
#define IT66121_SYS_STATUS_VID_STABLE		BIT(4)
#define IT66121_SYS_STATUS_AUD_CTS_CLR		BIT(1)
#define IT66121_SYS_STATUS_CLEAR_IRQ		BIT(0)

#define IT66121_DDC_STATUS_REG			0x16
#define IT66121_DDC_STATUS_TX_DONE		BIT(7)
#define IT66121_DDC_STATUS_ACTIVE		BIT(6)
#define IT66121_DDC_STATUS_NOACK		BIT(5)
#define IT66121_DDC_STATUS_WAIT_BUS		BIT(4)
#define IT66121_DDC_STATUS_ARBI_LOSE		BIT(3)
#define IT66121_DDC_STATUS_FIFO_FULL		BIT(2)
#define IT66121_DDC_STATUS_FIFO_EMPTY		BIT(1)
#define IT66121_DDC_STATUS_FIFO_VALID		BIT(0)

#define IT66121_VENDOR_ID0			0x54
#define IT66121_VENDOR_ID1			0x49
#define IT66121_DEVICE_ID0			0x12
#define IT66121_DEVICE_ID1			0x06
#define IT66121_DEVICE_MASK			0x0F
#define IT66121_EDID_SLEEP			20000
#define IT66121_EDID_TIMEOUT			200000
#define IT66121_EDID_FIFO_SIZE			32
#define IT66121_AFE_CLK_HIGH			80000

#define IT66121_AUD_CLK_CTRL_REG		0x58
#define IT66121_AUD_AUT_IP_CLK		BIT(0)
#define IT66121_AUD_AUT_OVR_SMPL_CLK		BIT(4)
#define IT66121_AUD_EXT_MCLK_128FS		(0 << 2)
#define IT66121_AUD_EXT_MCLK_256FS		BIT(2)
#define IT66121_AUD_EXT_MCLK_512FS		(2 << 2)
#define IT66121_AUD_EXT_MCLK_1024FS		(3 << 2)

#define IT66121_AUD_INFO_REG_SZ		5
#define IT66121_AUD_INFO_DB1_REG		0x168
#define IT66121_AUD_INFO_DB2_REG		0x169
#define IT66121_AUD_INFO_DB3_REG		0x16a
#define IT66121_AUD_INFO_DB4_REG		0x16b
#define IT66121_AUD_INFO_DB5_REG		0x16c
#define IT66121_AUD_INFO_CSUM_REG		0x16d

#define IT66121_AUD_INFO_PKT_REG		0xce
#define IT66121_AUD_INFO_PKT_ON		BIT(0)
#define IT66121_AUD_INFO_PKT_RPT		BIT(1)

#define IT66121_AUD_PKT_N1_REG		0x133
#define IT66121_AUD_PKT_N2_REG		0x134
#define IT66121_AUD_PKT_N3_REG		0x135

#define IT66121_AUD_PKT_CTS_MODE_REG		0xc5
#define IT66121_AUD_PKT_CTS_MODE_USER		BIT(1)
#define IT66121_AUD_PKT_CTS_MODE_AUTO_VAL	0
#define IT66121_AUD_PKT_CTS1_REG		0x130
#define IT66121_AUD_PKT_CTS2_REG		0x131
#define IT66121_AUD_PKT_CTS3_REG		0x132
#define IT66121_AUD_PKT_AUTO_CNT_CTS1_REG	0x135
#define IT66121_AUD_PKT_AUTO_CNT_CTS2_REG	0x136
#define IT66121_AUD_PKT_AUTO_CNT_CTS3_REG	0x137

#define IT66121_AUD_CHST_MODE_REG		0x191
#define IT66121_AUD_CHST_MODE_NLPCM		BIT(1)
#define IT66121_AUD_CHST_CAT_REG		0x192
#define IT66121_AUD_CHST_SRCNUM_REG		0x193
#define IT66121_AUD_CHST_CHTNUM_REG		0x194
#define IT66121_AUD_CHST_CA_FS_REG		0x198
#define IT66121_AUD_CHST_OFS_WL_REG		0x199

#define IT66121_AUD_CTRL0_REG			0xe0
#define IT66121_AUD_SWL_16BIT			(0 << 6)
#define IT66121_AUD_SWL_18BIT			BIT(6)
#define IT66121_AUD_SWL_20BIT			(2 << 6)
#define IT66121_AUD_SWL_24BIT			(3 << 6)
#define IT66121_AUD_SWL_MASK			IT66121_AUD_SWL_24BIT
#define IT66121_AUD_SPDIFTC			BIT(5)
#define IT66121_AUD_SPDIF			BIT(4)
#define IT66121_AUD_I2S			(0 << 4)
#define IT66121_AUD_TYPE_MASK			IT66121_AUD_SPDIF
#define IT66121_AUD_EN_I2S3			BIT(3)
#define IT66121_AUD_EN_I2S2			BIT(2)
#define IT66121_AUD_EN_I2S1			BIT(1)
#define IT66121_AUD_EN_I2S0			BIT(0)
#define IT66121_AUD_EN_I2S_MASK		0x0f
#define IT66121_AUD_EN_SPDIF			1

#define IT66121_AUD_CTRL1_REG			0xe1
#define IT66121_AUD_FMT_STD_I2S		(0 << 0)
#define IT66121_AUD_FMT_32BIT_I2S		BIT(0)
#define IT66121_AUD_FMT_LEFT_JUSTIFY		(0 << 1)
#define IT66121_AUD_FMT_RIGHT_JUSTIFY		BIT(1)
#define IT66121_AUD_FMT_DELAY_1T_TO_WS	(0 << 2)
#define IT66121_AUD_FMT_NO_DELAY_TO_WS	BIT(2)
#define IT66121_AUD_FMT_WS0_LEFT		(0 << 3)
#define IT66121_AUD_FMT_WS0_RIGHT		BIT(3)
#define IT66121_AUD_FMT_MSB_SHIFT_FIRST	(0 << 4)
#define IT66121_AUD_FMT_LSB_SHIFT_FIRST	BIT(4)
#define IT66121_AUD_FMT_RISE_EDGE_SAMPLE_WS	(0 << 5)
#define IT66121_AUD_FMT_FALL_EDGE_SAMPLE_WS	BIT(5)
#define IT66121_AUD_FMT_FULLPKT		BIT(6)

#define IT66121_AUD_FIFOMAP_REG		0xe2
#define IT66121_AUD_FIFO3_SEL			6
#define IT66121_AUD_FIFO2_SEL			4
#define IT66121_AUD_FIFO1_SEL			2
#define IT66121_AUD_FIFO0_SEL			0
#define IT66121_AUD_FIFO_SELSRC3		3
#define IT66121_AUD_FIFO_SELSRC2		2
#define IT66121_AUD_FIFO_SELSRC1		1
#define IT66121_AUD_FIFO_SELSRC0		0
#define IT66121_AUD_FIFOMAP_DEFAULT		(IT66121_AUD_FIFO_SELSRC0 \
						 << IT66121_AUD_FIFO0_SEL | \
						 IT66121_AUD_FIFO_SELSRC1 \
						   << IT66121_AUD_FIFO1_SEL | \
						 IT66121_AUD_FIFO_SELSRC2 \
						   << IT66121_AUD_FIFO2_SEL | \
						 IT66121_AUD_FIFO_SELSRC3 \
						   << IT66121_AUD_FIFO3_SEL)

#define IT66121_AUD_CTRL3_REG			0xe3
#define IT66121_AUD_MULCH			BIT(7)
#define IT66121_AUD_ZERO_CTS			BIT(6)
#define IT66121_AUD_CHSTSEL			BIT(4)
#define IT66121_AUD_S3RLCHG			BIT(3)
#define IT66121_AUD_S2RLCHG			BIT(2)
#define IT66121_AUD_S1RLCHG			BIT(1)
#define IT66121_AUD_S0RLCHG			BIT(0)
#define IT66121_AUD_SRLCHG_MASK		0x0f
#define IT66121_AUD_SRLCHG_DEFAULT		((~IT66121_AUD_S0RLCHG & \
						 ~IT66121_AUD_S1RLCHG & \
						 ~IT66121_AUD_S2RLCHG & \
						 ~IT66121_AUD_S3RLCHG) & \
						IT66121_AUD_SRLCHG_MASK)

#define IT66121_AUD_SRC_VALID_FLAT_REG	0xe4
#define IT66121_AUD_SPXFLAT_SRC3		BIT(7)
#define IT66121_AUD_SPXFLAT_SRC2		BIT(6)
#define IT66121_AUD_SPXFLAT_SRC1		BIT(5)
#define IT66121_AUD_SPXFLAT_SRC0		BIT(4)
#define IT66121_AUD_SPXFLAT_MASK		0xf0
#define IT66121_AUD_SPXFLAT_SRC_ALL		(IT66121_AUD_SPXFLAT_SRC0 | \
						 IT66121_AUD_SPXFLAT_SRC1 | \
						 IT66121_AUD_SPXFLAT_SRC2 | \
						 IT66121_AUD_SPXFLAT_SRC3)
#define IT66121_AUD_ERR2FLAT			BIT(3)
#define IT66121_AUD_S3VALID			BIT(2)
#define IT66121_AUD_S2VALID			BIT(1)
#define IT66121_AUD_S1VALID			BIT(0)

#define IT66121_AUD_HDAUDIO_REG		0xe5
#define IT66121_AUD_HBR			BIT(3)
#define IT66121_AUD_DSD			BIT(1)

struct it66121_conf {
	unsigned int input_mode_reg;
	unsigned int input_conversion_reg;
};

struct it66121_audio {
	struct platform_device *pdev;
	unsigned int sources;
	unsigned int n;
	unsigned int cts;
	unsigned int format;
	unsigned int sample_wl;
	bool is_hbr;
	struct hdmi_audio_infoframe audio_infoframe;
	struct snd_aes_iec958 aes_iec958;
};

struct it66121_ctx {
	struct regmap *regmap;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct device *dev;
	struct gpio_desc *gpio_reset;
	struct i2c_client *client;
	struct regulator_bulk_data supplies[3];
	bool dual_edge;
	const struct it66121_conf *conf;
	bool sink_has_audio;
	struct it66121_audio audio;
	struct mutex lock; /* Protects fields below and device registers */
	struct edid *edid;
	struct hdmi_avi_infoframe hdmi_avi_infoframe;
};

static const struct regmap_range_cfg it66121_regmap_banks[] = {
	{
		.name = "it66121-banks",
		.range_min = 0x30,
		.range_max = 0x1bf,
		.selector_reg = IT66121_CLK_BANK_REG,
		.selector_mask = 0x1,
		.selector_shift = 0,
		.window_start = 0x30,
		.window_len = 0xcf,
	},
};

static const struct regmap_config it66121_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,
	.max_register = 0x1bf,
	.ranges = it66121_regmap_banks,
	.num_ranges = ARRAY_SIZE(it66121_regmap_banks),
};

static const struct it66121_conf it66121_conf_simple = {
	.input_mode_reg = IT66121_INPUT_MODE_RGB | IT66121_INPUT_MODE_DDR,
	.input_conversion_reg = IT66121_INPUT_CSC_NO_CONV,
};

static const struct it66121_conf it66121fn_conf_simple = {
	.input_mode_reg = IT66121_INPUT_MODE_RGB,
	.input_conversion_reg = IT66121_INPUT_CSC_NO_CONV,
};

static void it66121_hw_reset(struct it66121_ctx *ctx)
{
	gpiod_set_value(ctx->gpio_reset, 1);
	msleep(20);
	gpiod_set_value(ctx->gpio_reset, 0);
}

static int ite66121_power_on(struct it66121_ctx *ctx)
{
	return regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int ite66121_power_off(struct it66121_ctx *ctx)
{
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int it66121_preamble_ddc(struct it66121_ctx *ctx)
{
	return regmap_write(ctx->regmap, IT66121_MASTER_SEL_REG,
				IT66121_MASTER_SEL_HOST);
}

static int it66121_fire_afe(struct it66121_ctx *ctx)
{
	return regmap_write(ctx->regmap, IT66121_AFE_DRV_REG, 0);
}

static int it66121_configure_input(struct it66121_ctx *ctx)
{
	int ret;

	ret = regmap_write(ctx->regmap, IT66121_INPUT_MODE_REG,
			   ctx->conf->input_mode_reg);
	if (ret)
		return ret;

	return regmap_write(ctx->regmap, IT66121_INPUT_CSC_REG,
			    ctx->conf->input_conversion_reg);
}

/**
 * it66121_configure_afe() - Configure the analog front end
 * @ctx: it66121_ctx object
 *
 * RETURNS:
 * zero if success, a negative error code otherwise.
 */
static int it66121_configure_afe(struct it66121_ctx *ctx,
				 const struct drm_display_mode *mode)
{
	int ret;

	ret = regmap_write(ctx->regmap, IT66121_AFE_DRV_REG,
			   IT66121_AFE_DRV_RST);
	if (ret)
		return ret;

	if (mode->clock > IT66121_AFE_CLK_HIGH) {
		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_REG,
					IT66121_AFE_XP_GAINBIT |
					IT66121_AFE_XP_ENO,
					IT66121_AFE_XP_GAINBIT);
		if (ret)
			return ret;

		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_IP_REG,
					IT66121_AFE_IP_GAINBIT |
					IT66121_AFE_IP_ER0 |
					IT66121_AFE_IP_EC1,
					IT66121_AFE_IP_GAINBIT);
		if (ret)
			return ret;

		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_EC1_REG,
					IT66121_AFE_XP_EC1_LOWCLK, 0x80);
		if (ret)
			return ret;

		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_PLL_CTRL,
					 IT66121_AFE_XP_PLL_HIGH_CLK_MASK,
					 IT66121_AFE_XP_PLL_HIGH_CLK_MASK);
		if (ret)
			return ret;

	} else {
		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_REG,
					IT66121_AFE_XP_GAINBIT |
					IT66121_AFE_XP_ENO,
					IT66121_AFE_XP_ENO);
		if (ret)
			return ret;

		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_IP_REG,
					IT66121_AFE_IP_GAINBIT |
					IT66121_AFE_IP_ER0 |
					IT66121_AFE_IP_EC1, IT66121_AFE_IP_ER0 |
					IT66121_AFE_IP_EC1);
		if (ret)
			return ret;

		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_EC1_REG,
					IT66121_AFE_XP_EC1_LOWCLK,
					IT66121_AFE_XP_EC1_LOWCLK);
		if (ret)
			return ret;


		ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_PLL_CTRL,
					IT66121_AFE_XP_PLL_HIGH_CLK_MASK,
					~(IT66121_AFE_XP_PLL_HIGH_CLK_MASK) & 0xff);
		if (ret)
			return ret;
	}

	/* Clear reset flags */
	ret = regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
				IT66121_SW_RST_REF | IT66121_SW_RST_VID,
				~(IT66121_SW_RST_REF | IT66121_SW_RST_VID) &
				0xFF);

	return ret;

}

static inline int it66121_wait_ddc_ready(struct it66121_ctx *ctx)
{
	int ret, val;

	ret = regmap_read_poll_timeout(ctx->regmap, IT66121_DDC_STATUS_REG,
				       val, true,
				       IT66121_EDID_SLEEP,
				       IT66121_EDID_TIMEOUT);
	if (ret)
		return ret;

	if (val & (IT66121_DDC_STATUS_NOACK | IT66121_DDC_STATUS_WAIT_BUS |
	    IT66121_DDC_STATUS_ARBI_LOSE))
		return -EAGAIN;

	return 0;
}

static int it66121_clear_ddc_fifo(struct it66121_ctx *ctx)
{
	int ret;

	ret = it66121_preamble_ddc(ctx);
	if (ret)
		return ret;

	return regmap_write(ctx->regmap, IT66121_DDC_COMMAND_REG,
			    IT66121_DDC_COMMAND_FIFO_CLR);
}

static int it66121_abort_ddc_ops(struct it66121_ctx *ctx)
{
	int ret;
	unsigned int swreset, cpdesire;

	ret = regmap_read(ctx->regmap, IT66121_SW_RST_REG, &swreset);
	if (ret)
		return ret;

	ret = regmap_read(ctx->regmap, IT66121_HDCP_REG, &cpdesire);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_HDCP_REG,
			   cpdesire & (~IT66121_HDCP_CPDESIRED & 0xFF));
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_SW_RST_REG,
			   swreset | IT66121_SW_RST_HDCP);
	if (ret)
		return ret;

	ret = it66121_preamble_ddc(ctx);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_DDC_COMMAND_REG,
			   IT66121_DDC_COMMAND_ABORT);
	if (ret)
		return ret;

	return it66121_wait_ddc_ready(ctx);
}

static int it66121_get_edid_block(void *context, u8 *buf,
				  unsigned int block, size_t len)
{
	struct it66121_ctx *ctx = context;
	unsigned int val;
	int remain = len;
	int offset = 0;
	int ret, cnt;

	offset = (block % 2) * len;
	block = block / 2;

	ret = regmap_read(ctx->regmap, IT66121_INT_STATUS1_REG, &val);
	if (ret)
		return ret;

	if (val & IT66121_INT_STATUS1_DDC_BUSHANG) {
		ret = it66121_abort_ddc_ops(ctx);
		if (ret)
			return ret;
	}

	ret = it66121_clear_ddc_fifo(ctx);
	if (ret)
		return ret;

	while (remain > 0) {
		cnt = (remain > IT66121_EDID_FIFO_SIZE) ?
				IT66121_EDID_FIFO_SIZE : remain;
		ret = it66121_preamble_ddc(ctx);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_COMMAND_REG,
				   IT66121_DDC_COMMAND_FIFO_CLR);
		if (ret)
			return ret;

		ret = it66121_wait_ddc_ready(ctx);
		if (ret)
			return ret;

		ret = regmap_read(ctx->regmap, IT66121_INT_STATUS1_REG, &val);
		if (ret)
			return ret;

		if (val & IT66121_INT_STATUS1_DDC_BUSHANG) {
			ret = it66121_abort_ddc_ops(ctx);
			if (ret)
				return ret;
		}

		ret = it66121_preamble_ddc(ctx);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_HEADER_REG,
				   IT66121_DDC_HEADER_EDID);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_OFFSET_REG, offset);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_BYTE_REG, cnt);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_SEGMENT_REG, block);
		if (ret)
			return ret;

		ret = regmap_write(ctx->regmap, IT66121_DDC_COMMAND_REG,
				   IT66121_DDC_COMMAND_EDID_READ);
		if (ret)
			return ret;

		offset += cnt;
		remain -= cnt;
		msleep(20);

		ret = it66121_wait_ddc_ready(ctx);
		if (ret)
			return ret;

		do {
			ret = regmap_read(ctx->regmap,
					  IT66121_DDC_RD_FIFO_REG, &val);
			if (ret)
				return ret;
			*(buf++) = val;
			cnt--;
		} while (cnt > 0);
	}

	return 0;
}

static int it66121_connector_get_modes(struct drm_connector *connector)
{
	int ret, num_modes = 0;
	struct it66121_ctx *ctx = container_of(connector, struct it66121_ctx,
			connector);

	if (ctx->edid)
		return drm_add_edid_modes(connector, ctx->edid);

	mutex_lock(&ctx->lock);

	ctx->edid = drm_do_get_edid(connector, it66121_get_edid_block, ctx);
	if (!ctx->edid) {
		DRM_ERROR("Failed to read EDID\n");
		goto unlock;
	}

	ret = drm_connector_update_edid_property(connector,
						 ctx->edid);
	if (ret) {
		DRM_ERROR("Failed to update EDID property: %d\n", ret);
		goto unlock;
	}

	num_modes = drm_add_edid_modes(connector, ctx->edid);
	ctx->sink_has_audio = drm_detect_monitor_audio(ctx->edid);

unlock:
	mutex_unlock(&ctx->lock);

	return num_modes;
}

static bool it66121_is_hpd_detect(struct it66121_ctx *ctx)
{
	int val;

	if (regmap_read(ctx->regmap, IT66121_SYS_STATUS_REG, &val))
		return false;

	return (val & IT66121_SYS_STATUS_HPDETECT);
}

static int it66121_connector_detect_ctx(struct drm_connector *connector,
					struct drm_modeset_acquire_ctx *c,
					bool force)
{
	struct it66121_ctx *ctx = container_of(connector, struct it66121_ctx,
			connector);

	return (it66121_is_hpd_detect(ctx)) ?
		connector_status_connected : connector_status_disconnected;
}

static enum drm_mode_status
it66121_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	unsigned long max_clock;
	struct it66121_ctx *ctx = container_of(connector, struct it66121_ctx,
			connector);

	max_clock = ctx->dual_edge ? 74250 : 148500;

	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static struct drm_connector_helper_funcs it66121_connector_helper_funcs = {
	.get_modes = it66121_connector_get_modes,
	.detect_ctx = it66121_connector_detect_ctx,
	.mode_valid = it66121_connector_mode_valid,
};

static const struct drm_connector_funcs it66121_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int it66121_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	int ret;
	struct it66121_ctx *ctx = container_of(bridge, struct it66121_ctx,
			bridge);

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR) {
		DRM_ERROR("Fix bridge driver to make connector optional!");
		return -EINVAL;
	}

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	ret = regmap_write_bits(ctx->regmap, IT66121_CLK_BANK_REG,
				IT66121_CLK_BANK_PWROFF_RCLK, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_INT_REG,
				IT66121_INT_TX_CLK_OFF, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_DRV_REG,
				IT66121_AFE_DRV_PWD, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_REG,
				IT66121_AFE_XP_PWDI | IT66121_AFE_XP_PWDPLL, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_IP_REG,
				IT66121_AFE_IP_PWDPLL, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_DRV_REG,
				IT66121_AFE_DRV_RST, 0);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_XP_REG,
				IT66121_AFE_XP_RESETB, IT66121_AFE_XP_RESETB);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AFE_IP_REG,
				IT66121_AFE_IP_RESETB, IT66121_AFE_IP_RESETB);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
				IT66121_SW_RST_REF,
				IT66121_SW_RST_REF);
	if (ret)
		return ret;

	msleep(50);

	ret = drm_connector_init(bridge->dev, &ctx->connector,
				 &it66121_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	ctx->connector.polled = DRM_CONNECTOR_POLL_HPD;
	drm_connector_helper_add(&ctx->connector,
				 &it66121_connector_helper_funcs);

	ret = drm_connector_attach_encoder(&ctx->connector, bridge->encoder);
	if (ret)
		return ret;

	ret = drm_connector_register(&ctx->connector);
	if (ret)
		return ret;

	/* Start interrupts */
	ret = regmap_write_bits(ctx->regmap, IT66121_INT_MASK3_REG,
				 IT66121_INT_MASK3_VID_STABLE,
				 ~(IT66121_INT_MASK3_VID_STABLE) & 0xff);
	if (ret)
		return ret;

	return regmap_write_bits(ctx->regmap, IT66121_INT_MASK1_REG,
				 IT66121_INT_MASK1_DDC_NOACK |
				 IT66121_INT_MASK1_HPD |
				 IT66121_INT_MASK1_DDC_FIFOERR |
				 IT66121_INT_MASK1_DDC_BUSHANG,
				 ~(IT66121_INT_MASK1_DDC_NOACK |
				 IT66121_INT_MASK1_HPD |
				 IT66121_INT_MASK1_DDC_FIFOERR |
				 IT66121_INT_MASK1_DDC_BUSHANG) & 0xFF);
}

static int it66121_set_mute(struct it66121_ctx *ctx, bool mute)
{
	int ret;
	unsigned int val;

	val = mute ? IT66121_AV_MUTE_ON : (~IT66121_AV_MUTE_ON & 0xFF);
	ret = regmap_write_bits(ctx->regmap, IT66121_AV_MUTE_REG,
				IT66121_AV_MUTE_ON, val);
	if (ret)
		return ret;

	return regmap_write(ctx->regmap, IT66121_PKT_GEN_CTRL_REG,
			    IT66121_PKT_GEN_CTRL_ON |
			    IT66121_PKT_GEN_CTRL_RPT);
}

static void it66121_bridge_enable(struct drm_bridge *bridge)
{
	struct it66121_ctx *ctx = container_of(bridge, struct it66121_ctx,
			bridge);

	it66121_set_mute(ctx, false);
}

static void it66121_bridge_disable(struct drm_bridge *bridge)
{
	struct it66121_ctx *ctx = container_of(bridge, struct it66121_ctx,
			bridge);

	it66121_set_mute(ctx, true);
}

static
void it66121_bridge_mode_set(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode)
{
	int ret, i;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	struct it66121_ctx *ctx = container_of(bridge, struct it66121_ctx,
			bridge);
	const u16 aviinfo_reg[HDMI_AVI_INFOFRAME_SIZE] = {
		IT66121_AVIINFO_DB1_REG,
		IT66121_AVIINFO_DB2_REG,
		IT66121_AVIINFO_DB3_REG,
		IT66121_AVIINFO_DB4_REG,
		IT66121_AVIINFO_DB5_REG,
		IT66121_AVIINFO_DB6_REG,
		IT66121_AVIINFO_DB7_REG,
		IT66121_AVIINFO_DB8_REG,
		IT66121_AVIINFO_DB9_REG,
		IT66121_AVIINFO_DB10_REG,
		IT66121_AVIINFO_DB11_REG,
		IT66121_AVIINFO_DB12_REG,
		IT66121_AVIINFO_DB13_REG
	};

	mutex_lock(&ctx->lock);

	hdmi_avi_infoframe_init(&ctx->hdmi_avi_infoframe);

	ret = drm_hdmi_avi_infoframe_from_display_mode(&ctx->hdmi_avi_infoframe,
						       &ctx->connector,
						       adjusted_mode);
	if (ret) {
		DRM_ERROR("Failed to setup AVI infoframe: %d\n", ret);
		goto unlock;
	}

	ret = hdmi_avi_infoframe_pack(&ctx->hdmi_avi_infoframe, buf,
				      sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("Failed to pack infoframe: %d\n", ret);
		goto unlock;
	}

	/* Write new AVI infoframe packet */
	for (i = 0; i < HDMI_AVI_INFOFRAME_SIZE; i++) {
		if (regmap_write(ctx->regmap, aviinfo_reg[i],
				 buf[i + HDMI_INFOFRAME_HEADER_SIZE]))
			goto unlock;
	}
	if (regmap_write(ctx->regmap, IT66121_AVIINFO_CSUM_REG, buf[3]))
		goto unlock;

	/* Enable AVI infoframe */
	if (regmap_write(ctx->regmap, IT66121_AVI_INFO_PKT_REG,
			 IT66121_AVI_INFO_PKT_ON |
			 IT66121_AVI_INFO_PKT_RPT))
		goto unlock;

	/* Set TX mode to HDMI */
	if (regmap_write(ctx->regmap, IT66121_HDMI_MODE_REG,
			 IT66121_HDMI_MODE_HDMI))
		goto unlock;

	if (regmap_write_bits(ctx->regmap, IT66121_CLK_BANK_REG,
			      IT66121_CLK_BANK_PWROFF_TXCLK,
			      IT66121_CLK_BANK_PWROFF_TXCLK))
		goto unlock;

	if (it66121_configure_input(ctx))
		goto unlock;

	if (it66121_configure_afe(ctx, adjusted_mode))
		goto unlock;

	regmap_write_bits(ctx->regmap, IT66121_CLK_BANK_REG,
			  IT66121_CLK_BANK_PWROFF_TXCLK,
			  ~IT66121_CLK_BANK_PWROFF_TXCLK & 0xFF);

unlock:
	mutex_unlock(&ctx->lock);
}

static const struct drm_bridge_funcs it66121_bridge_funcs = {
	.attach = it66121_bridge_attach,
	.enable = it66121_bridge_enable,
	.disable = it66121_bridge_disable,
	.mode_set = it66121_bridge_mode_set,
};

/* Audio related functions */

static int it66121_audio_reset_fifo(struct it66121_ctx *ctx)
{
	int ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
				IT66121_SW_RST_AUD,
				 IT66121_SW_RST_AUD);
	if (ret)
		return ret;

	usleep_range(2000, 4000);

	return regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
			  IT66121_SW_RST_AUD,
			  ~(IT66121_SW_RST_AUD) & 0xff);
}

static int it66121_audio_set_infoframe(struct it66121_ctx *ctx)
{
	int i, ret;
	u8 infoframe_buf[HDMI_INFOFRAME_SIZE(AUDIO)];

	const u16 audioinfo_reg[IT66121_AUD_INFO_REG_SZ] = {
		IT66121_AUD_INFO_DB1_REG,
		IT66121_AUD_INFO_DB2_REG,
		IT66121_AUD_INFO_DB3_REG,
		IT66121_AUD_INFO_DB4_REG,
		IT66121_AUD_INFO_DB5_REG
	};

	ret = hdmi_audio_infoframe_pack(&ctx->audio.audio_infoframe, infoframe_buf,
					sizeof(infoframe_buf));
	if (ret < 0) {
		dev_err(ctx->dev, "%s: failed to pack audio infoframe: %d\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < IT66121_AUD_INFO_REG_SZ; i++) {
		ret = regmap_write(ctx->regmap, audioinfo_reg[i],
				   infoframe_buf[i + HDMI_INFOFRAME_HEADER_SIZE]);
		if (ret)
			return ret;
	}

	/*
	 * TODO: linux defines 10 bytes; currently only 5 are filled
	 * if that ever changes checksum will differ since
	 * it66121 takes max 5 bytes -> calc checksum here????
	 */
	ret = regmap_write(ctx->regmap, IT66121_AUD_INFO_CSUM_REG, infoframe_buf[3]);

	if (ret)
		return ret;

	return regmap_write(ctx->regmap, IT66121_AUD_INFO_PKT_REG,
			    IT66121_AUD_INFO_PKT_ON | IT66121_AUD_INFO_PKT_RPT);
}

static int it66121_audio_set_cts_n(struct it66121_ctx *ctx)
{
	int ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_N1_REG,
			   ctx->audio.n & 0xff);
	if (ret)
		return ret;
	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_N2_REG,
			   (ctx->audio.n >> 8) & 0xff);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_N3_REG,
			   (ctx->audio.n >> 16) & 0xf);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_CTS1_REG,
			   ctx->audio.cts & 0xff);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_CTS2_REG,
			   (ctx->audio.cts >> 8) & 0xff);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_PKT_CTS3_REG,
			   (ctx->audio.cts >> 16) & 0xf);
	if (ret)
		return ret;

	/*
	 * magic values ("password") have to be written to
	 * f8 register to enable writing to IT66121_AUD_PKT_CTS_MODE_REG
	 */

	ret = regmap_write(ctx->regmap, 0xf8, 0xc3);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, 0xf8, 0xa5);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap,
				IT66121_AUD_PKT_CTS_MODE_REG,
				IT66121_AUD_PKT_CTS_MODE_USER,
				ctx->audio.cts == IT66121_AUD_PKT_CTS_MODE_AUTO_VAL
				  ? ~IT66121_AUD_PKT_CTS_MODE_USER & 0xff
				  : IT66121_AUD_PKT_CTS_MODE_USER);
	if (ret)
		return ret;

	/*
	 * disabling write to IT66121_AUD_PKT_CTS_MODE_REG
	 * again by overwriting perviously set "password"
	 */
	return regmap_write(ctx->regmap, 0xf8, 0xff);
}

static int it66121_audio_set_channel_status(struct it66121_ctx *ctx)
{
	int ret;
	unsigned int val;

	/* TODO: check: always use NLPCM - would to cover LPCM also?*/
	val = IT66121_AUD_CHST_MODE_NLPCM;
	val |= ctx->audio.aes_iec958.status[0] & IEC958_AES0_CON_NOT_COPYRIGHT
		? BIT(3)
		: (0 << 3);
	val |= (ctx->audio.aes_iec958.status[0] & IEC958_AES0_CON_EMPHASIS) << 4;
	ret = regmap_write(ctx->regmap,
			   IT66121_AUD_CHST_MODE_REG,
			   val);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap,
			   IT66121_AUD_CHST_CAT_REG,
			   ctx->audio.aes_iec958.status[1]);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap,
			   IT66121_AUD_CHST_SRCNUM_REG,
			   ctx->audio.aes_iec958.status[2] &
			   IEC958_AES2_CON_SOURCE);
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap,
			   IT66121_AUD_CHST_CHTNUM_REG,
			   ((ctx->audio.aes_iec958.status[2] &
			   IEC958_AES2_CON_CHANNEL) >> 4));
	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap,
			   IT66121_AUD_CHST_CA_FS_REG,
			   (ctx->audio.aes_iec958.status[3] &
			   IEC958_AES3_CON_CLOCK) << 2 |
			   (ctx->audio.aes_iec958.status[3] &
			   IEC958_AES3_CON_FS));
	if (ret)
		return ret;

	return regmap_write(ctx->regmap,
			    IT66121_AUD_CHST_OFS_WL_REG,
			    ctx->audio.aes_iec958.status[4]);
}

static int it66121_audio_mute(struct it66121_ctx *ctx, bool enable)
{
	return regmap_write_bits(ctx->regmap, IT66121_AUD_CTRL0_REG,
				 IT66121_AUD_EN_I2S_MASK,
				 enable ? 0x0 : ctx->audio.sources);
}

static int it66121_audio_set_controls(struct it66121_ctx *ctx)
{
	int ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_AUD_CTRL0_REG,
				IT66121_AUD_TYPE_MASK | IT66121_AUD_SWL_MASK,
				IT66121_AUD_I2S | ctx->audio.sample_wl);

	if (ret)
		return ret;

	ret = regmap_write(ctx->regmap, IT66121_AUD_CTRL1_REG,
			   ctx->audio.format);
	if (ret)
		return ret;

	/* default fifo mapping: fifo0 => source0, fifo1 => source1, ... */
	ret = regmap_write(ctx->regmap, IT66121_AUD_FIFOMAP_REG,
			   IT66121_AUD_FIFOMAP_DEFAULT);
	if (ret)
		return ret;

	/* Do not swap R/L for any source */
	ret = regmap_write_bits(ctx->regmap, IT66121_AUD_CTRL3_REG,
				IT66121_AUD_SRLCHG_MASK,
				IT66121_AUD_SRLCHG_DEFAULT);
	if (ret)
		return ret;

	/* "unflat" all sources */
	ret = regmap_write_bits(ctx->regmap,
				IT66121_AUD_SRC_VALID_FLAT_REG,
				IT66121_AUD_SPXFLAT_MASK,
				~IT66121_AUD_SPXFLAT_SRC_ALL & 0xff);
	if (ret)
		return ret;

	/* TODO: check if we really support HBR audio yet */
	return regmap_write_bits(ctx->regmap, IT66121_AUD_HDAUDIO_REG,
				 IT66121_AUD_HBR,
				 ctx->audio.is_hbr
				   ? IT66121_AUD_HBR
				   : ~IT66121_AUD_HBR & 0xff);
}

static int it66121_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	int ret;
	unsigned int sources = 0;

	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	if (!ctx->sink_has_audio) {
		dev_err(ctx->dev, "%s: sink has no audio", __func__);
		return -EINVAL;
	}

	/* for now i2s only */
	if (daifmt->bit_clk_master | daifmt->frame_clk_master) {
		dev_err(ctx->dev,
			"%s: only clk_master and frame_clk_master formats are supported\n",
			__func__);
		return -EINVAL;
	}

	/* TODO: move all these switches in functions ? */
	switch (daifmt->fmt) {
	case HDMI_I2S:
		ctx->audio.format = IT66121_AUD_FMT_32BIT_I2S;
		break;
	case HDMI_RIGHT_J:
		ctx->audio.format = IT66121_AUD_FMT_RIGHT_JUSTIFY;
		break;
	case HDMI_LEFT_J:
		ctx->audio.format = IT66121_AUD_FMT_LEFT_JUSTIFY;
		break;
	default:
		dev_err(ctx->dev, "%s: unsupported daiformat: %u\n",
			__func__, daifmt->fmt);
		return -EINVAL;
	}

	switch (params->channels) {
	case 7 ... 8:
		ctx->audio.sources |= IT66121_AUD_EN_I2S3;
		sources++;
		fallthrough;
	case 5 ... 6:
		ctx->audio.sources |= IT66121_AUD_EN_I2S2;
		sources++;
		fallthrough;
	case 3 ... 4:
		ctx->audio.sources |= IT66121_AUD_EN_I2S1;
		sources++;
		fallthrough;
	case 1 ... 2:
		ctx->audio.sources |= IT66121_AUD_EN_I2S0;
		sources++;
		break;
	default:
		dev_err(ctx->dev, "%s: unsupported channel count: %d\n",
			__func__, params->channels);
		return -EINVAL;
	}

	switch (params->sample_width) {
	case 16:
		ctx->audio.sample_wl = IT66121_AUD_SWL_16BIT;
		break;
	case 18:
		ctx->audio.sample_wl = IT66121_AUD_SWL_18BIT;
		break;
	case 20:
		ctx->audio.sample_wl = IT66121_AUD_SWL_20BIT;
		break;
	case 24:
	case 32:
		/* assume 24 bit wordlength for 32 bit width */
		ctx->audio.sample_wl = IT66121_AUD_SWL_24BIT;
		break;
	default:
		dev_err(ctx->dev, "%s: unsupported sample width: %d\n",
			__func__, params->channels);
		return -EINVAL;
	}

	switch (params->sample_rate) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		ctx->audio.n = 128 * params->sample_rate / 1000;
		ctx->audio.is_hbr = false;
		break;
	case 44100:
	case 88200:
	case 176400:
		ctx->audio.n = 128 * params->sample_rate / 900;
		ctx->audio.is_hbr = false;
		break;
	case 768000:
		ctx->audio.n = 24576;
		ctx->audio.is_hbr = true;
		break;
	default:
	dev_err(ctx->dev, "%s: unsupported sample_rate: %d\n",
		__func__, params->sample_rate);
		return -EINVAL;
	}

	/* not all bits are correctly filled in snd_aes_iec958 - fill them here */
	if ((params->iec.status[2] & IEC958_AES2_CON_SOURCE) == IEC958_AES2_CON_SOURCE_UNSPEC)
		params->iec.status[2] |= sources;

	if ((params->iec.status[2] & IEC958_AES2_CON_CHANNEL) == IEC958_AES2_CON_CHANNEL_UNSPEC)
		params->iec.status[2] |= params->channels << 4;

	/* OFs is 1-complement of Fs */
	if ((params->iec.status[4] & IEC958_AES4_CON_ORIGFS) == IEC958_AES4_CON_ORIGFS_NOTID &&
	    (params->iec.status[3] & IEC958_AES3_CON_FS) != IEC958_AES3_CON_FS_NOTID)
		params->iec.status[4] |= (~(params->iec.status[3] & IEC958_AES3_CON_FS) & 0xf) << 4;

	ctx->audio.format |= IT66121_AUD_FMT_FULLPKT;
	ctx->audio.cts = IT66121_AUD_PKT_CTS_MODE_AUTO_VAL;
	ctx->audio.audio_infoframe = params->cea;
	ctx->audio.aes_iec958 = params->iec;

	ret = it66121_audio_reset_fifo(ctx);
	if (ret)
		return ret;

	ret = it66121_audio_set_infoframe(ctx);
	if (ret) {
		dev_err(ctx->dev,
			"%s: failed to assemble/enable audio infoframe: %d\n",
			__func__, ret);
		/* TODO: Really fail here? */
		return ret;
	}

	ret = it66121_audio_set_cts_n(ctx);
	if (ret) {
		dev_err(ctx->dev,
			"%s: failed to write cts/n values: %d\n",
			__func__, ret);
		return ret;
	}

	ret = it66121_audio_set_channel_status(ctx);
	if (ret) {
		dev_err(ctx->dev,
			"%s: failed to write channel_status %d\n",
			__func__, ret);
		return ret;
	}

	ret = it66121_audio_set_controls(ctx);
	if (ret) {
		dev_err(ctx->dev,
			"%s: failed to write audio controls %d\n",
			__func__, ret);
		return ret;
	}

	return ret;
}

static void it66121_audio_shutdown(struct device *dev, void *data)
{
	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
			  IT66121_SW_RST_AUD | IT66121_SW_RST_AREF,
			  IT66121_SW_RST_AUD | IT66121_SW_RST_AREF);

	regmap_write(ctx->regmap, IT66121_AUD_CLK_CTRL_REG, 0x0);

	regmap_write_bits(ctx->regmap, IT66121_INT_MASK1_REG,
			  IT66121_INT_MASK1_AUD_OVF,
			  IT66121_INT_MASK1_AUD_OVF);

	regmap_write_bits(ctx->regmap, IT66121_CLK_BANK_REG,
			  IT66121_CLK_BANK_PWROFF_ACLK,
			  IT66121_CLK_BANK_PWROFF_ACLK);

	regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
			  IT66121_SW_RST_AUD | IT66121_SW_RST_AREF,
			  ~(IT66121_SW_RST_AUD | IT66121_SW_RST_AREF) & 0xff);
}

static int it66121_audio_startup(struct device *dev, void *data)
{
	int ret;

	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	ret = regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
				IT66121_SW_RST_AUD | IT66121_SW_RST_AREF,
				IT66121_SW_RST_AUD | IT66121_SW_RST_AREF);
	if (ret)
		return ret;
	/* TODO: check how to determine Fs at runtime -> only for spdif???*/
	ret = regmap_write(ctx->regmap, IT66121_AUD_CLK_CTRL_REG,
			   IT66121_AUD_AUT_OVR_SMPL_CLK |
			   IT66121_AUD_EXT_MCLK_256FS);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_CLK_BANK_REG,
				IT66121_CLK_BANK_PWROFF_ACLK,
				~IT66121_CLK_BANK_PWROFF_ACLK & 0xff);
	if (ret)
		return ret;

	ret = regmap_write_bits(ctx->regmap, IT66121_INT_MASK1_REG,
				IT66121_INT_MASK1_AUD_OVF,
				~IT66121_INT_MASK1_AUD_OVF & 0xff);
	if (ret)
		return ret;

	return regmap_write_bits(ctx->regmap, IT66121_SW_RST_REG,
				 IT66121_SW_RST_AUD | IT66121_SW_RST_AREF,
				  ~(IT66121_SW_RST_AUD | IT66121_SW_RST_AREF) & 0xff);
}

int it66121_audio_mute_stream(struct device *dev, void *data, bool enable,
				int direction)
{
	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	return it66121_audio_mute(ctx, enable);
}

static int it66121_audio_get_dai_id(struct snd_soc_component *component,
				    struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static int it66121_audio_get_eld(struct device *dev, void *data,
				 u8 *buf, size_t len)
{
	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	memcpy(buf, ctx->connector.eld,
	       min(sizeof(ctx->connector.eld), len));

	return 0;
}

static const struct hdmi_codec_ops it66121_audio_codec_ops = {
	.audio_shutdown = it66121_audio_shutdown,
	.audio_startup = it66121_audio_startup,
	.mute_stream = it66121_audio_mute_stream,
	.no_capture_mute = 1,
	.hw_params = it66121_audio_hw_params,
	.get_eld = it66121_audio_get_eld,
	.get_dai_id = it66121_audio_get_dai_id,
};

static const struct hdmi_codec_pdata codec_data = {
	.ops = &it66121_audio_codec_ops,
	.i2s = 1, /* Only i2s support for now. */
	.spdif = 0,
	.max_i2s_channels = 8,
};

static int it66121_audio_codec_init(struct it66121_ctx *it66121,
				    struct device *dev)
{
	struct it66121_ctx *ctx = dev_get_drvdata(dev);

	ctx->audio.pdev = platform_device_register_data(
			  dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
			  &codec_data, sizeof(codec_data));

	if (IS_ERR(ctx->audio.pdev))
		return PTR_ERR(ctx->audio.pdev);

	DRM_INFO("%s has been bound to to HDMITX it66121\n",
		 HDMI_CODEC_DRV_NAME);

	return 0;
}

static void it66121_audio_codec_exit(struct it66121_ctx *ctx)
{
	if (ctx->audio.pdev) {
		platform_device_unregister(ctx->audio.pdev);
		ctx->audio.pdev = NULL;
	}
}

static irqreturn_t it66121_irq_threaded_handler(int irq, void *dev_id)
{
	int ret;
	unsigned int val, sys_status;
	struct it66121_ctx *ctx = dev_id;
	struct device *dev = ctx->dev;
	bool event = false;

	mutex_lock(&ctx->lock);

	ret = regmap_read(ctx->regmap, IT66121_SYS_STATUS_REG, &sys_status);
	if (ret)
		goto unlock;

	if (sys_status & IT66121_SYS_STATUS_ACTIVE_IRQ) {
		ret = regmap_read(ctx->regmap, IT66121_INT_STATUS1_REG, &val);
		if (ret) {
			dev_err(dev, "Cannot read STATUS1_REG %d\n", ret);
		} else {
			if (val & IT66121_INT_STATUS1_DDC_FIFOERR)
				it66121_clear_ddc_fifo(ctx);
			if (val & (IT66121_INT_STATUS1_DDC_BUSHANG |
					IT66121_INT_STATUS1_DDC_NOACK))
				it66121_abort_ddc_ops(ctx);
			if (val & IT66121_INT_STATUS1_HPD_STATUS) {
				regmap_write_bits(ctx->regmap,
						  IT66121_INT_CLR1_REG,
						  IT66121_INT_CLR1_HPD,
						  IT66121_INT_CLR1_HPD);

				if (!it66121_is_hpd_detect(ctx)) {
					kfree(ctx->edid);
					ctx->edid = NULL;
				}
				event = true;
			}
			if (val & IT66121_INT_STATUS1_AUD_OVF)
				it66121_audio_reset_fifo(ctx);
		}

		ret = regmap_read(ctx->regmap, IT66121_INT_STATUS3_REG, &val);
		if (ret) {
			dev_err(dev, "Cannot read STATUS3_REG %d\n", ret);
		} else if (val) {
			if (val & IT66121_INT_STATUS3_VID_STABLE) {
				if (sys_status & IT66121_SYS_STATUS_VID_STABLE)
					it66121_fire_afe(ctx);

				regmap_write_bits(ctx->regmap,
						  IT66121_INT_CLR2_REG,
						  IT66121_INT_CLR2_VID_STABLE,
						  IT66121_INT_CLR2_VID_STABLE);
			}
		}

		regmap_write_bits(ctx->regmap, IT66121_SYS_STATUS_REG,
				  IT66121_SYS_STATUS_CLEAR_IRQ,
				  IT66121_SYS_STATUS_CLEAR_IRQ);
	}

unlock:
	mutex_unlock(&ctx->lock);

	if (event)
		drm_helper_hpd_irq_event(ctx->bridge.dev);

	return IRQ_HANDLED;
}

static int it66121_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	u8 ids[4];
	int i, ret;
	struct it66121_ctx *ctx;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "I2C check functionality failed.\n");
		return -ENXIO;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->client = client;
	i2c_set_clientdata(client, ctx);
	mutex_init(&ctx->lock);
	ctx->conf = (struct it66121_conf *)of_device_get_match_data(dev);
	if (!ctx->conf)
		return -ENODEV;

	ctx->supplies[0].supply = "vcn33";
	ctx->supplies[1].supply = "vcn18";
	ctx->supplies[2].supply = "vrf12";
	ret = devm_regulator_bulk_get(ctx->dev, 3, ctx->supplies);
	if (ret) {
		dev_err(ctx->dev, "regulator_bulk failed\n");
		return ret;
	}

	ctx->dual_edge = of_property_read_bool(dev->of_node, "pclk-dual-edge");

	ret = ite66121_power_on(ctx);
	if (ret)
		return ret;

	it66121_hw_reset(ctx);

	ctx->regmap = devm_regmap_init_i2c(client, &it66121_regmap_config);
	if (IS_ERR(ctx->regmap)) {
		ite66121_power_off(ctx);
		return PTR_ERR(ctx);
	}

	for (i = 0; i < 4; i++) {
		regmap_read(ctx->regmap, i, &ret);
		ids[i] = ret;
	}

	if (ids[0] != IT66121_VENDOR_ID0 ||
	    ids[1] != IT66121_VENDOR_ID1 ||
	    ids[2] != IT66121_DEVICE_ID0 ||
	    ((ids[3] & IT66121_DEVICE_MASK) != IT66121_DEVICE_ID1)) {
		ite66121_power_off(ctx);
		DRM_INFO("HDMITX it66121 could not be indentified.\n");
		return -ENODEV;
	} else
		DRM_INFO("HDMITX it66121 rev %d succsessfully indentified.\n",
		         ids[3] >> 4);

	ctx->bridge.funcs = &it66121_bridge_funcs;
	ctx->bridge.of_node = dev->of_node;

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					it66121_irq_threaded_handler,
					IRQF_SHARED | IRQF_TRIGGER_LOW |
					IRQF_ONESHOT,
					dev_name(dev),
					ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq %d:%d\n", client->irq, ret);
		ite66121_power_off(ctx);
		return ret;
	}

	drm_bridge_add(&ctx->bridge);

	ret = it66121_audio_codec_init(ctx, dev);
	if (ret) {
		dev_err(dev, "Failed to initialize audio codec %d\n", ret);
		return ret;
	}

	return 0;
}

static int it66121_remove(struct i2c_client *client)
{
	struct it66121_ctx *ctx = i2c_get_clientdata(client);

	it66121_audio_codec_exit(ctx);
	ite66121_power_off(ctx);
	drm_bridge_remove(&ctx->bridge);
	kfree(ctx->edid);
	mutex_destroy(&ctx->lock);

	return 0;
}

static const struct of_device_id it66121_dt_match[] = {
	{ .compatible = "ite,it66121",
	  .data = &it66121_conf_simple,
	},
	{ .compatible = "ite,it66121fn",
	  .data = &it66121fn_conf_simple,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, it66121_dt_match);

static const struct i2c_device_id it66121_id[] = {
	{ "it66121", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, it66121_id);

static struct i2c_driver it66121_driver = {
	.driver = {
		.name	= "it66121",
		.of_match_table = it66121_dt_match,
	},
	.probe = it66121_probe,
	.remove = it66121_remove,
	.id_table = it66121_id,
};

module_i2c_driver(it66121_driver);

MODULE_AUTHOR("Alex Bee");
MODULE_AUTHOR("Phong LE");
MODULE_DESCRIPTION("IT66121 HDMI transmitter driver");
MODULE_LICENSE("GPL v2");
