/*
 * alice-peek - safe kernel memory reader for reversing sprdwl_ng internals.
 * Uses probe_kernel_read(), so a bad address returns -EFAULT instead of an Oops.
 *
 *   echo "0xffffffc00e734000 512" > /sys/kernel/debug/alice2/addr
 *   cat /sys/kernel/debug/alice2/peek
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#define PEEK_MAX 1024

static struct dentry *dir;
static unsigned long peek_addr;
static unsigned int peek_len = 256;
static char outbuf[PEEK_MAX * 5];

static ssize_t addr_write(struct file *f, const char __user *ubuf, size_t n,
			  loff_t *ppos)
{
	char buf[64];
	unsigned long a;
	unsigned int l = 0;

	if (n == 0 || n >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, n))
		return -EFAULT;
	buf[n] = '\0';

	if (sscanf(buf, "%li %u", &a, &l) < 1)
		return -EINVAL;
	if (l > PEEK_MAX)
		l = PEEK_MAX;
	peek_addr = a;
	if (l)
		peek_len = l;
	pr_info("alice2: peek addr=%px len=%u\n", (void *)peek_addr, peek_len);
	return n;
}

static const struct file_operations addr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = addr_write,
	.llseek = default_llseek,
};

static ssize_t peek_show(struct file *f, char __user *ubuf, size_t n,
			 loff_t *ppos)
{
	u8 tmp[PEEK_MAX];
	int len = 0, i;
	int ret;

	ret = probe_kernel_read(tmp, (void *)peek_addr, peek_len);
	if (ret) {
		len = scnprintf(outbuf, sizeof(outbuf),
				"probe_kernel_read(%px, %u) = %d\n",
				(void *)peek_addr, peek_len, ret);
		return simple_read_from_buffer(ubuf, n, ppos, outbuf, len);
	}

	for (i = 0; i < (int)peek_len && len < (int)sizeof(outbuf) - 80; i++) {
		len += scnprintf(outbuf + len, sizeof(outbuf) - len,
				 "%04x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
				 i, tmp[i], tmp[i + 1], tmp[i + 2], tmp[i + 3],
				 tmp[i + 4], tmp[i + 5], tmp[i + 6], tmp[i + 7]);
		i += 7;
	}

	return simple_read_from_buffer(ubuf, n, ppos, outbuf, len);
}

static const struct file_operations peek_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = peek_show,
	.llseek = default_llseek,
};

static int __init peek_init(void)
{
	dir = debugfs_create_dir("alice2", NULL);
	if (!dir)
		return -ENOMEM;
	debugfs_create_file("addr", 0200, dir, NULL, &addr_fops);
	debugfs_create_file("peek", 0444, dir, NULL, &peek_fops);
	pr_info("alice2: peek loaded\n");
	return 0;
}

static void __exit peek_exit(void)
{
	debugfs_remove_recursive(dir);
	pr_info("alice2: peek unloaded\n");
}

module_init(peek_init);
module_exit(peek_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Safe kernel memory peek for alice");
