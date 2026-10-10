// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include "fxs.h"
#include "fxs_regs.h"

static bool fail_pll;
module_param(fail_pll, bool, 0444);

enum { S_PWM_CMD, S_PWM_CFG, S_PWM_CBCR, S_VOTE, S_L, S_ALPHA, S_ALPHA_U, S_USER,
       S_PCM_CFG, S_PCM_POSTDIV, S_PCM_CBCR, S_GLB_I2S, S_GLB_MODE, S_MODE, S_CLK_END };

static int wait_clear(void __iomem *r, u32 bit, unsigned int us)
{
	while (us--) {
		if (!(readl(r) & bit))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int wait_set(void __iomem *r, u32 bit, unsigned int us)
{
	while (us--) {
		if (readl(r) & bit)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int rcg_update(void __iomem *cmd)
{
	writel(readl(cmd) | RCG_UPDATE, cmd);
	return wait_clear(cmd, RCG_UPDATE, 1000);
}

int fxs_clk_enable(struct fxs_dev *d)
{
	void __iomem *g = d->gcc;
	u32 *s = d->save;
	u32 v;

	s[S_PWM_CMD] = readl(g + GCC_ADSS_PWM_CMD);
	s[S_PWM_CFG] = readl(g + GCC_ADSS_PWM_CFG);
	s[S_PWM_CBCR] = readl(g + GCC_ADSS_PWM_CBCR);
	s[S_VOTE] = readl(g + GCC_PLL_VOTE);
	s[S_L] = readl(g + APLL_L);
	s[S_ALPHA] = readl(g + APLL_ALPHA);
	s[S_ALPHA_U] = readl(g + APLL_ALPHA_U);
	s[S_USER] = readl(g + APLL_USER);
	s[S_MODE] = readl(g + APLL_MODE);

	if ((s[S_PWM_CBCR] & CBCR_EN) ||
	    (s[S_MODE] & (APLL_MODE_OUTCTRL | APLL_LOCK)) ||
	    (s[S_VOTE] & GCC_PLL_VOTE_AUDIO)) {
		pr_err("aw1000-fxs: ADSS clock or audio PLL already in use\n");
		return -EBUSY;
	}

	writel(0, g + GCC_ADSS_PWM_CFG);
	if (rcg_update(g + GCC_ADSS_PWM_CMD))
		goto fail;
	writel(s[S_PWM_CBCR] | CBCR_EN, g + GCC_ADSS_PWM_CBCR);
	d->clk_on = true;
	if (wait_clear(g + GCC_ADSS_PWM_CBCR, CBCR_OFF, 2000))
		goto fail;

	/* 19.2 MHz * (40 + 0.96) = 786.432 MHz, post-div /4 = 196.608 MHz */
	writel(0x28, g + APLL_L);
	writel(0xC28F5C29, g + APLL_ALPHA);
	writel(0xF5, g + APLL_ALPHA_U);
	v = s[S_USER] & ~APLL_USER_POSTDIV;
	v |= FIELD_PREP(APLL_USER_POSTDIV, 3) | APLL_USER_ALPHA_EN | APLL_USER_MAIN_OUT;
	writel(v, g + APLL_USER);
	d->pll_on = true;
	if (s[S_MODE] & APLL_MODE_FSM_ENA) {
		writel(readl(g + GCC_PLL_VOTE) | GCC_PLL_VOTE_AUDIO, g + GCC_PLL_VOTE);
	} else {
		/* manual start: bypass off, 5 us, out of reset, lock, output on */
		writel(s[S_MODE] | APLL_MODE_BYPASSNL, g + APLL_MODE);
		udelay(5);
		writel(readl(g + APLL_MODE) | APLL_MODE_RESET_N, g + APLL_MODE);
	}
	if (fail_pll || wait_set(g + APLL_MODE, APLL_LOCK, 5000)) {
		pr_err("aw1000-fxs: audio PLL did not lock (mode=%08x)\n",
		       readl(g + APLL_MODE));
		goto fail;
	}
	if (!(s[S_MODE] & APLL_MODE_FSM_ENA))
		writel(readl(g + APLL_MODE) | APLL_MODE_OUTCTRL, g + APLL_MODE);

	s[S_GLB_I2S] = readl(d->adss_glb + GLB_CHIP_CTRL_I2S);
	s[S_GLB_MODE] = readl(d->adss_glb + GLB_AUDIO_MODE);
	s[S_PCM_CFG] = readl(d->adss_cc + CC_PCM_CFG);
	s[S_PCM_POSTDIV] = readl(d->adss_cc + CC_PCM_POSTDIV);
	s[S_PCM_CBCR] = readl(d->adss_cc + CC_PCM_CBCR);
	d->adss_on = true;

	writel(s[S_GLB_I2S] | BIT(0), d->adss_glb + GLB_CHIP_CTRL_I2S);
	mdelay(5);
	writel(s[S_GLB_MODE] | GLB_AUDIO_MODE_B1K, d->adss_glb + GLB_AUDIO_MODE);

	/* audio PLL /12 = 16.384 MHz, post-div /2 = 8.192 MHz = 4 x PCLK */
	writel((1 << 8) | 23, d->adss_cc + CC_PCM_CFG);
	if (rcg_update(d->adss_cc + CC_PCM_CMD))
		goto fail;
	writel(1, d->adss_cc + CC_PCM_POSTDIV);
	writel(s[S_PCM_CBCR] | CBCR_EN, d->adss_cc + CC_PCM_CBCR);
	if (wait_clear(d->adss_cc + CC_PCM_CBCR, CBCR_OFF, 2000))
		goto fail;
	return 0;

fail:
	fxs_clk_disable(d);
	return -EIO;
}

void fxs_clk_disable(struct fxs_dev *d)
{
	void __iomem *g = d->gcc;
	u32 *s = d->save;

	if (d->adss_on) {
		writel(s[S_PCM_CBCR], d->adss_cc + CC_PCM_CBCR);
		writel(s[S_PCM_POSTDIV], d->adss_cc + CC_PCM_POSTDIV);
		writel(s[S_PCM_CFG], d->adss_cc + CC_PCM_CFG);
		rcg_update(d->adss_cc + CC_PCM_CMD);
		writel(s[S_GLB_MODE], d->adss_glb + GLB_AUDIO_MODE);
		writel(s[S_GLB_I2S], d->adss_glb + GLB_CHIP_CTRL_I2S);
		d->adss_on = false;
	}
	if (d->pll_on) {
		if (s[S_MODE] & APLL_MODE_FSM_ENA) {
			writel(readl(g + GCC_PLL_VOTE) & ~GCC_PLL_VOTE_AUDIO, g + GCC_PLL_VOTE);
		} else {
			writel(readl(g + APLL_MODE) & ~APLL_MODE_OUTCTRL, g + APLL_MODE);
			writel(readl(g + APLL_MODE) & ~APLL_MODE_RESET_N, g + APLL_MODE);
			writel(s[S_MODE], g + APLL_MODE);
		}
		writel(s[S_USER], g + APLL_USER);
		writel(s[S_ALPHA_U], g + APLL_ALPHA_U);
		writel(s[S_ALPHA], g + APLL_ALPHA);
		writel(s[S_L], g + APLL_L);
		d->pll_on = false;
	}
	if (d->clk_on) {
		writel(s[S_PWM_CBCR], g + GCC_ADSS_PWM_CBCR);
		writel(s[S_PWM_CFG], g + GCC_ADSS_PWM_CFG);
		rcg_update(g + GCC_ADSS_PWM_CMD);
		d->clk_on = false;
	}
}
