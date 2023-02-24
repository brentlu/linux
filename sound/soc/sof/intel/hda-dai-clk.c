// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.
//
// Authors: Brent Lu <brent.lu@intel.com>
//

/*
 * DAI clock layer to support explicit clock control in machine driver.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <sound/sof/dai.h>
#include "../sof-priv.h"
#include "shim.h"


struct hda_dai_clk_data {
	struct clk_hw hw;
	struct snd_sof_dev *sdev;
	char *clk_name;
	u32 clk_id;
	u32 dai_index;
};

#define to_clk_data(_hw) container_of(_hw, struct hda_dai_clk_data, hw)

static int hda_dai_clk_prepare(struct clk_hw *hw)
{
	struct hda_dai_clk_data *clk_data = to_clk_data(hw);
	struct snd_sof_dev *sdev = clk_data->sdev;

	dev_dbg(sdev->dev, "prepare %s, clk_id 0x%x\n", clk_data->clk_name,
		clk_data->clk_id);

	return snd_sof_send_pm_dai_clk_req_ipc(sdev, clk_data->clk_id, true,
					       SOF_DAI_INTEL_SSP,
					       clk_data->dai_index);
}

static void hda_dai_clk_unprepare(struct clk_hw *hw)
{
	struct hda_dai_clk_data *clk_data = to_clk_data(hw);
	struct snd_sof_dev *sdev = clk_data->sdev;

	dev_dbg(sdev->dev, "unprepare %s, clk_id 0x%x\n", clk_data->clk_name,
		clk_data->clk_id);

	snd_sof_send_pm_dai_clk_req_ipc(sdev, clk_data->clk_id, false,
					SOF_DAI_INTEL_SSP, clk_data->dai_index);
}

static const struct clk_ops hda_dai_clk_ops = {
	.prepare = hda_dai_clk_prepare,
	.unprepare = hda_dai_clk_unprepare,
};

int hda_register_dai_clocks(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	const struct sof_intel_dsp_desc *chip =
		(const struct sof_intel_dsp_desc *)desc->chip_info;
	struct device *dev = sdev->dev;
	struct hda_dai_clk_data *clk_data;
	struct clk_hw *hw;
	int i, j, ret;

	for (i = 0; i < chip->ssp_count; i++) {
		for (j = 0; j < SOF_DAI_INTEL_CLK_NUM; j++) {
			struct clk_init_data init = {};
			char prefix;

			switch (j) {
			case SOF_DAI_INTEL_CLK_MCLK:
				prefix = 'm';
				break;
			case SOF_DAI_INTEL_CLK_BCLK:
				prefix = 'b';
				break;
			default:
				dev_err(dev, "unknown clk %d\n", j);
				return -EINVAL;
			}

			clk_data = devm_kzalloc(dev, sizeof(*clk_data), GFP_KERNEL);
			if (!clk_data)
				return -ENOMEM;

			clk_data->sdev = sdev;
			clk_data->clk_name = devm_kasprintf(dev, GFP_KERNEL,
							    "ssp%d_%cclk", i,
							    prefix);
			if (!clk_data->clk_name)
				return -ENOMEM;
			clk_data->clk_id = j;
			clk_data->dai_index = i;

			init.name = clk_data->clk_name;
			init.ops = &hda_dai_clk_ops;
			init.flags = 0; /* rate operation not support */
			init.parent_names = NULL;
			init.num_parents = 0;

			hw = &clk_data->hw;
			hw->init = &init;

			dev_dbg(dev, "register %s, clk_id 0x%x\n", init.name,
				clk_data->clk_id);

			ret = devm_clk_hw_register(dev, hw);
			if (ret) {
				dev_err(dev,
					"failed to register clk hw %s, ret %d\n",
					init.name, ret);
				return ret;
			}

			if (dev->of_node)
				ret = devm_of_clk_add_hw_provider(dev,
								  of_clk_hw_simple_get,
								  hw);
			else
				ret = devm_clk_hw_register_clkdev(dev, hw,
								  init.name,
								  NULL);

			if (ret) {
				dev_err(dev,
					"failed to register clkdev %s, ret %d\n",
					init.name, ret);
				return ret;
			}
		}
	}

	return 0;
}
