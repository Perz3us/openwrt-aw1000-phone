/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef FXS_REGS_H
#define FXS_REGS_H

#include <linux/bits.h>

#define GCC_BASE		0x01800000
#define GCC_SIZE		0x50000
#define GCC_PLL_VOTE		0x0b000
#define GCC_PLL_VOTE_AUDIO	BIT(1)
#define GCC_ADSS_PWM_CMD	0x1c008
#define GCC_ADSS_PWM_CFG	0x1c00c
#define GCC_ADSS_PWM_CBCR	0x1c020

#define APLL			0x4b000
#define APLL_MODE		(APLL + 0x00)
#define APLL_L			(APLL + 0x04)
#define APLL_ALPHA		(APLL + 0x08)
#define APLL_ALPHA_U		(APLL + 0x0c)
#define APLL_USER		(APLL + 0x10)
#define APLL_CONFIG		(APLL + 0x18)
#define APLL_MODE_OUTCTRL	BIT(0)
#define APLL_MODE_BYPASSNL	BIT(1)
#define APLL_MODE_RESET_N	BIT(2)
#define APLL_MODE_FSM_ENA	BIT(20)
#define APLL_LOCK		BIT(31)
#define APLL_USER_ALPHA_EN	BIT(24)
#define APLL_USER_POSTDIV	GENMASK(9, 8)
#define APLL_USER_MAIN_OUT	BIT(0)

#define ADSS_GLB_BASE		0x07700000
#define ADSS_GLB_SIZE		0x34
#define GLB_CHIP_CTRL_I2S	0x10
#define GLB_AUDIO_MODE		0x30
#define GLB_AUDIO_MODE_B1K	BIT(28)

#define ADSS_CC_BASE		0x07700100
#define ADSS_CC_SIZE		0x200
#define CC_PCM_CMD		0xa0
#define CC_PCM_CFG		0xa4
#define CC_PCM_POSTDIV		0xa8
#define CC_PCM_CBCR		0xac

#define PCM_BASE		0x07704000
#define PCM_SIZE		0x40
#define PCM_BITMAP		0x00
#define PCM_CTRL		0x04
#define PCM_OFFSET		0x08
#define PCM_START		0x0c
#define PCM_DIVIDER		0x20
#define PCM_INT_STATUS		0x10
#define PCM_INT_ENABLE		0x14
#define PCM_TH			0x24
#define PCM_FIFO_CNT		0x28
#define PCM_RX16		0x30
#define PCM_TX16		0x34
#define PCM_CTRL_CPU		BIT(29)
#define PCM_REG_LAST		0x34

#define TLMM_BASE		0x01000000
#define TLMM_SIZE		0x50000
#define TLMM_CFG(n)		(0x1000 * (n))
#define TLMM_DRV_MASK		GENMASK(8, 6)
#define TLMM_DRV_8MA		(3 << 6)

#define RCG_UPDATE		BIT(0)
#define RCG_ROOT_OFF		BIT(31)
#define CBCR_EN			BIT(0)
#define CBCR_OFF		BIT(31)

#endif
