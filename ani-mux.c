/*
 * Copyright (c) 2023  Data Respons Solutions AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/mod_devicetable.h>

struct ani_mux {
	struct gpio_desc *gpiod;
	struct iio_channel *parent;
	int last_channel;
	u32 delay_us;
};

static int iio_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct ani_mux *priv = dev_get_drvdata(indio_dev->dev.parent);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		/* mux */
		if (priv->last_channel != chan->channel) {
			gpiod_set_value_cansleep(priv->gpiod, chan->channel);
			fsleep(priv->delay_us);
			priv->last_channel = chan->channel;
		}
		return iio_read_channel_raw(priv->parent, val);
	case IIO_CHAN_INFO_SCALE:
		return iio_read_channel_scale(priv->parent, val, val2);
	}
	return -EINVAL;
}

static const struct iio_chan_spec ani_mux_iio_channels[] = {
	{
		.channel = 0,
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.channel = 1,
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info ani_mux_iio_info = {
	.read_raw = iio_read_raw,
};

static int ani_mux_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = NULL;
	struct ani_mux *priv = NULL;
	struct device *dev = &pdev->dev;
	int r = 0;

	priv = devm_kzalloc(dev, sizeof(struct ani_mux), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpiod = devm_gpiod_get(dev, "mux", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod),
				"failed to get mux-gpios\n");

	priv->parent = devm_iio_channel_get(dev, "parent");
	if (IS_ERR(priv->parent))
		return dev_err_probe(dev, PTR_ERR(priv->parent),
				"failed to get parent channel\n");

	/* Set if provided */
	device_property_read_u32(dev, "settle-time-us", &priv->delay_us);
	dev_dbg(dev, "settle-time-us: %u\n", priv->delay_us);

	indio_dev = devm_iio_device_alloc(dev, 0);
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->name = dev_name(dev);
	indio_dev->info = &ani_mux_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ani_mux_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(ani_mux_iio_channels);

	r = devm_iio_device_register(dev, indio_dev);
	if (r) {
		dev_err(dev, "failed registering to iio: %d\n", r);
		return r;
	}

	return 0;
}

static const struct of_device_id of_ani_mux_match[] = {
	{ .compatible = "drs,ani-mux" },
	{ /* sentinel */ }
};

static struct platform_driver ani_mux_driver = {
	.driver = {
		.name = "ani-mux",
		.of_match_table	= of_ani_mux_match,
	},
	.probe = ani_mux_probe,
};
module_platform_driver(ani_mux_driver);

MODULE_AUTHOR("Mikko Salomäki <ms@datarespons.se>");
MODULE_DESCRIPTION("Analog input mux");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
