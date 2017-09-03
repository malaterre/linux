/*
 * jz4780-rng.c - Random Number Generator driver for the jz4780
 *
 * Copyright (c) 2017 PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <crypto/internal/rng.h>

#define REG_RNG_CTRL	0xD8
#define REG_RNG_DATA	0xDC

/* Context for crypto */
struct jz4780_rng_ctx {
	struct jz4780_rng *rng;
};

/* Device associated memory */
struct jz4780_rng {
	struct device *dev;
	struct regmap *regmap;
};

static struct jz4780_rng *jz4780_rng;

static int jz4780_rng_readl(struct jz4780_rng *rng, u32 offset)
{
	u32 val = 0;
	int ret;

	ret = regmap_read(rng->regmap, offset, &val);
	if (!ret)
		return val;

	return ret;
}

static int jz4780_rng_writel(struct jz4780_rng *rng, u32 val, u32 offset)
{
	return regmap_write(rng->regmap, offset, val);
}

static int jz4780_rng_generate(struct crypto_rng *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int dlen)
{
	struct jz4780_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct jz4780_rng *rng = ctx->rng;
	u32 data;

	/*
	 * A delay is required so that the current RNG data is not bit shifted
	 * version of previous RNG data which could happen if random data is
	 * read continuously from this device.
	 */
	jz4780_rng_writel(rng, 1, REG_RNG_CTRL);
	while (dlen >= 4) {
		data = jz4780_rng_readl(rng, REG_RNG_DATA);
		memcpy((void *)dst, (void *)&data, 4);
		dlen -= 4;
		dst += 4;
		udelay(20);
	};

	if (dlen > 0) {
		data = jz4780_rng_readl(rng, REG_RNG_DATA);
		memcpy((void *)dst, (void *)&data, dlen);
	}
	jz4780_rng_writel(rng, 0, REG_RNG_CTRL);

	return 0;
}

static int jz4780_rng_kcapi_init(struct crypto_tfm *tfm)
{
	struct jz4780_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rng = jz4780_rng;

	return 0;
}

static struct rng_alg jz4780_rng_alg = {
	.generate		= jz4780_rng_generate,
	.base			= {
		.cra_name		= "stdrng",
		.cra_driver_name	= "jz4780_rng",
		.cra_priority		= 100,
		.cra_ctxsize		= sizeof(struct jz4780_rng_ctx),
		.cra_module		= THIS_MODULE,
		.cra_init		= jz4780_rng_kcapi_init,
	}
};

static int jz4780_rng_probe(struct platform_device *pdev)
{
	struct jz4780_rng *rng;
	struct resource *res;
	int ret;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rng->regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(rng->regmap))
		return PTR_ERR(rng->regmap);

	jz4780_rng = rng;

	ret = crypto_register_rng(&jz4780_rng_alg);
	if (ret) {
		dev_err(&pdev->dev,
			"Couldn't register rng crypto alg: %d\n", ret);
		jz4780_rng = NULL;
	}

	return ret;
}

static int jz4780_rng_remove(struct platform_device *pdev)
{
	crypto_unregister_rng(&jz4780_rng_alg);

	jz4780_rng = NULL;

	return 0;
}

static const struct of_device_id jz4780_rng_dt_match[] = {
	{
		.compatible = "ingenic,jz4780-rng",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, jz4780_rng_dt_match);

static struct platform_driver jz4780_rng_driver = {
	.driver		= {
		.name	= "jz4780-rng",
		.of_match_table = jz4780_rng_dt_match,
	},
	.probe		= jz4780_rng_probe,
	.remove		= jz4780_rng_remove,
};

module_platform_driver(jz4780_rng_driver);

MODULE_DESCRIPTION("Ingenic JZ4780 H/W Pseudo Random Number Generator driver");
MODULE_AUTHOR("PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>");
MODULE_LICENSE("GPL");
