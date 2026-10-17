// SPDX-License-Identifier: GPL-2.0
/*
 * kstb-v3d-power.c - the V3D GPU's power island, as a clock gate
 *
 * After BOLT the GPU's island is off and every V3D register aborts. The
 * stock driver switches it through 0xf041d020 (nexus.ko
 * BVC5_P_HardwareBPCMPowerUp/PowerDown, docs/hardware/gpu.md "Power island"):
 * write 0x1d00 and wait for (v & 0x74000000) == 0x34000000 to power up,
 * write 0xb00 and wait for (v & 0x72000000) == 0x42000000 to power down.
 * Both took about 1.6 ms.
 *
 * Mainline's v3d driver has no power-domain hook for this chip, but it
 * prepares and enables its optional clock before its first register access
 * and disables it at remove. So the island is offered as that clock: the
 * GPU's node points its "clocks" here, and v3d waits (probe deferral) until
 * this module is loaded.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define PWR_UP		0x1d00
#define PWR_DOWN	0x0b00
#define UP_MASK		0x74000000
#define UP_VAL		0x34000000
#define DOWN_MASK	0x72000000
#define DOWN_VAL	0x42000000
#define PWR_ON_BIT	BIT(26)		/* Nexus powers down only if this is set */

struct kstb_v3d_power {
	struct clk_hw hw;
	void __iomem *reg;
};

#define to_pwr(_hw) container_of(_hw, struct kstb_v3d_power, hw)

static int kstb_v3d_power_prepare(struct clk_hw *hw)
{
	struct kstb_v3d_power *p = to_pwr(hw);
	u32 v;
	int ret;

	writel(PWR_UP, p->reg);
	ret = readl_poll_timeout(p->reg, v, (v & UP_MASK) == UP_VAL, 100, 100000);
	if (ret)
		pr_err("kstb-v3d-power: no power-up, %08x\n", v);
	return ret;
}

static void kstb_v3d_power_unprepare(struct clk_hw *hw)
{
	struct kstb_v3d_power *p = to_pwr(hw);
	u32 v;

	if (!(readl(p->reg) & PWR_ON_BIT))
		return;
	writel(PWR_DOWN, p->reg);
	if (readl_poll_timeout(p->reg, v, (v & DOWN_MASK) == DOWN_VAL, 100, 100000))
		pr_err("kstb-v3d-power: no power-down, %08x\n", v);
}

static int kstb_v3d_power_is_prepared(struct clk_hw *hw)
{
	return (readl(to_pwr(hw)->reg) & UP_MASK) == UP_VAL;
}

static const struct clk_ops kstb_v3d_power_ops = {
	.prepare = kstb_v3d_power_prepare,
	.unprepare = kstb_v3d_power_unprepare,
	.is_prepared = kstb_v3d_power_is_prepared,
};

static int kstb_v3d_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_init_data init = {
		.name = "v3d-power",
		.ops = &kstb_v3d_power_ops,
	};
	struct kstb_v3d_power *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->reg))
		return PTR_ERR(p->reg);
	p->hw.init = &init;
	ret = devm_clk_hw_register(dev, &p->hw);
	if (ret)
		return ret;
	dev_info(dev, "island %s (%08x)\n",
		 kstb_v3d_power_is_prepared(&p->hw) ? "on" : "off", readl(p->reg));
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &p->hw);
}

static const struct of_device_id kstb_v3d_power_of_match[] = {
	{ .compatible = "kaon,kstb6077-v3d-power" },
	{ }
};
MODULE_DEVICE_TABLE(of, kstb_v3d_power_of_match);

static struct platform_driver kstb_v3d_power_driver = {
	.probe = kstb_v3d_power_probe,
	.driver = {
		.name = "kstb-v3d-power",
		.of_match_table = kstb_v3d_power_of_match,
	},
};
module_platform_driver(kstb_v3d_power_driver);

MODULE_DESCRIPTION("KSTB6077 (BCM7268) V3D power island");
MODULE_LICENSE("GPL");
