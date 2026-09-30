// SPDX-License-Identifier: GPL-2.0
/*
 * Google Wonder Virtual Driver for Android Mosey (AirDrop interoperability)
 * Native implementation for Linux 6.12 GKI / WildKernel
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/ktime.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#define DRV_NAME "wonder"
#define IF_NAME "mosey0"
#define GOOGLE_OUI 0x001A11

enum wonder_vendor_subcmds {
	WONDER_VENDOR_SCMD_SET_FREQ = 1,
	WONDER_VENDOR_SCMD_SET_FILTER = 2,
	WONDER_VENDOR_SCMD_SET_FIXED_TX_RATE = 3,
	WONDER_VENDOR_SCMD_SET_REG = 4,
	WONDER_VENDOR_SCMD_GET_IF_MAC_ADDR = 5,
	WONDER_VENDOR_SCMD_SET_CHANNEL_SCHEDULE_REQ = 6,
	WONDER_VENDOR_SCMD_GET_MAC_TSF = 7,
	WONDER_VENDOR_SCMD_GET_CAP = 8,
	WONDER_VENDOR_SCMD_GET_CHANNEL_STATUS_REPORT = 9,
	WONDER_VENDOR_SCMD_SET_STATION_INFO = 10,
	WONDER_VENDOR_SCMD_SET_FEATURES = 11,
	WONDER_VENDOR_SCMD_SET_TX_RATE_TEST = 15,
};

struct wonder_priv {
	struct ieee80211_hw *hw;
	struct net_device *netdev;
	struct device *dev;
	u8 mac_addr[ETH_ALEN];
	u32 freq;
	u32 channel;
	struct mutex lock;
};

static struct wonder_priv *g_wonder_priv;

/* Netdev operations for mosey0 */
static int mosey_netdev_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int mosey_netdev_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t mosey_netdev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mosey_netdev_ops = {
	.ndo_open = mosey_netdev_open,
	.ndo_stop = mosey_netdev_stop,
	.ndo_start_xmit = mosey_netdev_start_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
};

/* Vendor command handlers for Mosey daemon */
static int wonder_vcmd_get_cap(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int len)
{
	struct sk_buff *skb;
	u32 max_channels = 0x7FFFFFFF;
	u8 val1 = 1, val0 = 0;

	(void)wdev; (void)data; (void)len;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 64);
	if (!skb)
		return -ENOMEM;

	if (nla_put_u32(skb, 1, max_channels) ||
	    nla_put_u8(skb, 2, val1) ||
	    nla_put_u8(skb, 3, val1) ||
	    nla_put_u8(skb, 4, val1) ||
	    nla_put_u8(skb, 5, val1) ||
	    nla_put_u8(skb, 6, val0) ||
	    nla_put_u8(skb, 7, val0)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	return cfg80211_vendor_cmd_reply(skb);
}

static int wonder_vcmd_get_if_mac_addr(struct wiphy *wiphy, struct wireless_dev *wdev,
				       const void *data, int len)
{
	struct wonder_priv *priv = wiphy_priv(wiphy);
	struct sk_buff *skb;

	(void)wdev; (void)data; (void)len;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 32);
	if (!skb)
		return -ENOMEM;

	if (nla_put(skb, 1, ETH_ALEN, priv->mac_addr)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	return cfg80211_vendor_cmd_reply(skb);
}

static int wonder_vcmd_get_mac_tsf(struct wiphy *wiphy, struct wireless_dev *wdev,
				   const void *data, int len)
{
	struct sk_buff *skb;
	u32 tsf_low;
	u64 now_ns;

	(void)wdev; (void)data; (void)len;

	now_ns = ktime_get_boottime_ns();
	tsf_low = (u32)(now_ns / 1000ULL);

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 64);
	if (!skb)
		return -ENOMEM;

	if (nla_put_u32(skb, 1, tsf_low) ||
	    nla_put_u64_64bit(skb, 2, now_ns, 0) ||
	    nla_put_u64_64bit(skb, 3, now_ns, 0)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	return cfg80211_vendor_cmd_reply(skb);
}

static int wonder_vcmd_set_freq(struct wiphy *wiphy, struct wireless_dev *wdev,
				const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_filter(struct wiphy *wiphy, struct wireless_dev *wdev,
				  const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_fixed_tx_rate(struct wiphy *wiphy, struct wireless_dev *wdev,
					const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_reg(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_channel_schedule_req(struct wiphy *wiphy, struct wireless_dev *wdev,
						const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_get_channel_status_report(struct wiphy *wiphy, struct wireless_dev *wdev,
						 const void *data, int len)
{
	struct sk_buff *skb;

	(void)wdev; (void)data; (void)len;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 32);
	if (!skb)
		return -ENOMEM;

	nla_put_u32(skb, 1, 0);
	return cfg80211_vendor_cmd_reply(skb);
}

static int wonder_vcmd_set_station_info(struct wiphy *wiphy, struct wireless_dev *wdev,
					const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_features(struct wiphy *wiphy, struct wireless_dev *wdev,
				    const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static int wonder_vcmd_set_tx_rate_test(struct wiphy *wiphy, struct wireless_dev *wdev,
					const void *data, int len)
{
	(void)wiphy; (void)wdev; (void)data; (void)len;
	return 0;
}

static const struct wiphy_vendor_command wonder_vendor_commands[] = {
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_FREQ },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_freq,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_FILTER },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_filter,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_FIXED_TX_RATE },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_fixed_tx_rate,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_TX_RATE_TEST },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_tx_rate_test,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_REG },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_reg,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_GET_IF_MAC_ADDR },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_get_if_mac_addr,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_CHANNEL_SCHEDULE_REQ },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_channel_schedule_req,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_GET_MAC_TSF },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_get_mac_tsf,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_GET_CAP },
		.flags = 0,
		.doit = wonder_vcmd_get_cap,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_GET_CHANNEL_STATUS_REPORT },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_get_channel_status_report,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_STATION_INFO },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_station_info,
	},
	{
		.info = { .vendor_id = GOOGLE_OUI, .subcmd = WONDER_VENDOR_SCMD_SET_FEATURES },
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = wonder_vcmd_set_features,
	},
};

static int wonder_start(struct ieee80211_hw *hw)
{
	(void)hw;
	return 0;
}

static void wonder_stop(struct ieee80211_hw *hw)
{
	(void)hw;
}

static int wonder_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	(void)hw;
	dev_kfree_skb_any(skb);
	return 0;
}

static int wonder_config(struct ieee80211_hw *hw, u32 changed)
{
	(void)hw; (void)changed;
	return 0;
}

static int wonder_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	(void)hw; (void)vif;
	return 0;
}

static void wonder_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	(void)hw; (void)vif;
}

static const struct ieee80211_ops wonder_mac_ops = {
	.start = wonder_start,
	.stop = wonder_stop,
	.tx = wonder_tx,
	.config = wonder_config,
	.add_interface = wonder_add_interface,
	.remove_interface = wonder_remove_interface,
};

static struct ieee80211_channel wonder_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1, .flags = 0 },
	{ .center_freq = 2437, .hw_value = 6, .flags = 0 },
	{ .center_freq = 2462, .hw_value = 11, .flags = 0 },
};

static struct ieee80211_channel wonder_channels_5ghz[] = {
	{ .center_freq = 5180, .hw_value = 36, .flags = 0 },
	{ .center_freq = 5220, .hw_value = 44, .flags = 0 },
	{ .center_freq = 5745, .hw_value = 149, .flags = 0 },
};

static struct ieee80211_rate wonder_rates[] = {
	{ .bitrate = 60, .hw_value = 0 },
	{ .bitrate = 120, .hw_value = 1 },
	{ .bitrate = 240, .hw_value = 2 },
	{ .bitrate = 540, .hw_value = 3 },
};

static int wonder_init_hw(struct wonder_priv *priv)
{
	struct ieee80211_hw *hw;
	struct wiphy *wiphy;
	struct net_device *netdev;
	int ret;

	hw = ieee80211_alloc_hw_nm(sizeof(struct wonder_priv *), &wonder_mac_ops, "wonder%d");
	if (!hw)
		return -ENOMEM;

	priv->hw = hw;
	wiphy = hw->wiphy;

	/* Setup 2.4 GHz band */
	hw->wiphy->bands[NL80211_BAND_2GHZ] = kzalloc(sizeof(struct ieee80211_supported_band), GFP_KERNEL);
	if (hw->wiphy->bands[NL80211_BAND_2GHZ]) {
		hw->wiphy->bands[NL80211_BAND_2GHZ]->channels = wonder_channels_2ghz;
		hw->wiphy->bands[NL80211_BAND_2GHZ]->n_channels = ARRAY_SIZE(wonder_channels_2ghz);
		hw->wiphy->bands[NL80211_BAND_2GHZ]->bitrates = wonder_rates;
		hw->wiphy->bands[NL80211_BAND_2GHZ]->n_bitrates = ARRAY_SIZE(wonder_rates);
	}

	/* Setup 5 GHz band */
	hw->wiphy->bands[NL80211_BAND_5GHZ] = kzalloc(sizeof(struct ieee80211_supported_band), GFP_KERNEL);
	if (hw->wiphy->bands[NL80211_BAND_5GHZ]) {
		hw->wiphy->bands[NL80211_BAND_5GHZ]->channels = wonder_channels_5ghz;
		hw->wiphy->bands[NL80211_BAND_5GHZ]->n_channels = ARRAY_SIZE(wonder_channels_5ghz);
		hw->wiphy->bands[NL80211_BAND_5GHZ]->bitrates = wonder_rates;
		hw->wiphy->bands[NL80211_BAND_5GHZ]->n_bitrates = ARRAY_SIZE(wonder_rates);
	}

	/* Vendor commands */
	wiphy->vendor_commands = wonder_vendor_commands;
	wiphy->n_vendor_commands = ARRAY_SIZE(wonder_vendor_commands);

	/* Supported interface modes */
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_P2P_GO) |
				 BIT(NL80211_IFTYPE_NAN);

	/* Set permanent MAC address */
	eth_random_addr(priv->mac_addr);
	memcpy(wiphy->perm_addr, priv->mac_addr, ETH_ALEN);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		pr_err("[wonder] Failed to register mac80211 hw: %d\n", ret);
		kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
		kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
		ieee80211_free_hw(hw);
		return ret;
	}

	pr_info("[wonder] Wonder Soft-MAC registered (wiphy: %s, MAC: %pM)\n",
		wiphy_name(wiphy), priv->mac_addr);

	/* Create mosey0 netdev */
	netdev = alloc_etherdev(0);
	if (netdev) {
		strscpy(netdev->name, IF_NAME, IFNAMSIZ);
		netdev->netdev_ops = &mosey_netdev_ops;
		eth_hw_addr_set(netdev, priv->mac_addr);
		netdev->mtu = 1500;
		ret = register_netdev(netdev);
		if (ret) {
			pr_warn("[wonder] Failed to register netdev %s: %d\n", IF_NAME, ret);
			free_netdev(netdev);
		} else {
			priv->netdev = netdev;
			pr_info("[wonder] Successfully registered network interface %s\n", IF_NAME);
		}
	}

	return 0;
}

static int wonder_probe(struct platform_device *pdev)
{
	struct wonder_priv *priv;
	int ret;

	pr_info("[wonder] Probing Google Wonder Driver\n");

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	mutex_init(&priv->lock);
	platform_set_drvdata(pdev, priv);
	g_wonder_priv = priv;

	ret = wonder_init_hw(priv);
	if (ret)
		return ret;

	return 0;
}

static void wonder_remove(struct platform_device *pdev)
{
	struct wonder_priv *priv = platform_get_drvdata(pdev);

	if (priv) {
		if (priv->netdev) {
			unregister_netdev(priv->netdev);
			free_netdev(priv->netdev);
		}
		if (priv->hw) {
			ieee80211_unregister_hw(priv->hw);
			kfree(priv->hw->wiphy->bands[NL80211_BAND_2GHZ]);
			kfree(priv->hw->wiphy->bands[NL80211_BAND_5GHZ]);
			ieee80211_free_hw(priv->hw);
		}
	}
	pr_info("[wonder] Google Wonder Driver removed\n");
}

static const struct of_device_id wonder_of_match[] = {
	{ .compatible = "google,wonder-drv-v1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wonder_of_match);

static struct platform_driver wonder_platform_driver = {
	.probe = wonder_probe,
	.remove_new = wonder_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = wonder_of_match,
	},
};

static struct platform_device *wonder_pdev;

static int __init wonder_init(void)
{
	int ret;

	pr_info("[wonder] Initializing Google Wonder Driver for Android Mosey\n");

	ret = platform_driver_register(&wonder_platform_driver);
	if (ret)
		return ret;

	/* Fallback platform device if DT node is disabled or not bound automatically */
	if (!g_wonder_priv) {
		pr_info("[wonder] Instantiating fallback platform device\n");
		wonder_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
		if (IS_ERR(wonder_pdev)) {
			pr_warn("[wonder] Fallback device registration failed\n");
			wonder_pdev = NULL;
		}
	}

	return 0;
}

static void __exit wonder_exit(void)
{
	if (wonder_pdev)
		platform_device_unregister(wonder_pdev);
	platform_driver_unregister(&wonder_platform_driver);
}

module_init(wonder_init);
module_exit(wonder_exit);

MODULE_AUTHOR("Google Android WiFi Team");
MODULE_DESCRIPTION("Google Wonder Virtual mac80211 Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("of:N*T*Cgoogle,wonder-drv-v1");
