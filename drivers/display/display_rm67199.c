/*
 * Copyright 2025, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT raydium_rm67199

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(rm67199, CONFIG_DISPLAY_LOG_LEVEL);

/*
 * These commands are taken from NXP's MCUXpresso SDK.
 * Additional documentation is added where possible, but the
 * Manufacture command set pages are not described in the datasheet
 */
static const struct {
	uint8_t cmd;
	uint8_t param;
} rm67199_init_setting[] = {
	{.cmd = 0xFE, .param = 0xA0},
	{.cmd = 0x2B, .param = 0x18},
	{.cmd = 0xFE, .param = 0x70},
	{.cmd = 0x7D, .param = 0x05},
	{0x5D, 0x0A},
	{0x5A, 0x79},
	{0x5C, 0x00},
	{0x52, 0x00},
	{0xFE, 0xD0},
	{0x40, 0x02},
	{0x13, 0x40},
	{0xFE, 0x40},
	{0x05, 0x08},
	{0x06, 0x08},
	{0x08, 0x08},
	{0x09, 0x08},
	{0x0A, 0xCA},
	{0x0B, 0x88},
	{0x20, 0x93},
	{0x21, 0x93},
	{0x24, 0x02},
	{0x26, 0x02},
	{0x28, 0x05},
	{0x2A, 0x05},
	{0x74, 0x2F},
	{0x75, 0x1E},
	{0xAD, 0x00},
	{0xFE, 0x60},
	{0x00, 0xCC},
	{0x01, 0x00},
	{0x02, 0x04},
	{0x03, 0x00},
	{0x04, 0x00},
	{0x05, 0x07},
	{0x06, 0x00},
	{0x07, 0x88},
	{0x08, 0x00},
	{0x09, 0xCC},
	{0x0A, 0x00},
	{0x0B, 0x04},
	{0x0C, 0x00},
	{0x0D, 0x00},
	{0x0E, 0x05},
	{0x0F, 0x00},
	{0x10, 0x88},
	{0x11, 0x00},
	{0x12, 0xCC},
	{0x13, 0x0F},
	{0x14, 0xFF},
	{0x15, 0x04},
	{0x16, 0x00},
	{0x17, 0x06},
	{0x18, 0x00},
	{0x19, 0x96},
	{0x1A, 0x00},
	{0x24, 0xCC},
	{0x25, 0x00},
	{0x26, 0x02},
	{0x27, 0x00},
	{0x28, 0x00},
	{0x29, 0x06},
	{0x2A, 0x06},
	{0x2B, 0x82},
	{0x2D, 0x00},
	{0x2F, 0xCC},
	{0x30, 0x00},
	{0x31, 0x02},
	{0x32, 0x00},
	{0x33, 0x00},
	{0x34, 0x07},
	{0x35, 0x06},
	{0x36, 0x82},
	{0x37, 0x00},
	{0x38, 0xCC},
	{0x39, 0x00},
	{0x3A, 0x02},
	{0x3B, 0x00},
	{0x3D, 0x00},
	{0x3F, 0x07},
	{0x40, 0x00},
	{0x41, 0x88},
	{0x42, 0x00},
	{0x43, 0xCC},
	{0x44, 0x00},
	{0x45, 0x02},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x06},
	{0x49, 0x02},
	{0x4A, 0x8A},
	{0x4B, 0x00},
	{0x5F, 0xCA},
	{0x60, 0x01},
	{0x61, 0xE8},
	{0x62, 0x09},
	{0x63, 0x00},
	{0x64, 0x07},
	{0x65, 0x00},
	{0x66, 0x30},
	{0x67, 0x80},
	{0x9B, 0x03},
	{0xA9, 0x07},
	{0xAA, 0x06},
	{0xAB, 0x02},
	{0xAC, 0x10},
	{0xAD, 0x11},
	{0xAE, 0x05},
	{0xAF, 0x04},
	{0xB0, 0x10},
	{0xB1, 0x10},
	{0xB2, 0x10},
	{0xB3, 0x10},
	{0xB4, 0x10},
	{0xB5, 0x10},
	{0xB6, 0x10},
	{0xB7, 0x10},
	{0xB8, 0x10},
	{0xB9, 0x10},
	{0xBA, 0x04},
	{0xBB, 0x05},
	{0xBC, 0x00},
	{0xBD, 0x01},
	{0xBE, 0x0A},
	{0xBF, 0x10},
	{0xC0, 0x11},
	{0xFE, 0xA0},
	{0x22, 0x00},
};

struct rm67199_config {
	const struct device *mipi_dsi;
	uint8_t channel;
	uint8_t num_of_lanes;
	const struct gpio_dt_spec reset_gpio;
	const struct gpio_dt_spec bl_gpio;
};

struct rm67199_data {
	uint8_t pixel_format;
	uint8_t bytes_per_pixel;
	struct k_sem te_sem;
};

static int rm67199_init(const struct device *dev)
{
	const struct rm67199_config *config = dev->config;
	struct rm67199_data *data = dev->data;
	struct mipi_dsi_device mdev = {0};
	int ret;
	uint32_t i;
	uint8_t buf[2];

	LOG_INF("rm67199_init starting");

	/* Attach to MIPI DSI host */
	mdev.data_lanes = config->num_of_lanes;
	mdev.pixfmt = data->pixel_format;
	mdev.mode_flags = MIPI_DSI_MODE_VIDEO;

	ret = mipi_dsi_attach(config->mipi_dsi, config->channel, &mdev);
	if (ret < 0) {
		LOG_ERR("Could not attach to MIPI-DSI host");
		return ret;
	}

	if (config->reset_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Could not configure reset GPIO (%d)", ret);
			return ret;
		}

		/*
		 * Power to the display has been enabled via the regulator fixed api during
		 * regulator init. Per datasheet, we must wait at least 10ms before
		 * starting reset sequence after power on.
		 */
		k_sleep(K_MSEC(10));
		/* Start reset sequence */
		ret = gpio_pin_set_dt(&config->reset_gpio, 0);
		if (ret < 0) {
			LOG_ERR("Could not pull reset low (%d)", ret);
			return ret;
		}
		/* Per datasheet, reset low pulse width should be at least 10usec */
		k_sleep(K_USEC(10));
		ret = gpio_pin_set_dt(&config->reset_gpio, 1);
		if (ret < 0) {
			LOG_ERR("Could not pull reset high (%d)", ret);
			return ret;
		}
		/*
		 * It is necessary to wait at least 120msec after releasing reset,
		 * before sending additional commands. This delay can be 5msec
		 * if we are certain the display module is in SLEEP IN state,
		 * but this is not guaranteed (for example, with a warm reset)
		 */
		k_sleep(K_MSEC(150));
	}

	/* Now, write initialization settings for display */
	for (i = 0; i < ARRAY_SIZE(rm67199_init_setting); i++) {
		buf[0] = rm67199_init_setting[i].cmd;
		buf[1] = rm67199_init_setting[i].param;
		ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, buf, 2);
		if (ret < 0) {
			return ret;
		}
	}

	/* Change to send user command. */
	buf[0] = 0xFE;
	buf[1] = 0x00;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, buf, 2);
	if (ret < 0) {
		return ret;
	}

	/* Set DSI mode */
	buf[0] = 0xC2;
	buf[1] = 0x03;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, buf, 2);
	if (ret < 0) {
		return ret;
	}

	/* Set pixel format */
	if (data->pixel_format == MIPI_DSI_PIXFMT_RGB888) {
		buf[1] = MIPI_DCS_PIXEL_FORMAT_24BIT;
		data->bytes_per_pixel = 3;
	} else if (data->pixel_format == MIPI_DSI_PIXFMT_RGB565) {
		buf[1] = MIPI_DCS_PIXEL_FORMAT_16BIT;
		data->bytes_per_pixel = 2;
	} else {
		/* Unsupported pixel format */
		LOG_ERR("Pixel format not supported");
		return -ENOTSUP;
	}
	buf[0] = MIPI_DCS_SET_PIXEL_FORMAT;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, buf, 2);
	if (ret < 0) {
		return ret;
	}

	/* Brightness. */
	buf[0] = MIPI_DCS_SET_DISPLAY_BRIGHTNESS;
	buf[1] = 0xFF;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, buf, 2);
	if (ret < 0) {
		return ret;
	}

	/* Delay 50 ms before exiting sleep mode */
	k_sleep(K_MSEC(50));
	buf[0] = MIPI_DCS_EXIT_SLEEP_MODE;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, &buf[0], 1);
	if (ret < 0) {
		return ret;
	}
	/*
	 * We must wait 5 ms after exiting sleep mode before sending additional
	 * commands. If we intend to enter sleep mode, we must delay
	 * 120 ms before sending that command. To be safe, delay 150ms
	 */
	k_sleep(K_MSEC(150));

	/* Setup backlight */
	if (config->bl_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->bl_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Could not configure bl GPIO (%d)", ret);
			return ret;
		}
	}

	/* Now, enable display */
	buf[0] = MIPI_DCS_SET_DISPLAY_ON;
	ret = mipi_dsi_generic_write(config->mipi_dsi, config->channel, &buf[0], 1);
	if (ret < 0) {
		LOG_ERR("%s failed", __func__);
	} else {
		LOG_INF("%s succeeded", __func__);
	}

	return ret;
}

static int rm67199_write(const struct device *dev, const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(desc);
	ARG_UNUSED(buf);

	LOG_WRN("%s is not implemented", __func__);

	return -ENOTSUP;
}

static void rm67199_get_capabilities(const struct device *dev,
				     struct display_capabilities *capabilities)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(capabilities);

	LOG_WRN("%s is not implemented", __func__);
}

static int rm67199_blanking_off(const struct device *dev)
{
	const struct rm67199_config *config = dev->config;

	if (config->bl_gpio.port != NULL) {
		return gpio_pin_set_dt(&config->bl_gpio, 1);
	} else {
		return -ENOTSUP;
	}
}

static int rm67199_blanking_on(const struct device *dev)
{
	const struct rm67199_config *config = dev->config;

	if (config->bl_gpio.port != NULL) {
		return gpio_pin_set_dt(&config->bl_gpio, 0);
	} else {
		return -ENOTSUP;
	}
}

static int rm67199_set_pixel_format(const struct device *dev,
				    const enum display_pixel_format pixel_format)
{
	const struct rm67199_config *config = dev->config;
	struct rm67199_data *data = dev->data;
	uint8_t param;

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB_565:
		data->pixel_format = MIPI_DSI_PIXFMT_RGB565;
		param = MIPI_DCS_PIXEL_FORMAT_16BIT;
		data->bytes_per_pixel = 2;
		break;
	case PIXEL_FORMAT_RGB_888:
		data->pixel_format = MIPI_DSI_PIXFMT_RGB888;
		param = MIPI_DCS_PIXEL_FORMAT_24BIT;
		data->bytes_per_pixel = 3;
		break;
	default:
		/* Other display formats not implemented */
		return -ENOTSUP;
	}

	return mipi_dsi_dcs_write(config->mipi_dsi, config->channel, MIPI_DCS_SET_PIXEL_FORMAT,
				  &param, 1);
}

static int rm67199_set_orientation(const struct device *dev,
				   const enum display_orientation orientation)
{
	ARG_UNUSED(dev);

	if (orientation == DISPLAY_ORIENTATION_NORMAL) {
		return 0;
	}
	LOG_ERR("Changing display orientation not implemented");
	return -ENOTSUP;
}

static const struct display_driver_api rm67199_api = {
	.blanking_on = rm67199_blanking_on,
	.blanking_off = rm67199_blanking_off,
	.get_capabilities = rm67199_get_capabilities,
	.write = rm67199_write,
	.set_pixel_format = rm67199_set_pixel_format,
	.set_orientation = rm67199_set_orientation,
};

#define RM67199_PANEL(id)                                                                          \
	static const struct rm67199_config rm67199_config_##id = {                                 \
		.mipi_dsi = DEVICE_DT_GET(DT_INST_BUS(id)),                                        \
		.channel = DT_INST_REG_ADDR(id),                                                   \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(id, reset_gpios, {0}),                      \
		.bl_gpio = GPIO_DT_SPEC_INST_GET_OR(id, bl_gpios, {0}),                            \
		.num_of_lanes = DT_INST_PROP_BY_IDX(id, data_lanes, 0),                            \
	};                                                                                         \
	static struct rm67199_data rm67199_data_##id = {                                           \
		.pixel_format = DT_INST_PROP(id, pixel_format),                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(id, &rm67199_init, PM_DEVICE_DT_INST_GET(id), &rm67199_data_##id,    \
			      &rm67199_config_##id, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, \
			      &rm67199_api);

DT_INST_FOREACH_STATUS_OKAY(RM67199_PANEL)
