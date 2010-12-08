/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "rtl8192c-reg.h"
#include "rtl8192c-def.h"
#include "rtl8192c-phy.h"
#include "rtl8192c-trx.h"
#include "rtl8192c-led.h"

static enum rtl_desc_qsel _rtl92ce_map_hwqueue_to_fwqueue(u16 fc,
							  unsigned int
							  skb_queue)
{
	enum rtl_desc_qsel qsel;

	if (unlikely(ieee80211_is_beacon(fc))) {
		qsel = QSLT_BEACON;
		return qsel;
	}

	if (ieee80211_is_mgmt(fc)) {
		qsel = QSLT_MGNT;
		return qsel;
	}

	switch (skb_queue) {
	case VO_QUEUE:
		qsel = QSLT_VO;
		break;
	case VI_QUEUE:
		qsel = QSLT_VI;
		break;
	case BE_QUEUE:
		qsel = QSLT_BE;
		break;
	case BK_QUEUE:
		qsel = QSLT_BK;
		break;
	default:
		qsel = QSLT_BE;
		RT_ASSERT(false, ("BE queue, skb_queue:%d,"
				  " set qsel = 0x%X\n", skb_queue, QSLT_BE));
		break;
	}
	return qsel;
}

static int _rtl92ce_rate_mapping(bool isht, u8 desc_rate, bool first_ampdu)
{
	int rate_idx;

	if (first_ampdu) {
		if (false == isht) {
			switch (desc_rate) {
			case DESC92C_RATE1M:
				rate_idx = 0;
				break;
			case DESC92C_RATE2M:
				rate_idx = 1;
				break;
			case DESC92C_RATE5_5M:
				rate_idx = 2;
				break;
			case DESC92C_RATE11M:
				rate_idx = 3;
				break;
			case DESC92C_RATE6M:
				rate_idx = 4;
				break;
			case DESC92C_RATE9M:
				rate_idx = 5;
				break;
			case DESC92C_RATE12M:
				rate_idx = 6;
				break;
			case DESC92C_RATE18M:
				rate_idx = 7;
				break;
			case DESC92C_RATE24M:
				rate_idx = 8;
				break;
			case DESC92C_RATE36M:
				rate_idx = 9;
				break;
			case DESC92C_RATE48M:
				rate_idx = 10;
				break;
			case DESC92C_RATE54M:
				rate_idx = 11;
				break;
			default:
				rate_idx = 0;
				break;
			}
		} else {
			rate_idx = 11;
		}

		return rate_idx;
	}

	switch (desc_rate) {
	case DESC92C_RATE1M:
		rate_idx = 0;
		break;
	case DESC92C_RATE2M:
		rate_idx = 1;
		break;
	case DESC92C_RATE5_5M:
		rate_idx = 2;
		break;
	case DESC92C_RATE11M:
		rate_idx = 3;
		break;
	case DESC92C_RATE6M:
		rate_idx = 4;
		break;
	case DESC92C_RATE9M:
		rate_idx = 5;
		break;
	case DESC92C_RATE12M:
		rate_idx = 6;
		break;
	case DESC92C_RATE18M:
		rate_idx = 7;
		break;
	case DESC92C_RATE24M:
		rate_idx = 8;
		break;
	case DESC92C_RATE36M:
		rate_idx = 9;
		break;
	case DESC92C_RATE48M:
		rate_idx = 10;
		break;
	case DESC92C_RATE54M:
		rate_idx = 11;
		break;
	default:
		rate_idx = 11;
		break;
	}
	return rate_idx;
}

static u8 _rtl92c_query_rxpwrpercentage(char antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return 0;
	else if (antpower >= 0)
		return 100;
	else
		return 100 + antpower;
}

static u8 _rtl92c_evm_db_to_percentage(char value)
{
	char ret_val;
	ret_val = value;

	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;

	return ret_val;
}

static long _rtl92ce_translate_todbm(struct ieee80211_hw *hw,
				     u8 signal_strength_index)
{
	long signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;
	return signal_power;
}

static long _rtl92ce_signal_scale_mapping(struct ieee80211_hw *hw,
		long currsig)
{
	long retsig;

	if (currsig >= 61 && currsig <= 100)
		retsig = 90 + ((currsig - 60) / 4);
	else if (currsig >= 41 && currsig <= 60)
		retsig = 78 + ((currsig - 40) / 2);
	else if (currsig >= 31 && currsig <= 40)
		retsig = 66 + (currsig - 30);
	else if (currsig >= 21 && currsig <= 30)
		retsig = 54 + (currsig - 20);
	else if (currsig >= 5 && currsig <= 20)
		retsig = 42 + (((currsig - 5) * 2) / 3);
	else if (currsig == 4)
		retsig = 36;
	else if (currsig == 3)
		retsig = 27;
	else if (currsig == 2)
		retsig = 18;
	else if (currsig == 1)
		retsig = 9;
	else
		retsig = currsig;

	return retsig;
}

static void _rtl92ce_query_rxphystatus(struct ieee80211_hw *hw,
				       struct rtl_stats *pstats,
				       struct rx_desc_92c *pdesc,
				       struct rx_fwinfo_92c *p_drvinfo,
				       bool bpacket_match_bssid,
				       bool bpacket_toself,
				       bool b_packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct phy_sts_cck_8192s_t *cck_buf;
	s8 rx_pwr_all, rx_pwr[4];
	u8 rf_rx_num, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi;
	bool is_cck_rate;

	is_cck_rate = RX_HAL_IS_CCK_RATE(pdesc);
	pstats->b_packet_matchbssid = bpacket_match_bssid;
	pstats->b_packet_toself = bpacket_toself;
	pstats->b_is_cck = is_cck_rate;
	pstats->b_packet_beacon = b_packet_beacon;
	pstats->b_is_cck = is_cck_rate;
	pstats->rx_mimo_signalquality[0] = -1;
	pstats->rx_mimo_signalquality[1] = -1;

	if (is_cck_rate) {
		u8 report, cck_highpwr;
		cck_buf = (struct phy_sts_cck_8192s_t *)p_drvinfo;

		cck_highpwr = (u8) rtl_get_bbreg(hw,
					 RFPGA0_XA_HSSIPARAMETER2,
					 BIT(9));
		if (!cck_highpwr) {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = cck_buf->cck_agc_rpt & 0xc0;
			report = report >> 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - (cck_agc_rpt & 0x3e);
				break;
			case 0x2:
				rx_pwr_all = -26 - (cck_agc_rpt & 0x3e);
				break;
			case 0x1:
				rx_pwr_all = -12 - (cck_agc_rpt & 0x3e);
				break;
			case 0x0:
				rx_pwr_all = 16 - (cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = p_drvinfo->cfosho[0] & 0x60;
			report = report >> 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -26 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -12 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 16 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			}
		}

		pwdb_all = _rtl92c_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (bpacket_match_bssid) {
			u8 sq;
			if (pstats->rx_pwdb_all > 40)
				sq = 100;
			else {
				sq = cck_buf->sq_rpt;
				if (sq > 64)
					sq = 0;
				else if (sq < 20)
					sq = 100;
				else
					sq = ((64 - sq) * 100) / 44;
			}

			pstats->signalquality = sq;
			pstats->rx_mimo_signalquality[0] = sq;
			pstats->rx_mimo_signalquality[1] = -1;
		}
	} else {
		rtlpriv->dm.brfpath_rxenable[0] =
		    rtlpriv->dm.brfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF90_PATH_MAX; i++) {
			if (rtlpriv->dm.brfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] =
			    ((p_drvinfo->gain_trsw[i] & 0x3f) * 2) - 110;
			rssi = _rtl92c_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
			    (long)(p_drvinfo->rxsnr[i] / 2);

			if (bpacket_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8) rssi;
		}

		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;
		pwdb_all = _rtl92c_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (pdesc->rxht && pdesc->rxmcs >= DESC92C_RATEMCS8 &&
		    pdesc->rxmcs <= DESC92C_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			evm = _rtl92c_evm_db_to_percentage(p_drvinfo->rxevm[i]);

			if (bpacket_match_bssid) {
				if (i == 0)
					pstats->signalquality =
					    (u8) (evm & 0xff);
				pstats->rx_mimo_signalquality[i] =
				    (u8) (evm & 0xff);
			}
		}
	}

	if (is_cck_rate)
		pstats->signalstrength =
		    (u8) (_rtl92ce_signal_scale_mapping(hw, pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength =
		    (u8) (_rtl92ce_signal_scale_mapping
			  (hw, total_rssi /= rf_rx_num));
}

static void _rtl92ce_process_ui_rssi(struct ieee80211_hw *hw,
		struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 rfpath;
	u32 last_rssi, tmpval;

	if (pstats->b_packet_toself || pstats->b_packet_beacon) {
		rtlpriv->stats.rssi_calculate_cnt++;

		if (rtlpriv->stats.ui_rssi.total_num++ >=
		    PHY_RSSI_SLID_WIN_MAX) {
			rtlpriv->stats.ui_rssi.total_num =
			    PHY_RSSI_SLID_WIN_MAX;
			last_rssi =
			    rtlpriv->stats.ui_rssi.elements[rtlpriv->
						    stats.ui_rssi.index];
			rtlpriv->stats.ui_rssi.total_val -= last_rssi;
		}

		rtlpriv->stats.ui_rssi.total_val += pstats->signalstrength;
		rtlpriv->stats.ui_rssi.elements[rtlpriv->stats.ui_rssi.
						index++] =
		    pstats->signalstrength;

		if (rtlpriv->stats.ui_rssi.index >= PHY_RSSI_SLID_WIN_MAX)
			rtlpriv->stats.ui_rssi.index = 0;

		tmpval = rtlpriv->stats.ui_rssi.total_val /
		    rtlpriv->stats.ui_rssi.total_num;
		rtlpriv->stats.signal_strength =
		    _rtl92ce_translate_todbm(hw, (u8) tmpval);
		pstats->rssi = rtlpriv->stats.signal_strength;
	}

	if (!pstats->b_is_cck && pstats->b_packet_toself) {
		for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
		     rfpath++) {

			if (!rtl8192_phy_check_is_legal_rfpath(hw, rfpath))
				continue;

			if (rtlpriv->stats.rx_rssi_percentage[rfpath] == 0) {
				rtlpriv->stats.rx_rssi_percentage[rfpath] =
				    pstats->rx_mimo_signalstrength[rfpath];

			}

			if (pstats->rx_mimo_signalstrength[rfpath] >
			    rtlpriv->stats.rx_rssi_percentage[rfpath]) {
				rtlpriv->stats.rx_rssi_percentage[rfpath] =
				    ((rtlpriv->stats.
				      rx_rssi_percentage[rfpath] *
				      (RX_SMOOTH_FACTOR - 1)) +
				     (pstats->rx_mimo_signalstrength[rfpath])) /
				    (RX_SMOOTH_FACTOR);

				rtlpriv->stats.rx_rssi_percentage[rfpath] =
				    rtlpriv->stats.rx_rssi_percentage[rfpath] +
				    1;
			} else {
				rtlpriv->stats.rx_rssi_percentage[rfpath] =
				    ((rtlpriv->stats.
				      rx_rssi_percentage[rfpath] *
				      (RX_SMOOTH_FACTOR - 1)) +
				     (pstats->rx_mimo_signalstrength[rfpath])) /
				    (RX_SMOOTH_FACTOR);
			}

		}
	}
}

static void _rtl92ce_update_rxsignalstatistics(struct ieee80211_hw *hw,
					       struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int weighting;

	if (rtlpriv->stats.recv_signal_power == 0)
		rtlpriv->stats.recv_signal_power = pstats->recvsignalpower;

	if (pstats->recvsignalpower > rtlpriv->stats.recv_signal_power)
		weighting = 5;

	else if (pstats->recvsignalpower < rtlpriv->stats.recv_signal_power)
		weighting = (-5);

	rtlpriv->stats.recv_signal_power =
	    (rtlpriv->stats.recv_signal_power * 5 +
	     pstats->recvsignalpower + weighting) / 6;
}

static void _rtl92ce_process_pwdb(struct ieee80211_hw *hw,
		struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undecorated_smoothed_pwdb;

	if (mac->opmode == NL80211_IFTYPE_ADHOC) {
		return;
	} else {
		undecorated_smoothed_pwdb =
		    rtlpriv->dm.undecorated_smoothed_pwdb;
	}

	if (pstats->b_packet_toself || pstats->b_packet_beacon) {
		if (undecorated_smoothed_pwdb < 0)
			undecorated_smoothed_pwdb = pstats->rx_pwdb_all;

		if (pstats->rx_pwdb_all > (u32) undecorated_smoothed_pwdb) {
			undecorated_smoothed_pwdb =
			    (((undecorated_smoothed_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);

			undecorated_smoothed_pwdb = undecorated_smoothed_pwdb
			    + 1;
		} else {
			undecorated_smoothed_pwdb =
			    (((undecorated_smoothed_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
		}

		rtlpriv->dm.undecorated_smoothed_pwdb =
		    undecorated_smoothed_pwdb;
		_rtl92ce_update_rxsignalstatistics(hw, pstats);
	}
}

static void _rtl92ce_process_ui_link_quality(struct ieee80211_hw *hw,
					     struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 last_evm, n_spatialstream, tmpval;

	if (pstats->signalquality != 0) {
		if (pstats->b_packet_toself || pstats->b_packet_beacon) {

			if (rtlpriv->stats.ui_link_quality.total_num++ >=
			    PHY_LINKQUALITY_SLID_WIN_MAX) {
				rtlpriv->stats.ui_link_quality.total_num =
				    PHY_LINKQUALITY_SLID_WIN_MAX;
				last_evm =
				    rtlpriv->stats.
				    ui_link_quality.elements[rtlpriv->
							  stats.ui_link_quality.
							  index];
				rtlpriv->stats.ui_link_quality.total_val -=
				    last_evm;
			}

			rtlpriv->stats.ui_link_quality.total_val +=
			    pstats->signalquality;
			rtlpriv->stats.ui_link_quality.elements[rtlpriv->stats.
								ui_link_quality.
								index++] =
			    pstats->signalquality;

			if (rtlpriv->stats.ui_link_quality.index >=
			    PHY_LINKQUALITY_SLID_WIN_MAX)
				rtlpriv->stats.ui_link_quality.index = 0;

			tmpval = rtlpriv->stats.ui_link_quality.total_val /
			    rtlpriv->stats.ui_link_quality.total_num;
			rtlpriv->stats.signal_quality = tmpval;

			rtlpriv->stats.last_sigstrength_inpercent = tmpval;

			for (n_spatialstream = 0; n_spatialstream < 2;
			     n_spatialstream++) {
				if (pstats->
				    rx_mimo_signalquality[n_spatialstream] !=
				    -1) {
					if (rtlpriv->stats.
					    rx_evm_percentage[n_spatialstream]
					    == 0) {
						rtlpriv->stats.
						   rx_evm_percentage
						   [n_spatialstream] =
						   pstats->rx_mimo_signalquality
						   [n_spatialstream];
					}

					rtlpriv->stats.
					    rx_evm_percentage[n_spatialstream] =
					    ((rtlpriv->
					      stats.rx_evm_percentage
					      [n_spatialstream] *
					      (RX_SMOOTH_FACTOR - 1)) +
					     (pstats->
					      rx_mimo_signalquality
					      [n_spatialstream] * 1)) /
					    (RX_SMOOTH_FACTOR);
				}
			}
		}
	} else {
		;
	}
}

static void _rtl92ce_process_phyinfo(struct ieee80211_hw *hw,
				     u8 *buffer,
				     struct rtl_stats *pcurrent_stats)
{

	if (!pcurrent_stats->b_packet_matchbssid &&
	    !pcurrent_stats->b_packet_beacon)
		return;

	_rtl92ce_process_ui_rssi(hw, pcurrent_stats);
	_rtl92ce_process_pwdb(hw, pcurrent_stats);
	_rtl92ce_process_ui_link_quality(hw, pcurrent_stats);
}

static void _rtl92ce_translate_rx_signal_stuff(struct ieee80211_hw *hw,
					       struct sk_buff *skb,
					       struct rtl_stats *pstats,
					       struct rx_desc_92c *pdesc,
					       struct rx_fwinfo_92c *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	u8 *psaddr;
	u16 fc, type;
	bool b_packet_matchbssid, b_packet_toself, b_packet_beacon;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;

	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = le16_to_cpu(hdr->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	psaddr = hdr->addr2;

	b_packet_matchbssid =
	    ((IEEE80211_FTYPE_CTL != type) &&
	     (!compare_ether_addr(mac->bssid,
				  (fc & IEEE80211_FCTL_TODS) ?
				  hdr->addr1 : (fc & IEEE80211_FCTL_FROMDS) ?
				  hdr->addr2 : hdr->addr3)) &&
	     (!pstats->b_hwerror) && (!pstats->b_crc) && (!pstats->b_icv));

	b_packet_toself = b_packet_matchbssid &&
	    (!compare_ether_addr(praddr, rtlefuse->dev_addr));

	if (ieee80211_is_beacon(fc))
		b_packet_beacon = true;

	_rtl92ce_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
				   b_packet_matchbssid, b_packet_toself,
				   b_packet_beacon);

	_rtl92ce_process_phyinfo(hw, tmp_buf, pstats);
}

bool rtl92ce_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *p_desc, struct sk_buff *skb)
{
	struct rx_fwinfo_92c *p_drvinfo;
	struct rx_desc_92c *pdesc = (struct rx_desc_92c *)p_desc;

	u32 phystatus = GET_RX_DESC_PHYST(pdesc);
	stats->length = (u16) GET_RX_DESC_PKT_LEN(pdesc);
	stats->rx_drvinfo_size = (u8) GET_RX_DESC_DRV_INFO_SIZE(pdesc) *
	    RX_DRV_INFO_SIZE_UNIT;
	stats->rx_bufshift = (u8) (GET_RX_DESC_SHIFT(pdesc) & 0x03);
	stats->b_icv = (u16) GET_RX_DESC_ICV(pdesc);
	stats->b_crc = (u16) GET_RX_DESC_CRC32(pdesc);
	stats->b_hwerror = (stats->b_crc | stats->b_icv);
	stats->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	stats->rate = (u8) GET_RX_DESC_RXMCS(pdesc);
	stats->b_shortpreamble = (u16) GET_RX_DESC_SPLCP(pdesc);
	stats->b_isampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	stats->b_isampdu = (bool) ((GET_RX_DESC_PAGGR(pdesc) == 1)
				   && (GET_RX_DESC_FAGGR(pdesc) == 1));
	stats->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	stats->rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(pdesc);

	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->band = hw->conf.channel->band;

	if (GET_RX_DESC_CRC32(pdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (!GET_RX_DESC_SWDEC(pdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;

	if (GET_RX_DESC_BW(pdesc))
		rx_status->flag |= RX_FLAG_40MHZ;

	if (GET_RX_DESC_RXHT(pdesc))
		rx_status->flag |= RX_FLAG_HT;

	rx_status->flag |= RX_FLAG_TSFT;

	if (stats->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;

	rx_status->rate_idx = _rtl92ce_rate_mapping((bool)
						    GET_RX_DESC_RXHT(pdesc),
						    (u8)
						    GET_RX_DESC_RXMCS(pdesc),
						    (bool)
						    GET_RX_DESC_PAGGR(pdesc));

	rx_status->mactime = GET_RX_DESC_TSFL(pdesc);
	if (phystatus == true) {
		p_drvinfo = (struct rx_fwinfo_92c *)(skb->data +
						     stats->rx_bufshift);

		_rtl92ce_translate_rx_signal_stuff(hw,
						   skb, stats, pdesc,
						   p_drvinfo);
	}

	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->rssi + 10;
	/*rx_status->noise = -stats->noise; */

	return true;
}

void rtl92ce_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  struct ieee80211_tx_info *info, struct sk_buff *skb,
			  unsigned int queue_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool b_defaultadapter = true;

	struct ieee80211_sta *sta = ieee80211_find_sta(mac->vif, mac->bssid);

	u8 *pdesc = (u8 *) pdesc_tx;
	struct rtl_tcb_desc tcb_desc;
	u8 *qc = ieee80211_get_qos_ctl(hdr);
	u8 tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
	u16 seq_number;
	u16 fc = le16_to_cpu(hdr->frame_control);
	u8 rate_flag = info->control.rates[0].flags;

	enum rtl_desc_qsel fw_qsel =
	    _rtl92ce_map_hwqueue_to_fwqueue(le16_to_cpu(hdr->frame_control),
					    queue_index);

	bool b_firstseg = ((hdr->seq_ctrl &
			    cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0);

	bool b_lastseg = ((hdr->frame_control &
			   cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) == 0);

	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
					    skb->data, skb->len,
					    PCI_DMA_TODEVICE);

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	rtl_get_tcb_desc(hw, info, skb, &tcb_desc);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, sizeof(struct tx_desc_92c));

	if (b_firstseg) {
		SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);

		SET_TX_DESC_TX_RATE(pdesc, tcb_desc.hw_rate);

		if (tcb_desc.use_shortgi || tcb_desc.use_shortpreamble)
			SET_TX_DESC_DATA_SHORTGI(pdesc, 1);

		if (mac->tids[tid].agg.agg_state == RTL_AGG_ON &&
		    info->flags & IEEE80211_TX_CTL_AMPDU) {
			SET_TX_DESC_AGG_BREAK(pdesc, 1);
			SET_TX_DESC_MAX_AGG_NUM(pdesc, 0x14);
		}
		SET_TX_DESC_SEQ(pdesc, seq_number);

		SET_TX_DESC_RTS_ENABLE(pdesc, ((tcb_desc.b_rts_enable &&
						!tcb_desc.
						b_cts_enable) ? 1 : 0));
		SET_TX_DESC_HW_RTS_ENABLE(pdesc,
					  ((tcb_desc.b_rts_enable
					    || tcb_desc.b_cts_enable) ? 1 : 0));
		SET_TX_DESC_CTS2SELF(pdesc, ((tcb_desc.b_cts_enable) ? 1 : 0));
		SET_TX_DESC_RTS_STBC(pdesc, ((tcb_desc.b_rts_stbc) ? 1 : 0));

		SET_TX_DESC_RTS_RATE(pdesc, tcb_desc.rts_rate);
		SET_TX_DESC_RTS_BW(pdesc, 0);
		SET_TX_DESC_RTS_SC(pdesc, tcb_desc.rts_sc);
		SET_TX_DESC_RTS_SHORT(pdesc,
				      ((tcb_desc.rts_rate <= DESC92C_RATE54M) ?
				      (tcb_desc.b_rts_use_shortpreamble ? 1 : 0)
				      : (tcb_desc.b_rts_use_shortgi ? 1 : 0)));

		if (mac->bw_40) {
			if (tcb_desc.b_packet_bw) {
				SET_TX_DESC_DATA_BW(pdesc, 1);
				SET_TX_DESC_TX_SUB_CARRIER(pdesc, 3);
			} else {
				SET_TX_DESC_DATA_BW(pdesc, 0);

				if (rate_flag & IEEE80211_TX_RC_DUP_DATA) {
					SET_TX_DESC_TX_SUB_CARRIER(pdesc,
							mac->cur_40_prime_sc);
				}
			}
		} else {
			SET_TX_DESC_DATA_BW(pdesc, 0);
			SET_TX_DESC_TX_SUB_CARRIER(pdesc, 0);
		}

		SET_TX_DESC_LINIP(pdesc, 0);
		SET_TX_DESC_PKT_SIZE(pdesc, (u16) skb->len);

		if (sta) {
			u8 ampdu_density = sta->ht_cap.ampdu_density;
			SET_TX_DESC_AMPDU_DENSITY(pdesc, ampdu_density);
		}

		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf =
			    info->control.hw_key;

			switch (keyconf->cipher) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
			case WLAN_CIPHER_SUITE_TKIP:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x1);
				break;
			case WLAN_CIPHER_SUITE_CCMP:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x3);
				break;
			default:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x0);
				break;

			}
		}

		SET_TX_DESC_PKT_ID(pdesc, 0);
		SET_TX_DESC_QUEUE_SEL(pdesc, fw_qsel);

		SET_TX_DESC_DATA_RATE_FB_LIMIT(pdesc, 0x1F);
		SET_TX_DESC_RTS_RATE_FB_LIMIT(pdesc, 0xF);
		SET_TX_DESC_DISABLE_FB(pdesc, 0);
		SET_TX_DESC_USE_RATE(pdesc, tcb_desc.use_driver_rate ? 1 : 0);

		if (ieee80211_is_data_qos(fc)) {
			if (mac->rdg_en) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
					 ("Enable RDG function.\n"));
				SET_TX_DESC_RDG_ENABLE(pdesc, 1);
				SET_TX_DESC_HTC(pdesc, 1);
			}
		}
	}

	SET_TX_DESC_FIRST_SEG(pdesc, (b_firstseg ? 1 : 0));
	SET_TX_DESC_LAST_SEG(pdesc, (b_lastseg ? 1 : 0));

	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) skb->len);

	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, cpu_to_le32(mapping));

	if (rtlpriv->dm.b_useramask) {
		SET_TX_DESC_RATE_ID(pdesc, tcb_desc.ratr_index);
		SET_TX_DESC_MACID(pdesc, tcb_desc.mac_id);
	} else {
		SET_TX_DESC_RATE_ID(pdesc, 0xC + tcb_desc.ratr_index);
		SET_TX_DESC_MACID(pdesc, tcb_desc.ratr_index);
	}

	if ((!ieee80211_is_data_qos(fc)) && ppsc->b_leisure_ps &&
	    ppsc->b_fwctrl_lps) {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_PKT_ID(pdesc, 8);

		if (!b_defaultadapter)
			SET_TX_DESC_QOS(pdesc, 1);
	}

	SET_TX_DESC_MORE_FRAG(pdesc, (b_lastseg ? 0 : 1));

	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr))) {
		SET_TX_DESC_BMC(pdesc, 1);
	}

	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, ("\n"));
}

void rtl92ce_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool b_firstseg,
			     bool b_lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 fw_queue = QSLT_BEACON;

	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
					    skb->data, skb->len,
					    PCI_DMA_TODEVICE);

	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	u16 fc = le16_to_cpu(hdr->frame_control);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE);

	if (b_firstseg)
		SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);

	SET_TX_DESC_TX_RATE(pdesc, DESC92C_RATE1M);

	SET_TX_DESC_SEQ(pdesc, 0);

	SET_TX_DESC_LINIP(pdesc, 0);

	SET_TX_DESC_QUEUE_SEL(pdesc, fw_queue);

	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);

	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) (skb->len));

	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, cpu_to_le32(mapping));

	SET_TX_DESC_RATE_ID(pdesc, 7);
	SET_TX_DESC_MACID(pdesc, 0);

	SET_TX_DESC_OWN(pdesc, 1);

	SET_TX_DESC_PKT_SIZE((u8 *) pdesc, (u16) (skb->len));

	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);

	SET_TX_DESC_OFFSET(pdesc, 0x20);

	SET_TX_DESC_USE_RATE(pdesc, 1);

	if (!ieee80211_is_data_qos(fc)) {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_PKT_ID(pdesc, 8);
	}

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "H2C Tx Cmd Content\n",
		      pdesc, TX_DESC_SIZE);
}

void rtl92ce_set_desc(u8 *pdesc, bool istx, u8 desc_name, u8 *val)
{
	if (istx == true) {
		switch (desc_name) {
		case HW_DESC_OWN:
			SET_TX_DESC_OWN(pdesc, 1);
			break;
		case HW_DESC_TX_NEXTDESC_ADDR:
			SET_TX_DESC_NEXT_DESC_ADDRESS(pdesc, *(u32 *) val);
			break;
		default:
			RT_ASSERT(false, ("ERR txdesc :%d"
					  " not process\n", desc_name));
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXOWN:
			SET_RX_DESC_OWN(pdesc, 1);
			break;
		case HW_DESC_RXBUFF_ADDR:
			SET_RX_DESC_BUFF_ADDR(pdesc, *(u32 *) val);
			break;
		case HW_DESC_RXPKT_LEN:
			SET_RX_DESC_PKT_LEN(pdesc, *(u32 *) val);
			break;
		case HW_DESC_RXERO:
			SET_RX_DESC_EOR(pdesc, 1);
			break;
		default:
			RT_ASSERT(false, ("ERR rxdesc :%d "
					  "not process\n", desc_name));
			break;
		}
	}
}

u32 rtl92ce_get_desc(u8 *p_desc, bool istx, u8 desc_name)
{
	u32 ret = 0;

	if (istx == true) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = GET_TX_DESC_OWN(p_desc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = GET_TX_DESC_TX_BUFFER_ADDRESS(p_desc);
			break;
		default:
			RT_ASSERT(false, ("ERR txdesc :%d "
					  "not process\n", desc_name));
			break;
		}
	} else {
		struct rx_desc_92c *pdesc = (struct rx_desc_92c *)p_desc;
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = GET_RX_DESC_OWN(pdesc);
			break;
		case HW_DESC_RXPKT_LEN:
			ret = GET_RX_DESC_PKT_LEN(pdesc);
			break;
		default:
			RT_ASSERT(false, ("ERR rxdesc :%d "
					  "not process\n", desc_name));
			break;
		}
	}
	return ret;
}

void rtl92ce_tx_polling(struct ieee80211_hw *hw, unsigned int hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (hw_queue == BEACON_QUEUE) {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, BIT(4));
	} else {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG,
			       BIT(0) << (hw_queue));
	}
}
