// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include "fxs.h"
#include "fxs_regs.h"
#include "fxs_proto.h"

#define SPI_HALF_NS	600
#define TLMM_LABEL	"1000000.pinctrl"
#define S_SPI_TLMM	24

static const unsigned int spi_pins[] = { 50, 51, 52, 53 };

static struct gpiod_lookup_table fxs_gpios = {
	.dev_id = "aw1000-fxs",
	.table = {
		GPIO_LOOKUP(TLMM_LABEL, 46, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(TLMM_LABEL, 47, "irq", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(TLMM_LABEL, 50, "sclk", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(TLMM_LABEL, 51, "cs", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(TLMM_LABEL, 52, "miso", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(TLMM_LABEL, 53, "mosi", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpio_desc *get(struct fxs_dev *d, const char *con, enum gpiod_flags fl)
{
	return gpiod_get(&d->pdev->dev, con, fl);
}

static void put(struct gpio_desc **g)
{
	if (!IS_ERR_OR_NULL(*g)) {
		gpiod_direction_input(*g);
		gpiod_put(*g);
	}
	*g = NULL;
}

static u8 xfer(struct fxs_dev *d, u8 out)
{
	u8 in = 0;
	int b;

	gpiod_set_raw_value(d->cs, 0);
	ndelay(SPI_HALF_NS);
	for (b = 7; b >= 0; b--) {
		gpiod_set_raw_value(d->sclk, 0);
		gpiod_set_raw_value(d->mosi, (out >> b) & 1);
		ndelay(SPI_HALF_NS);
		gpiod_set_raw_value(d->sclk, 1);
		in = (in << 1) | gpiod_get_raw_value(d->miso);
		ndelay(SPI_HALF_NS);
	}
	gpiod_set_raw_value(d->cs, 1);
	ndelay(SPI_HALF_NS);
	return in;
}

int fxs_reg_write(struct fxs_dev *d, u8 ch, u8 addr, u8 val)
{
	xfer(d, fxs_ctrl_byte(0, 1, ch));
	xfer(d, addr);
	xfer(d, val);
	return 0;
}

int fxs_reg_read(struct fxs_dev *d, u8 ch, u8 addr, u8 *val)
{
	xfer(d, fxs_ctrl_byte(1, 1, ch));
	xfer(d, addr);
	*val = xfer(d, 0);
	return 0;
}

static int ram_wait(struct fxs_dev *d, u8 ch)
{
	unsigned int n = 1000;
	u8 st;

	while (n--) {
		fxs_reg_read(d, ch, FXS_REG_RAMSTAT, &st);
		if (!(st & 1))
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

int fxs_ram_read(struct fxs_dev *d, u8 ch, u16 addr, u32 *val)
{
	u8 b[4];
	int i;

	fxs_reg_write(d, ch, FXS_REG_RAM_ADDR_HI, fxs_ram_addr_hi(addr));
	fxs_reg_write(d, ch, FXS_REG_RAM_ADDR_LO, fxs_ram_addr_lo(addr));
	if (ram_wait(d, ch))
		return -ETIMEDOUT;
	for (i = 0; i < 4; i++)
		fxs_reg_read(d, ch, FXS_REG_RAM_D0 + i, &b[i]);
	*val = fxs_ram_unpack(b);
	return 0;
}

int fxs_ram_write(struct fxs_dev *d, u8 ch, u16 addr, u32 val)
{
	u8 b[4];
	int i;

	fxs_ram_pack(val, b);
	fxs_reg_write(d, ch, FXS_REG_RAM_ADDR_HI, fxs_ram_addr_hi(addr));
	for (i = 0; i < 4; i++)
		fxs_reg_write(d, ch, FXS_REG_RAM_D0 + i, b[i]);
	fxs_reg_write(d, ch, FXS_REG_RAM_ADDR_LO, fxs_ram_addr_lo(addr));
	return ram_wait(d, ch);
}

void fxs_slic_reset(struct fxs_dev *d, bool assert)
{
	gpiod_set_raw_value(d->rst, assert ? 0 : 1);
}

/*
 * SLIC INT is level-low until the IRQ registers are read. Mask on entry and
 * unmask from FXS_IRQ_ACK after userspace has cleared the sources.
 */
static irqreturn_t fxs_irq(int irq, void *data)
{
	struct fxs_dev *d = data;

	disable_irq_nosync(irq);
	atomic_set(&d->irq_masked, 1);
	atomic_inc(&d->irq_count);
	wake_up_interruptible(&d->evq);
	return IRQ_HANDLED;
}

void fxs_irq_ack(struct fxs_dev *d)
{
	if (d->spi_on && atomic_cmpxchg(&d->irq_masked, 1, 0) == 1)
		enable_irq(d->irq);
}

int fxs_spi_init(struct fxs_dev *d)
{
	int i, ret;

	gpiod_add_lookup_table(&fxs_gpios);
	d->pdev = platform_device_register_simple("aw1000-fxs", -1, NULL, 0);
	if (IS_ERR(d->pdev)) {
		ret = PTR_ERR(d->pdev);
		d->pdev = NULL;
		gpiod_remove_lookup_table(&fxs_gpios);
		return ret;
	}

	d->rst = get(d, "reset", GPIOD_OUT_LOW);
	d->irq_gpio = get(d, "irq", GPIOD_IN);
	d->sclk = get(d, "sclk", GPIOD_OUT_HIGH);
	d->cs = get(d, "cs", GPIOD_OUT_HIGH);
	d->miso = get(d, "miso", GPIOD_IN);
	d->mosi = get(d, "mosi", GPIOD_OUT_HIGH);
	if (IS_ERR(d->rst) || IS_ERR(d->irq_gpio) || IS_ERR(d->sclk) ||
	    IS_ERR(d->cs) || IS_ERR(d->miso) || IS_ERR(d->mosi)) {
		ret = -EBUSY;
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(spi_pins); i++) {
		void __iomem *r = d->tlmm + TLMM_CFG(spi_pins[i]);

		d->save[S_SPI_TLMM + i] = readl(r);
		writel((readl(r) & ~TLMM_DRV_MASK) | TLMM_DRV_8MA, r);
	}
	d->spi_drv_set = true;

	d->irq = gpiod_to_irq(d->irq_gpio);
	if (d->irq < 0) {
		ret = d->irq;
		goto err;
	}
	ret = request_irq(d->irq, fxs_irq, IRQF_TRIGGER_LOW, "aw1000-fxs", d);
	if (ret)
		goto err;
	d->spi_on = true;
	return 0;
err:
	fxs_spi_exit(d);
	return ret;
}

void fxs_spi_exit(struct fxs_dev *d)
{
	if (d->spi_on) {
		fxs_irq_ack(d);
		free_irq(d->irq, d);
		d->spi_on = false;
	}
	if (!IS_ERR_OR_NULL(d->rst)) {
		gpiod_set_raw_value(d->rst, 0);
		udelay(10);
	}
	put(&d->mosi);
	put(&d->miso);
	put(&d->cs);
	put(&d->sclk);
	put(&d->irq_gpio);
	put(&d->rst);
	if (d->spi_drv_set) {
		int i;

		for (i = 0; i < ARRAY_SIZE(spi_pins); i++) {
			void __iomem *r = d->tlmm + TLMM_CFG(spi_pins[i]);

			writel((readl(r) & ~TLMM_DRV_MASK) |
			       (d->save[S_SPI_TLMM + i] & TLMM_DRV_MASK), r);
		}
		d->spi_drv_set = false;
	}
	if (d->pdev) {
		platform_device_unregister(d->pdev);
		d->pdev = NULL;
		gpiod_remove_lookup_table(&fxs_gpios);
	}
}
