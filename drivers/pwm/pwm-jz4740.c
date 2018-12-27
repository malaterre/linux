/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define NUM_PWM 8

struct jz4740_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	struct regmap *map;
};

static inline struct jz4740_pwm_chip *to_jz4740(struct pwm_chip *chip)
{
	return container_of(chip, struct jz4740_pwm_chip, chip);
}

static int jz4740_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/*
	 * Timers 0 and 1 are used for system tasks, so they are unavailable
	 * for use as PWMs.
	 */
	if (pwm->hwpwm < 2)
		return -EBUSY;

	regmap_write(jz->map, TCU_REG_TSCR, BIT(pwm->hwpwm));

	return 0;
}

static void jz4740_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	regmap_write(jz->map, TCU_REG_TSCR, BIT(pwm->hwpwm));
}

static int jz4740_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Enable PWM output */
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_EN, TCU_TCSR_PWM_EN);

	/* Start counter */
	regmap_write(jz->map, TCU_REG_TESR, BIT(pwm->hwpwm));
	return 0;
}

static void jz4740_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Disable PWM output.
	 * In TCU2 mode (channel 1/2 on JZ4750+), this must be done before the
	 * counter is stopped, while in TCU1 mode the order does not matter.
	 */
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_EN, 0);

	/* Stop counter */
	regmap_write(jz->map, TCU_REG_TECR, BIT(pwm->hwpwm));
}

static int jz4740_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    struct pwm_state *state)
{
	struct jz4740_pwm_chip *jz4740 = to_jz4740(pwm->chip);
	unsigned long long tmp;
	unsigned long period, duty;
	unsigned int prescaler = 0;

	tmp = (unsigned long long)clk_get_rate(jz4740->clk) * state->period;
	do_div(tmp, 1000000000);
	period = tmp;

	while (period > 0xffff && prescaler < 6) {
		period >>= 2;
		++prescaler;
	}

	if (prescaler == 6)
		return -EINVAL;

	tmp = (unsigned long long)period * state->duty_cycle;
	do_div(tmp, state->period);
	duty = period - tmp;

	if (duty >= period)
		duty = period - 1;

	jz4740_pwm_disable(chip, pwm);

	/* Set abrupt shutdown */
	regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_SD, TCU_TCSR_PWM_SD);

	/* Set clock prescale */
	regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PRESCALE_MASK,
			   prescaler << TCU_TCSR_PRESCALE_LSB);

	/* Reset counter to 0 */
	regmap_write(jz4740->map, TCU_REG_TCNTc(pwm->hwpwm), 0);

	/* Set duty */
	regmap_write(jz4740->map, TCU_REG_TDHRc(pwm->hwpwm), duty);

	/* Set period */
	regmap_write(jz4740->map, TCU_REG_TDFRc(pwm->hwpwm), period);

	/* Set polarity */
	switch (state->polarity) {
	case PWM_POLARITY_NORMAL:
		regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
				   TCU_TCSR_PWM_INITL_HIGH, 0);
		break;
	case PWM_POLARITY_INVERSED:
		regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
				   TCU_TCSR_PWM_INITL_HIGH,
				   TCU_TCSR_PWM_INITL_HIGH);
		break;
	}

	if (state->enabled)
		jz4740_pwm_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops jz4740_pwm_ops = {
	.request = jz4740_pwm_request,
	.free = jz4740_pwm_free,
	.apply = jz4740_pwm_apply,
	.owner = THIS_MODULE,
};

static int jz4740_pwm_probe(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740;
	struct device *dev = &pdev->dev;

	jz4740 = devm_kzalloc(dev, sizeof(*jz4740), GFP_KERNEL);
	if (!jz4740)
		return -ENOMEM;

	jz4740->clk = devm_clk_get(&pdev->dev, "ext");
	if (IS_ERR(jz4740->clk))
		return PTR_ERR(jz4740->clk);

	jz4740->map = dev_get_regmap(dev->parent, NULL);
	if (!jz4740->map) {
		dev_err(dev, "regmap not found\n");
		return -EINVAL;
	}

	jz4740->chip.dev = dev;
	jz4740->chip.ops = &jz4740_pwm_ops;
	jz4740->chip.npwm = NUM_PWM;
	jz4740->chip.base = -1;
	jz4740->chip.of_xlate = of_pwm_xlate_with_flags;
	jz4740->chip.of_pwm_n_cells = 3;

	platform_set_drvdata(pdev, jz4740);

	return pwmchip_add(&jz4740->chip);
}

static int jz4740_pwm_remove(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740 = platform_get_drvdata(pdev);

	return pwmchip_remove(&jz4740->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id jz4740_pwm_dt_ids[] = {
	{ .compatible = "ingenic,jz4740-pwm", },
	{ .compatible = "ingenic,jz4770-pwm", },
	{ .compatible = "ingenic,jz4780-pwm", },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_pwm_dt_ids);
#endif

static struct platform_driver jz4740_pwm_driver = {
	.driver = {
		.name = "jz4740-pwm",
		.of_match_table = of_match_ptr(jz4740_pwm_dt_ids),
	},
	.probe = jz4740_pwm_probe,
	.remove = jz4740_pwm_remove,
};
module_platform_driver(jz4740_pwm_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 PWM driver");
MODULE_ALIAS("platform:jz4740-pwm");
MODULE_LICENSE("GPL");
