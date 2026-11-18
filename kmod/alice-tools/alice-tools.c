/*
 * alice-tools - instrumentation module for Yandex Station Micro (UWE5621)
 *
 * Resolves internal sprdwl_ng driver functions via kallsyms and exposes a
 * debugfs interface to talk to the Wi-Fi firmware directly.
 *
 *   /sys/kernel/debug/alice/priv    - read: driver priv pointer / wiphy name
 *   /sys/kernel/debug/alice/syncver - write anything: driver init handshake
 *   /sys/kernel/debug/alice/fwcmd   - write "cmd=84 ctx=0 data=0102ff"
 *
 * Loading with cmd=.. data=.. runs a single command at init time.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/kallsyms.h>

#define ALICE_MAX_PAYLOAD 256
#define ALICE_MAX_RSP     512
#define ALICE_CMD_TIMEOUT 3000

/* resolved at load time */
static void *(*p_getbuf)(void *priv, u16 len, u8 ctx_id, int rsp,
			 u8 cmd_id, gfp_t flags);
static int (*p_sendrecv)(void *priv, void *msg, unsigned int timeout,
			 u8 *rbuf, u16 *rlen);
static int (*p_syncver)(void *priv);
static void *(*p_getstats)(void);
static int (*p_sendtoic)(void *priv, void *msg);

/* first four members of struct sprdwl_msg_buf, offset-stable */
struct alice_msg_prefix {
	struct list_head list;
	struct sk_buff *skb;
	void *data;
	void *tran_data;
};

static struct dentry *alice_dir;
static char *param_data = "";
static int param_cmd = -1;
static int param_ctx;

/*
 * sprdwl_ng.ko layout, derived from its own symbol table:
 *   .text @ link 0x0        (sprdwl_get_stats is the first function)
 *   .bss  @ link 0x40f80, g_sprdwl_priv @ +8
 * Runtime module base comes from kallsyms, so this delta stays valid.
 */
#define SPRDWL_PRIV_SLOT_DELTA 0x41088UL

module_param_named(cmd, param_cmd, int, 0444);
module_param_named(ctx, param_ctx, int, 0444);
module_param_named(data, param_data, charp, 0444);
MODULE_PARM_DESC(cmd, "Wi-Fi firmware command id to send at load time (-1 = none)");
MODULE_PARM_DESC(ctx, "ctx_id for the load-time command");
MODULE_PARM_DESC(data, "hex payload for the load-time command");

static void *alice_get_priv(void)
{
	unsigned long slot = (unsigned long)p_getstats + SPRDWL_PRIV_SLOT_DELTA;
	void *priv = NULL, *ops = NULL, *intf = NULL;
	u32 hw_off = 0;

	if (!p_getstats)
		return NULL;

	if (probe_kernel_read(&priv, (void *)slot, sizeof(priv)) ||
	    !priv || ((unsigned long)priv >> 48) != 0xffff) {
		pr_err("alice: cannot read g_sprdwl_priv at %px\n", (void *)slot);
		return NULL;
	}

	if (probe_kernel_read(&ops, (char *)priv + 200, sizeof(ops)) ||
	    probe_kernel_read(&intf, (char *)priv + 184, sizeof(intf)) ||
	    probe_kernel_read(&hw_off, (char *)priv + 192, sizeof(hw_off))) {
		pr_err("alice: priv %px unreadable\n", priv);
		return NULL;
	}

	/* sanity: ops points into the driver module, hw_offset is small */
	if (!ops || ((unsigned long)ops >> 32) != 0xffffff80UL || hw_off > 64) {
		pr_err("alice: priv %px failed validation (ops=%px hw_offset=%u)\n",
		       priv, ops, hw_off);
		return NULL;
	}

	return priv;
}

static int alice_hex2bin(const char *hex, u8 *out, int max)
{
	int n = 0;

	while (hex && *hex) {
		int hi, lo;

		while (*hex == ' ' || *hex == ':')
			hex++;
		if (!*hex)
			break;
		hi = hex_to_bin(*hex++);
		if (hi < 0)
			return -EINVAL;
		lo = hex_to_bin(*hex++);
		if (lo < 0)
			return -EINVAL;
		if (n >= max)
			return -E2BIG;
		out[n++] = (hi << 4) | lo;
	}
	return n;
}

static int alice_send_cmd(u8 cmd_id, u8 ctx_id, const u8 *payload, int plen,
			  int raw)
{
	void *priv;
	void *msg;
	struct alice_msg_prefix *m;
	u8 rbuf[ALICE_MAX_RSP];
	u16 rlen = sizeof(rbuf);
	int ret;

	priv = alice_get_priv();
	if (!priv)
		return -ENODEV;

	pr_info("alice: sending cmd=%u ctx=%u plen=%d raw=%d\n",
		cmd_id, ctx_id, plen, raw);

	msg = p_getbuf(priv, plen, ctx_id, raw ? 0 : 1, cmd_id, GFP_KERNEL);
	if (!msg) {
		pr_err("alice: cmd %u: getbuf failed (cp_asserted?)\n", cmd_id);
		return -ENOMEM;
	}

	m = (struct alice_msg_prefix *)msg;
	if (plen && payload)
		memcpy(m->data, payload, plen);

	if (raw) {
		/*
		 * Bypass sprdwl_api_available_check(): the driver drops any
		 * command id missing from its version table, but the firmware
		 * itself supports more (e.g. SNIFFER_MODE_SWITCH = 62).
		 * sprdwl_cmd_send_to_ic() sends without that gate.
		 */
		ret = p_sendtoic(priv, msg);
		pr_info("alice: cmd %u raw -> ret=%d\n", cmd_id, ret);
		if (ret)
			kfree(m->tran_data);
		return ret;
	}

	memset(rbuf, 0, sizeof(rbuf));
	ret = p_sendrecv(priv, msg, ALICE_CMD_TIMEOUT, rbuf, &rlen);

	pr_info("alice: cmd %u -> ret=%d rlen=%u\n", cmd_id, ret, rlen);
	if (!ret && rlen)
		print_hex_dump(KERN_INFO, "alice-rsp: ", DUMP_PREFIX_OFFSET,
			       16, 1, rbuf, min_t(u16, rlen, sizeof(rbuf)), true);

	return ret;
}

static ssize_t alice_priv_show(struct file *f, char __user *ubuf, size_t n,
			       loff_t *ppos)
{
	char buf[160];
	void *priv;
	int len;

	priv = alice_get_priv();
	if (!priv) {
		len = scnprintf(buf, sizeof(buf), "driver priv not available\n");
	} else {
		len = scnprintf(buf, sizeof(buf),
				"priv=%p hw_priv=%px hw_offset=%u ops=%px\n",
				priv,
				*(void **)((char *)priv + 184),
				*(unsigned int *)((char *)priv + 192),
				*(void **)((char *)priv + 200));
	}

	return simple_read_from_buffer(ubuf, n, ppos, buf, len);
}

static const struct file_operations alice_priv_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = alice_priv_show,
	.llseek = default_llseek,
};

static ssize_t alice_syncver_write(struct file *f, const char __user *ubuf,
				   size_t n, loff_t *ppos)
{
	void *priv;
	int ret;

	priv = alice_get_priv();
	if (!priv)
		return -ENODEV;

	ret = p_syncver(priv);
	pr_info("alice: sync_version -> %d\n", ret);
	return ret < 0 ? ret : n;
}

static const struct file_operations alice_syncver_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = alice_syncver_write,
	.llseek = default_llseek,
};

static ssize_t alice_fwcmd_write(struct file *f, const char __user *ubuf,
				 size_t n, loff_t *ppos)
{
	char *buf, *p, *tok;
	u8 payload[ALICE_MAX_PAYLOAD];
	int plen = 0, cmd = -1, ctx = 0, raw = 0;
	int ret;

	if (n == 0 || n > 1024)
		return -EINVAL;

	buf = kmalloc(n + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, ubuf, n)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[n] = '\0';

	p = strim(buf);
	while ((tok = strsep(&p, " \t\n"))) {
		if (!*tok)
			continue;
		if (!strncmp(tok, "cmd=", 4)) {
			if (kstrtoint(tok + 4, 0, &cmd))
				goto bad;
		} else if (!strncmp(tok, "ctx=", 4)) {
			if (kstrtoint(tok + 4, 0, &ctx))
				goto bad;
		} else if (!strncmp(tok, "raw=", 4)) {
			if (kstrtoint(tok + 4, 0, &raw))
				goto bad;
		} else if (!strncmp(tok, "data=", 5)) {
			plen = alice_hex2bin(tok + 5, payload,
					     sizeof(payload));
			if (plen < 0) {
				ret = plen;
				kfree(buf);
				return ret;
			}
		} else {
			goto bad;
		}
	}

	if (cmd < 0 || cmd > 255 || ctx < 0 || ctx > 7) {
		kfree(buf);
		return -EINVAL;
	}

	ret = alice_send_cmd(cmd, ctx, payload, plen, raw);
	kfree(buf);

	return ret < 0 ? ret : n;

bad:
	kfree(buf);
	return -EINVAL;
}

static const struct file_operations alice_fwcmd_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = alice_fwcmd_write,
	.llseek = default_llseek,
};

static int __init alice_init(void)
{
	u8 payload[ALICE_MAX_PAYLOAD];
	int plen = 0;

	p_getbuf = (void *)kallsyms_lookup_name("__sprdwl_cmd_getbuf");
	p_sendrecv = (void *)kallsyms_lookup_name("sprdwl_cmd_send_recv");
	p_syncver = (void *)kallsyms_lookup_name("sprdwl_sync_version");
	p_getstats = (void *)kallsyms_lookup_name("sprdwl_get_stats");
	p_sendtoic = (void *)kallsyms_lookup_name("sprdwl_cmd_send_to_ic");

	if (!p_getbuf || !p_sendrecv || !p_syncver || !p_getstats || !p_sendtoic) {
		pr_err("alice: symbol lookup failed (getbuf=%px sendrecv=%px syncver=%px getstats=%px sendtoic=%px)\n",
		       p_getbuf, p_sendrecv, p_syncver, p_getstats, p_sendtoic);
		return -ENOENT;
	}

	alice_dir = debugfs_create_dir("alice", NULL);
	if (!alice_dir)
		return -ENOMEM;

	debugfs_create_file("priv", 0444, alice_dir, NULL, &alice_priv_fops);
	debugfs_create_file("syncver", 0200, alice_dir, NULL,
			    &alice_syncver_fops);
	debugfs_create_file("fwcmd", 0200, alice_dir, NULL, &alice_fwcmd_fops);

	pr_info("alice: tools loaded, getbuf=%ps sendrecv=%ps syncver=%ps\n",
		p_getbuf, p_sendrecv, p_syncver);
	pr_info("alice: raw RX dump: echo debug_level=5 > /sys/kernel/debug/sprdwl_debug/log_level\n");

	if (param_cmd >= 0) {
		plen = alice_hex2bin(param_data, payload, sizeof(payload));
		if (plen < 0) {
			pr_err("alice: bad data=%s\n", param_data);
			plen = 0;
		}
		alice_send_cmd((u8)param_cmd, (u8)param_ctx, payload, plen, 0);
	}

	return 0;
}

static void __exit alice_exit(void)
{
	debugfs_remove_recursive(alice_dir);
	pr_info("alice: tools unloaded\n");
}

module_init(alice_init);
module_exit(alice_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Alice Station Wi-Fi firmware instrument");
MODULE_AUTHOR("alice");
