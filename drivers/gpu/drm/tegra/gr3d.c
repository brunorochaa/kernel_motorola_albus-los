/*
 * Copyright (C) 2013 Avionic Design GmbH
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/tegra-powergate.h>

#include "drm.h"
#include "gem.h"
#include "gr3d.h"

struct gr3d {
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct clk *clk_secondary;
	struct clk *clk;

	DECLARE_BITMAP(addr_regs, GR3D_NUM_REGS);
};

static inline struct gr3d *to_gr3d(struct tegra_drm_client *client)
{
	return container_of(client, struct gr3d, client);
}

static int gr3d_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct tegra_drm *tegra = dev_get_drvdata(client->parent);
	unsigned long flags = HOST1X_SYNCPT_HAS_BASE;
	struct gr3d *gr3d = to_gr3d(drm);

	gr3d->channel = host1x_channel_request(client->dev);
	if (!gr3d->channel)
		return -ENOMEM;

	client->syncpts[0] = host1x_syncpt_request(client->dev, flags);
	if (!client->syncpts[0]) {
		host1x_channel_free(gr3d->channel);
		return -ENOMEM;
	}

	return tegra_drm_register_client(tegra, drm);
}

static int gr3d_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct tegra_drm *tegra = dev_get_drvdata(client->parent);
	struct gr3d *gr3d = to_gr3d(drm);
	int err;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	host1x_syncpt_free(client->syncpts[0]);
	host1x_channel_free(gr3d->channel);

	return 0;
}

static const struct host1x_client_ops gr3d_client_ops = {
	.init = gr3d_init,
	.exit = gr3d_exit,
};

static int gr3d_open_channel(struct tegra_drm_client *client,
			     struct tegra_drm_context *context)
{
	struct gr3d *gr3d = to_gr3d(client);

	context->channel = host1x_channel_get(gr3d->channel);
	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void gr3d_close_channel(struct tegra_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static int gr3d_is_addr_reg(struct device *dev, u32 class, u32 offset)
{
	struct gr3d *gr3d = dev_get_drvdata(dev);

	switch (class) {
	case HOST1X_CLASS_HOST1X:
		if (offset == 0x2b)
			return 1;

		break;

	case HOST1X_CLASS_GR3D:
		if (offset >= GR3D_NUM_REGS)
			break;

		if (test_bit(offset, gr3d->addr_regs))
			return 1;

		break;
	}

	return 0;
}

static const struct tegra_drm_client_ops gr3d_ops = {
	.open_channel = gr3d_open_channel,
	.close_channel = gr3d_close_channel,
	.is_addr_reg = gr3d_is_addr_reg,
	.submit = tegra_drm_submit,
};

static const struct of_device_id tegra_gr3d_match[] = {
	{ .compatible = "nvidia,tegra114-gr3d" },
	{ .compatible = "nvidia,tegra30-gr3d" },
	{ .compatible = "nvidia,tegra20-gr3d" },
	{ }
};

static const u32 gr3d_addr_regs[] = {
	GR3D_IDX_ATTRIBUTE( 0),
	GR3D_IDX_ATTRIBUTE( 1),
	GR3D_IDX_ATTRIBUTE( 2),
	GR3D_IDX_ATTRIBUTE( 3),
	GR3D_IDX_ATTRIBUTE( 4),
	GR3D_IDX_ATTRIBUTE( 5),
	GR3D_IDX_ATTRIBUTE( 6),
	GR3D_IDX_ATTRIBUTE( 7),
	GR3D_IDX_ATTRIBUTE( 8),
	GR3D_IDX_ATTRIBUTE( 9),
	GR3D_IDX_ATTRIBUTE(10),
	GR3D_IDX_ATTRIBUTE(11),
	GR3D_IDX_ATTRIBUTE(12),
	GR3D_IDX_ATTRIBUTE(13),
	GR3D_IDX_ATTRIBUTE(14),
	GR3D_IDX_ATTRIBUTE(15),
	GR3D_IDX_INDEX_BASE,
	GR3D_QR_ZTAG_ADDR,
	GR3D_QR_CTAG_ADDR,
	GR3D_QR_CZ_ADDR,
	GR3D_TEX_TEX_ADDR( 0),
	GR3D_TEX_TEX_ADDR( 1),
	GR3D_TEX_TEX_ADDR( 2),
	GR3D_TEX_TEX_ADDR( 3),
	GR3D_TEX_TEX_ADDR( 4),
	GR3D_TEX_TEX_ADDR( 5),
	GR3D_TEX_TEX_ADDR( 6),
	GR3D_TEX_TEX_ADDR( 7),
	GR3D_TEX_TEX_ADDR( 8),
	GR3D_TEX_TEX_ADDR( 9),
	GR3D_TEX_TEX_ADDR(10),
	GR3D_TEX_TEX_ADDR(11),
	GR3D_TEX_TEX_ADDR(12),
	GR3D_TEX_TEX_ADDR(13),
	GR3D_TEX_TEX_ADDR(14),
	GR3D_TEX_TEX_ADDR(15),
	GR3D_DW_MEMORY_OUTPUT_ADDRESS,
	GR3D_GLOBAL_SURFADDR( 0),
	GR3D_GLOBAL_SURFADDR( 1),
	GR3D_GLOBAL_SURFADDR( 2),
	GR3D_GLOBAL_SURFADDR( 3),
	GR3D_GLOBAL_SURFADDR( 4),
	GR3D_GLOBAL_SURFADDR( 5),
	GR3D_GLOBAL_SURFADDR( 6),
	GR3D_GLOBAL_SURFADDR( 7),
	GR3D_GLOBAL_SURFADDR( 8),
	GR3D_GLOBAL_SURFADDR( 9),
	GR3D_GLOBAL_SURFADDR(10),
	GR3D_GLOBAL_SURFADDR(11),
	GR3D_GLOBAL_SURFADDR(12),
	GR3D_GLOBAL_SURFADDR(13),
	GR3D_GLOBAL_SURFADDR(14),
	GR3D_GLOBAL_SURFADDR(15),
	GR3D_GLOBAL_SPILLSURFADDR,
	GR3D_GLOBAL_SURFOVERADDR( 0),
	GR3D_GLOBAL_SURFOVERADDR( 1),
	GR3D_GLOBAL_SURFOVERADDR( 2),
	GR3D_GLOBAL_SURFOVERADDR( 3),
	GR3D_GLOBAL_SURFOVERADDR( 4),
	GR3D_GLOBAL_SURFOVERADDR( 5),
	GR3D_GLOBAL_SURFOVERADDR( 6),
	GR3D_GLOBAL_SURFOVERADDR( 7),
	GR3D_GLOBAL_SURFOVERADDR( 8),
	GR3D_GLOBAL_SURFOVERADDR( 9),
	GR3D_GLOBAL_SURFOVERADDR(10),
	GR3D_GLOBAL_SURFOVERADDR(11),
	GR3D_GLOBAL_SURFOVERADDR(12),
	GR3D_GLOBAL_SURFOVERADDR(13),
	GR3D_GLOBAL_SURFOVERADDR(14),
	GR3D_GLOBAL_SURFOVERADDR(15),
	GR3D_GLOBAL_SAMP01SURFADDR( 0),
	GR3D_GLOBAL_SAMP01SURFADDR( 1),
	GR3D_GLOBAL_SAMP01SURFADDR( 2),
	GR3D_GLOBAL_SAMP01SURFADDR( 3),
	GR3D_GLOBAL_SAMP01SURFADDR( 4),
	GR3D_GLOBAL_SAMP01SURFADDR( 5),
	GR3D_GLOBAL_SAMP01SURFADDR( 6),
	GR3D_GLOBAL_SAMP01SURFADDR( 7),
	GR3D_GLOBAL_SAMP01SURFADDR( 8),
	GR3D_GLOBAL_SAMP01SURFADDR( 9),
	GR3D_GLOBAL_SAMP01SURFADDR(10),
	GR3D_GLOBAL_SAMP01SURFADDR(11),
	GR3D_GLOBAL_SAMP01SURFADDR(12),
	GR3D_GLOBAL_SAMP01SURFADDR(13),
	GR3D_GLOBAL_SAMP01SURFADDR(14),
	GR3D_GLOBAL_SAMP01SURFADDR(15),
	GR3D_GLOBAL_SAMP23SURFADDR( 0),
	GR3D_GLOBAL_SAMP23SURFADDR( 1),
	GR3D_GLOBAL_SAMP23SURFADDR( 2),
	GR3D_GLOBAL_SAMP23SURFADDR( 3),
	GR3D_GLOBAL_SAMP23SURFADDR( 4),
	GR3D_GLOBAL_SAMP23SURFADDR( 5),
	GR3D_GLOBAL_SAMP23SURFADDR( 6),
	GR3D_GLOBAL_SAMP23SURFADDR( 7),
	GR3D_GLOBAL_SAMP23SURFADDR( 8),
	GR3D_GLOBAL_SAMP23SURFADDR( 9),
	GR3D_GLOBAL_SAMP23SURFADDR(10),
	GR3D_GLOBAL_SAMP23SURFADDR(11),
	GR3D_GLOBAL_SAMP23SURFADDR(12),
	GR3D_GLOBAL_SAMP23SURFADDR(13),
	GR3D_GLOBAL_SAMP23SURFADDR(14),
	GR3D_GLOBAL_SAMP23SURFADDR(15),
};

static int gr3d_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct host1x_syncpt **syncpts;
	struct gr3d *gr3d;
	unsigned int i;
	int err;

	gr3d = devm_kzalloc(&pdev->dev, sizeof(*gr3d), GFP_KERNEL);
	if (!gr3d)
		return -ENOMEM;

	syncpts = devm_kzalloc(&pdev->dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	gr3d->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(gr3d->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(gr3d->clk);
	}

	if (of_device_is_compatible(np, "nvidia,tegra30-gr3d")) {
		gr3d->clk_secondary = devm_clk_get(&pdev->dev, "3d2");
		if (IS_ERR(gr3d->clk)) {
			dev_err(&pdev->dev, "cannot get secondary clock\n");
			return PTR_ERR(gr3d->clk);
		}
	}

	err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_3D, gr3d->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to power up 3D unit\n");
		return err;
	}

	if (gr3d->clk_secondary) {
		err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_3D1,
							gr3d->clk_secondary);
		if (err < 0) {
			dev_err(&pdev->dev,
				"failed to power up secondary 3D unit\n");
			return err;
		}
	}

	INIT_LIST_HEAD(&gr3d->client.base.list);
	gr3d->client.base.ops = &gr3d_client_ops;
	gr3d->client.base.dev = &pdev->dev;
	gr3d->client.base.class = HOST1X_CLASS_GR3D;
	gr3d->client.base.syncpts = syncpts;
	gr3d->client.base.num_syncpts = 1;

	INIT_LIST_HEAD(&gr3d->client.list);
	gr3d->client.ops = &gr3d_ops;

	err = host1x_client_register(&gr3d->client.base);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	/* initialize address register map */
	for (i = 0; i < ARRAY_SIZE(gr3d_addr_regs); i++)
		set_bit(gr3d_addr_regs[i], gr3d->addr_regs);

	platform_set_drvdata(pdev, gr3d);

	return 0;
}

static int gr3d_remove(struct platform_device *pdev)
{
	struct gr3d *gr3d = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&gr3d->client.base);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	if (gr3d->clk_secondary) {
		tegra_powergate_power_off(TEGRA_POWERGATE_3D1);
		clk_disable_unprepare(gr3d->clk_secondary);
	}

	tegra_powergate_power_off(TEGRA_POWERGATE_3D);
	clk_disable_unprepare(gr3d->clk);

	return 0;
}

struct platform_driver tegra_gr3d_driver = {
	.driver = {
		.name = "tegra-gr3d",
		.of_match_table = tegra_gr3d_match,
	},
	.probe = gr3d_probe,
	.remove = gr3d_remove,
};
