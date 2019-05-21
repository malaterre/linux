// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU MFD driver
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/mfd/ingenic-tcu.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct ingenic_soc_info {
	unsigned int num_channels;
};

static struct regmap *tcu_regmap __initdata;

static const struct regmap_config ingenic_tcu_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = TCU_REG_OST_CNTHBUF,
};

static const struct ingenic_soc_info jz4740_soc_info = {
	.num_channels = 8,
};

static const struct ingenic_soc_info jz4725b_soc_info = {
	.num_channels = 6,
};

static const struct of_device_id ingenic_tcu_of_match[] = {
	{ .compatible = "ingenic,jz4740-tcu", .data = &jz4740_soc_info, },
	{ .compatible = "ingenic,jz4725b-tcu", .data = &jz4725b_soc_info, },
	{ .compatible = "ingenic,jz4770-tcu", .data = &jz4740_soc_info, },
	{ }
};

static struct regmap * __init ingenic_tcu_create_regmap(struct device_node *np)
{
	struct resource res;
	void __iomem *base;
	struct regmap *map;

	if (!of_match_node(ingenic_tcu_of_match, np))
		return ERR_PTR(-EINVAL);

	base = of_io_request_and_map(np, 0, "TCU");
	if (IS_ERR(base))
		return ERR_PTR(PTR_ERR(base));

	map = regmap_init_mmio(NULL, base, &ingenic_tcu_regmap_config);
	if (IS_ERR(map))
		goto err_iounmap;

	return map;

err_iounmap:
	iounmap(base);
	of_address_to_resource(np, 0, &res);
	release_mem_region(res.start, resource_size(&res));

	return map;
}

static int __init ingenic_tcu_probe(struct platform_device *pdev)
{
	struct regmap *map = ingenic_tcu_get_regmap(pdev->dev.of_node);

	platform_set_drvdata(pdev, map);

	regmap_attach_dev(&pdev->dev, map, &ingenic_tcu_regmap_config);

	return devm_of_platform_populate(&pdev->dev);
}

static struct platform_driver ingenic_tcu_driver = {
	.driver = {
		.name = "ingenic-tcu",
		.of_match_table = ingenic_tcu_of_match,
	},
};

static int __init ingenic_tcu_platform_init(void)
{
	return platform_driver_probe(&ingenic_tcu_driver,
				     ingenic_tcu_probe);
}
subsys_initcall(ingenic_tcu_platform_init);

struct regmap * __init ingenic_tcu_get_regmap(struct device_node *np)
{
	if (!tcu_regmap)
		tcu_regmap = ingenic_tcu_create_regmap(np);

	return tcu_regmap;
}

bool ingenic_tcu_pwm_can_use_chn(struct device *dev, unsigned int channel)
{
	const struct ingenic_soc_info *soc = device_get_match_data(dev->parent);

	/* Enable all TCU channels for PWM use by default except channels 0/1 */
	u32 pwm_channels_mask = GENMASK(soc->num_channels - 1, 2);

	device_property_read_u32(dev->parent, "ingenic,pwm-channels-mask",
				 &pwm_channels_mask);

	return !!(pwm_channels_mask & BIT(channel));
}
EXPORT_SYMBOL_GPL(ingenic_tcu_pwm_can_use_chn);
