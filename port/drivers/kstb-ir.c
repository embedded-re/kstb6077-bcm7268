// SPDX-License-Identifier: GPL-2.0
/*
 * kstb-ir.c - the BCM7268's IR receiver (the "KBD" block) as an rc-core device
 *
 * Channel kbd1 decodes NEC in hardware once it has Nexus's 27 timing
 * parameters (hardware/ir.md). One interrupt per frame, through the
 * AON L2 bit 0: STATUS bit 0 = a code is in DATA0, bit 6 = first frame of a
 * press, otherwise a repeat. Writing STATUS back with bit 0 clear drops the
 * interrupt.
 *
 * The Kaon remote sends extended NEC with address 0x20 0x08, so the
 * scancodes are 0x2008cc (rc-core's NECX form); the key map below is the
 * stock one (kaon_301rcu_6077.ikm), named "rc-kaon-kstb6077".
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <media/rc-core.h>
#include <media/rc-map.h>

#define KBD_STATUS	0x00
#define KBD_FILTER1	0x08
#define KBD_DATA0	0x10
#define KBD_CMD		0x14
#define KBD_CIR_ADDR	0x18
#define KBD_CIR_DATA	0x1c

#define STATUS_READY	BIT(0)
#define STATUS_FIRST	BIT(6)
#define CMD_CIR		BIT(4)
#define CMD_INT		BIT(5)

#define KSTB_IR_MAP	"rc-kaon-kstb6077"

/* NEC timing parameters as BKIR_P_ConfigCir packs necParam; 24 isn't written */
static const u16 nec_param[28] = {
	0x0ff, 0x000, 0x012, 0x384, 0x384, 0x1c2, 0x0e1, 0x000, 0x000, 0x000,
	0x000, 0x80f, 0xc32, 0x000, 0x071, 0x071, 0x10d, 0x032, 0x830, 0x031,
	0x016, 0x006, 0x258, 0x118, 0x000, 0x000, 0x000, 0x000,
};

struct kstb_ir {
	void __iomem *base;
	struct rc_dev *rc;
};

static irqreturn_t kstb_ir_irq(int irq, void *data)
{
	struct kstb_ir *ir = data;
	u32 status = readl(ir->base + KBD_STATUS);
	enum rc_proto proto;
	u32 code, scancode;

	if (!(status & STATUS_READY))
		return IRQ_NONE;
	code = readl(ir->base + KBD_DATA0);
	writel(status & ~STATUS_READY, ir->base + KBD_STATUS);

	if (!(status & STATUS_FIRST)) {
		rc_repeat(ir->rc);
		return IRQ_HANDLED;
	}
	scancode = ir_nec_bytes_to_scancode(code, code >> 8, code >> 16, code >> 24, &proto);
	dev_dbg(&ir->rc->dev, "status %02x code %08x\n", status, code);
	rc_keydown(ir->rc, proto, scancode, 0);
	return IRQ_HANDLED;
}

/* Nexus's order: STATUS, interrupt enable, CIR on, FILTER1, the parameters */
static void kstb_ir_start(struct kstb_ir *ir)
{
	int i;

	writel(0, ir->base + KBD_STATUS);
	writel(readl(ir->base + KBD_CMD) | CMD_INT, ir->base + KBD_CMD);
	writel(readl(ir->base + KBD_CMD) | CMD_CIR, ir->base + KBD_CMD);
	writel(0, ir->base + KBD_FILTER1);
	for (i = 0; i < ARRAY_SIZE(nec_param); i++) {
		if (i == 24)
			continue;
		writel(i, ir->base + KBD_CIR_ADDR);
		writel(nec_param[i], ir->base + KBD_CIR_DATA);
	}
}

static void kstb_ir_stop(struct kstb_ir *ir)
{
	writel(readl(ir->base + KBD_CMD) & ~(CMD_INT | CMD_CIR), ir->base + KBD_CMD);
	writel(0, ir->base + KBD_STATUS);
}

static int kstb_ir_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kstb_ir *ir;
	int irq, ret;

	ir = devm_kzalloc(dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;
	ir->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ir->base))
		return PTR_ERR(ir->base);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ir->rc = devm_rc_allocate_device(dev, RC_DRIVER_SCANCODE);
	if (!ir->rc)
		return -ENOMEM;
	ir->rc->priv = ir;
	ir->rc->device_name = "KSTB6077 IR receiver";
	ir->rc->input_phys = "kstb-ir/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	ir->rc->driver_name = "kstb-ir";
	ir->rc->map_name = KSTB_IR_MAP;
	ir->rc->allowed_protocols = RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32;
	ir->rc->enabled_protocols = ir->rc->allowed_protocols;
	platform_set_drvdata(pdev, ir);

	kstb_ir_stop(ir);
	ret = devm_request_irq(dev, irq, kstb_ir_irq, 0, "kstb-ir", ir);
	if (ret)
		return dev_err_probe(dev, ret, "interrupt %d\n", irq);
	ret = devm_rc_register_device(dev, ir->rc);
	if (ret)
		return dev_err_probe(dev, ret, "rc device\n");
	kstb_ir_start(ir);
	return 0;
}

static void kstb_ir_remove(struct platform_device *pdev)
{
	kstb_ir_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id kstb_ir_of_match[] = {
	{ .compatible = "brcm,bcm7268-kbd" },
	{ }
};
MODULE_DEVICE_TABLE(of, kstb_ir_of_match);

static struct platform_driver kstb_ir_driver = {
	.probe = kstb_ir_probe,
	.remove = kstb_ir_remove,
	.driver = {
		.name = "kstb-ir",
		.of_match_table = kstb_ir_of_match,
	},
};

/* the Kaon remote (stock key map kaon_301rcu_6077, custom code 0x0820) */
static struct rc_map_table kaon_keys[] = {
	{ 0x200800, KEY_POWER },
	{ 0x200802, KEY_VOLUMEUP },
	{ 0x200803, KEY_VOLUMEDOWN },
	{ 0x200804, KEY_MUTE },
	{ 0x200805, KEY_TV },
	{ 0x200806, KEY_CHANNELUP },
	{ 0x200807, KEY_CHANNELDOWN },
	{ 0x20080b, KEY_SOUND },
	{ 0x20080c, KEY_EPG },
	{ 0x20080d, KEY_TUNER },
	{ 0x20080e, KEY_SEARCH },
	{ 0x20080f, KEY_SUBTITLE },
	{ 0x200810, KEY_BACK },
	{ 0x200811, KEY_MENU },
	{ 0x200812, KEY_UP },
	{ 0x200813, KEY_DOWN },
	{ 0x200814, KEY_LEFT },
	{ 0x200815, KEY_OK },
	{ 0x200816, KEY_RIGHT },
	{ 0x200818, KEY_RED },
	{ 0x200819, KEY_GREEN },
	{ 0x20081a, KEY_YELLOW },
	{ 0x20081b, KEY_BLUE },
	{ 0x20081c, KEY_1 },
	{ 0x20081d, KEY_2 },
	{ 0x20081e, KEY_3 },
	{ 0x20081f, KEY_SLEEP },
	{ 0x200840, KEY_4 },
	{ 0x200841, KEY_5 },
	{ 0x200842, KEY_6 },
	{ 0x200843, KEY_PLAYPAUSE },
	{ 0x200844, KEY_7 },
	{ 0x200845, KEY_8 },
	{ 0x200846, KEY_9 },
	{ 0x200847, KEY_0 },
	{ 0x200848, KEY_PREVIOUSSONG },
	{ 0x200849, KEY_REWIND },
	{ 0x20084a, KEY_FASTFORWARD },
	{ 0x20084b, KEY_NEXTSONG },
	{ 0x20084c, KEY_RECORD },
	{ 0x20084d, KEY_STOP },
	{ 0x20084e, KEY_PLAY },
	{ 0x200850, KEY_INFO },
	{ 0x200857, KEY_LAST },
	{ 0x20085a, KEY_HOMEPAGE },
};

static struct rc_map_list kaon_map = {
	.map = {
		.scan = kaon_keys,
		.size = ARRAY_SIZE(kaon_keys),
		.rc_proto = RC_PROTO_NECX,
		.name = KSTB_IR_MAP,
	},
};

static int __init kstb_ir_init(void)
{
	int ret = rc_map_register(&kaon_map);

	if (ret)
		return ret;
	ret = platform_driver_register(&kstb_ir_driver);
	if (ret)
		rc_map_unregister(&kaon_map);
	return ret;
}
module_init(kstb_ir_init);

static void __exit kstb_ir_exit(void)
{
	platform_driver_unregister(&kstb_ir_driver);
	rc_map_unregister(&kaon_map);
}
module_exit(kstb_ir_exit);

MODULE_DESCRIPTION("KSTB6077 (BCM7268) IR receiver, kbd1, NEC");
MODULE_LICENSE("GPL");
