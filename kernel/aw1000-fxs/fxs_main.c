// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "fxs.h"
#include "fxs_proto.h"
#include "fxs_regs.h"
#include "fxs_uapi.h"

static char *compat = "arcadyan,aw1000";
module_param(compat, charp, 0444);

static struct fxs_dev fxs;

static void ring_wd_fire(struct timer_list *t)
{
	fxs_slic_reset(&fxs, true);
	pr_warn("aw1000-fxs: ring watchdog reset SLIC\n");
}

static int fxs_open(struct inode *i, struct file *f)
{
	mutex_lock(&fxs.lock);
	if (fxs.open) {
		mutex_unlock(&fxs.lock);
		return -EBUSY;
	}
	fxs.open = true;
	fxs.irq_seen = atomic_read(&fxs.irq_count);
	mutex_unlock(&fxs.lock);
	return 0;
}

static int fxs_release(struct inode *i, struct file *f)
{
	timer_delete_sync(&fxs.ring_wd);
	mutex_lock(&fxs.lock);
	fxs_slic_reset(&fxs, true);
	fxs_pcm_stop(&fxs);
	fxs_irq_ack(&fxs);
	fxs.open = false;
	mutex_unlock(&fxs.lock);
	return 0;
}

static long fxs_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	struct fxs_reg r;
	struct fxs_ram m;
	struct fxs_pcmreg pr;
	u32 v;
	long ret = 0;

	mutex_lock(&fxs.lock);
	switch (cmd) {
	case FXS_RD_REG:
		if (copy_from_user(&r, p, sizeof(r))) {
			ret = -EFAULT;
			break;
		}
		fxs_reg_read(&fxs, r.ch, r.addr, &r.val);
		if (copy_to_user(p, &r, sizeof(r)))
			ret = -EFAULT;
		break;
	case FXS_WR_REG:
		if (copy_from_user(&r, p, sizeof(r))) {
			ret = -EFAULT;
			break;
		}
		fxs_reg_write(&fxs, r.ch, r.addr, r.val);
		break;
	case FXS_RD_RAM:
		if (copy_from_user(&m, p, sizeof(m))) {
			ret = -EFAULT;
			break;
		}
		ret = fxs_ram_read(&fxs, m.ch, m.addr, &m.val);
		if (!ret && copy_to_user(p, &m, sizeof(m)))
			ret = -EFAULT;
		break;
	case FXS_WR_RAM:
		if (copy_from_user(&m, p, sizeof(m))) {
			ret = -EFAULT;
			break;
		}
		ret = fxs_ram_write(&fxs, m.ch, m.addr, m.val);
		break;
	case FXS_RESET:
		if (get_user(v, (u32 __user *)p)) {
			ret = -EFAULT;
			break;
		}
		fxs_slic_reset(&fxs, v != 0);
		break;
	case FXS_RING_ARM:
		if (get_user(v, (u32 __user *)p)) {
			ret = -EFAULT;
			break;
		}
		if (v)
			mod_timer(&fxs.ring_wd, jiffies + msecs_to_jiffies(v));
		else
			timer_delete(&fxs.ring_wd);
		break;
	case FXS_PCM_ENABLE:
		if (get_user(v, (u32 __user *)p)) {
			ret = -EFAULT;
			break;
		}
		if (v)
			fxs_pcm_start(&fxs);
		else
			fxs_pcm_stop(&fxs);
		break;
	case FXS_IRQ_ACK:
		fxs_irq_ack(&fxs);
		break;
	case FXS_PCM_RD:
		if (copy_from_user(&pr, p, sizeof(pr))) {
			ret = -EFAULT;
			break;
		}
		ret = fxs_pcm_rd(&fxs, pr.off, &pr.val);
		if (!ret && copy_to_user(p, &pr, sizeof(pr)))
			ret = -EFAULT;
		break;
	case FXS_PCM_WR:
		if (copy_from_user(&pr, p, sizeof(pr))) {
			ret = -EFAULT;
			break;
		}
		ret = fxs_pcm_wr(&fxs, pr.off, pr.val);
		break;
	case FXS_PCM_CAPTURE: {
		struct fxs_pcmcap *c = kzalloc(sizeof(*c), GFP_KERNEL);

		if (!c) {
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(c, p, sizeof(*c)))
			ret = -EFAULT;
		else
			ret = fxs_pcm_capture(&fxs, c->bitmap, c->ms, c->w, &c->n);
		if (!ret && copy_to_user(p, c, sizeof(*c)))
			ret = -EFAULT;
		kfree(c);
		break;
	}
	case FXS_PCM_BRIDGE:
		if (get_user(v, (u32 __user *)p)) {
			ret = -EFAULT;
			break;
		}
		ret = fxs_pcm_bridge(&fxs, v);
		break;
	case FXS_PCM_STAT: {
		struct fxs_pcmstat st = {
			.moved = READ_ONCE(fxs.br_moved),
			.resync = READ_ONCE(fxs.br_resync),
			.ticks = READ_ONCE(fxs.br_ticks),
			.status = READ_ONCE(fxs.br_status),
			.peak_a = READ_ONCE(fxs.br_peak_a),
			.peak_b = READ_ONCE(fxs.br_peak_b),
		};

		fxs.br_peak_a = fxs.br_peak_b = 0;

		if (copy_to_user(p, &st, sizeof(st)))
			ret = -EFAULT;
		break;
	}
	case FXS_AUDIO_STAT: {
		struct fxs_austat st = {
			.under = READ_ONCE(fxs.au_under),
			.over = READ_ONCE(fxs.au_over),
			.drop = READ_ONCE(fxs.au_drop),
			.avail = fxs_au_avail(&fxs),
		};

		if (copy_to_user(p, &st, sizeof(st)))
			ret = -EFAULT;
		break;
	}
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&fxs.lock);
	return ret;
}

static ssize_t fxs_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct fxs_event ev;
	int ret;

	if (len < sizeof(ev))
		return -EINVAL;
	ret = wait_event_interruptible(fxs.evq,
		atomic_read(&fxs.irq_count) != fxs.irq_seen);
	if (ret)
		return ret;
	ev.irq_count = atomic_read(&fxs.irq_count);
	fxs.irq_seen = ev.irq_count;
	if (copy_to_user(buf, &ev, sizeof(ev)))
		return -EFAULT;
	return sizeof(ev);
}

static __poll_t fxs_poll(struct file *f, poll_table *w)
{
	poll_wait(f, &fxs.evq, w);
	return atomic_read(&fxs.irq_count) != fxs.irq_seen ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations fxs_fops = {
	.owner = THIS_MODULE,
	.open = fxs_open,
	.release = fxs_release,
	.unlocked_ioctl = fxs_ioctl,
	.read = fxs_read,
	.poll = fxs_poll,
	.llseek = noop_llseek,
};

static struct miscdevice fxs_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fxs0",
	.fops = &fxs_fops,
};

#define AU_CHUNK	1024

static int au_open(struct inode *i, struct file *f)
{
	u32 *buf = kmalloc_array(AU_CHUNK, sizeof(u32), GFP_KERNEL);

	if (!buf)
		return -ENOMEM;
	mutex_lock(&fxs.lock);
	if (fxs.au_open) {
		mutex_unlock(&fxs.lock);
		kfree(buf);
		return -EBUSY;
	}
	fxs.au_open = true;
	mutex_unlock(&fxs.lock);
	f->private_data = buf;
	return 0;
}

static int au_release(struct inode *i, struct file *f)
{
	kfree(f->private_data);
	mutex_lock(&fxs.lock);
	fxs.au_open = false;
	mutex_unlock(&fxs.lock);
	return 0;
}

static ssize_t au_read(struct file *f, char __user *ubuf, size_t len, loff_t *off)
{
	u32 *buf = f->private_data;
	u32 n;
	int ret;

	if (len < sizeof(u32))
		return -EINVAL;
	if (!(f->f_flags & O_NONBLOCK)) {
		ret = wait_event_interruptible(fxs.auq, !READ_ONCE(fxs.br_user) ||
			fxs_au_avail(&fxs) >= fxs_au_period(&fxs));
		if (ret)
			return ret;
	}
	n = fxs_au_read(&fxs, buf, min_t(size_t, len / sizeof(u32), AU_CHUNK));
	if (!n)
		return READ_ONCE(fxs.br_user) ? -EAGAIN : 0;
	if (copy_to_user(ubuf, buf, n * sizeof(u32)))
		return -EFAULT;
	return n * sizeof(u32);
}

static ssize_t au_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	u32 *buf = f->private_data;
	u32 n = min_t(size_t, len / sizeof(u32), AU_CHUNK);

	if (!READ_ONCE(fxs.br_user))
		return -EIO;
	if (!n)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, n * sizeof(u32)))
		return -EFAULT;
	fxs_au_write(&fxs, buf, n);
	return n * sizeof(u32);
}

static __poll_t au_poll(struct file *f, poll_table *w)
{
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	poll_wait(f, &fxs.auq, w);
	if (READ_ONCE(fxs.br_user) && fxs_au_avail(&fxs) >= fxs_au_period(&fxs))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations au_fops = {
	.owner = THIS_MODULE,
	.open = au_open,
	.release = au_release,
	.read = au_read,
	.write = au_write,
	.poll = au_poll,
	.llseek = noop_llseek,
};

static struct miscdevice au_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fxs0audio",
	.fops = &au_fops,
};

static void fxs_unmap(void)
{
	if (fxs.tlmm)
		iounmap(fxs.tlmm);
	if (fxs.pcm)
		iounmap(fxs.pcm);
	if (fxs.adss_cc)
		iounmap(fxs.adss_cc);
	if (fxs.adss_glb)
		iounmap(fxs.adss_glb);
	if (fxs.gcc)
		iounmap(fxs.gcc);
}

static int __init fxs_init(void)
{
	int ret;

	if (!of_machine_is_compatible(compat)) {
		pr_err("aw1000-fxs: unsupported board\n");
		return -ENODEV;
	}
	memset(&fxs, 0, sizeof(fxs));
	mutex_init(&fxs.lock);
	init_waitqueue_head(&fxs.evq);
	init_waitqueue_head(&fxs.auq);
	spin_lock_init(&fxs.au_lock);
	timer_setup(&fxs.ring_wd, ring_wd_fire, 0);

	fxs.au_rx = kcalloc(FXS_AU_RING, sizeof(u32), GFP_KERNEL);
	fxs.au_tx = kcalloc(FXS_AU_RING, sizeof(u32), GFP_KERNEL);
	if (!fxs.au_rx || !fxs.au_tx) {
		ret = -ENOMEM;
		goto err_map;
	}

	fxs.gcc = ioremap(GCC_BASE, GCC_SIZE);
	fxs.adss_glb = ioremap(ADSS_GLB_BASE, ADSS_GLB_SIZE);
	fxs.adss_cc = ioremap(ADSS_CC_BASE, ADSS_CC_SIZE);
	fxs.pcm = ioremap(PCM_BASE, PCM_SIZE);
	fxs.tlmm = ioremap(TLMM_BASE, TLMM_SIZE);
	if (!fxs.gcc || !fxs.adss_glb || !fxs.adss_cc || !fxs.pcm || !fxs.tlmm) {
		ret = -ENOMEM;
		goto err_map;
	}

	ret = fxs_clk_enable(&fxs);
	if (ret)
		goto err_map;
	pr_info("aw1000-fxs: clocks on, pll mode=%08x\n", readl(fxs.gcc + APLL_MODE));

	ret = fxs_spi_init(&fxs);
	if (ret)
		goto err_clk;

	ret = misc_register(&fxs_misc);
	if (ret)
		goto err_spi;
	ret = misc_register(&au_misc);
	if (ret)
		goto err_misc;
	return 0;

err_misc:
	misc_deregister(&fxs_misc);
err_spi:
	fxs_spi_exit(&fxs);
err_clk:
	fxs_clk_disable(&fxs);
err_map:
	fxs_unmap();
	kfree(fxs.au_rx);
	kfree(fxs.au_tx);
	return ret;
}

static void __exit fxs_exit(void)
{
	misc_deregister(&au_misc);
	misc_deregister(&fxs_misc);
	timer_delete_sync(&fxs.ring_wd);
	fxs_spi_exit(&fxs);
	fxs_pcm_stop(&fxs);
	fxs_clk_disable(&fxs);
	fxs_unmap();
	kfree(fxs.au_rx);
	kfree(fxs.au_tx);
}

module_init(fxs_init);
module_exit(fxs_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("perz3us");
MODULE_DESCRIPTION("Arcadyan AW1000 FXS port");
