/* Copyright (C) 2007-2013 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _NET_BATMAN_ADV_SEND_H_
#define _NET_BATMAN_ADV_SEND_H_

int batadv_send_skb_packet(struct sk_buff *skb,
			   struct batadv_hard_iface *hard_iface,
			   const uint8_t *dst_addr);
int batadv_send_skb_to_orig(struct sk_buff *skb,
			    struct batadv_orig_node *orig_node,
			    struct batadv_hard_iface *recv_if);
void batadv_schedule_bat_ogm(struct batadv_hard_iface *hard_iface);
int batadv_add_bcast_packet_to_list(struct batadv_priv *bat_priv,
				    const struct sk_buff *skb,
				    unsigned long delay);
void batadv_send_outstanding_bat_ogm_packet(struct work_struct *work);
void
batadv_purge_outstanding_packets(struct batadv_priv *bat_priv,
				 const struct batadv_hard_iface *hard_iface);
bool batadv_send_skb_prepare_unicast_4addr(struct batadv_priv *bat_priv,
					   struct sk_buff *skb,
					   struct batadv_orig_node *orig_node,
					   int packet_subtype);
int batadv_send_skb_generic_unicast(struct batadv_priv *bat_priv,
				    struct sk_buff *skb, int packet_type,
				    int packet_subtype);


/**
 * batadv_send_unicast_skb - send the skb encapsulated in a unicast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the payload to send
 *
 * Returns 1 in case of error or 0 otherwise.
 */
static inline int batadv_send_skb_unicast(struct batadv_priv *bat_priv,
					  struct sk_buff *skb)
{
	return batadv_send_skb_generic_unicast(bat_priv, skb, BATADV_UNICAST,
					       0);
}

/**
 * batadv_send_4addr_unicast_skb - send the skb encapsulated in a unicast 4addr
 *  packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the payload to send
 * @packet_subtype: the unicast 4addr packet subtype to use
 *
 * Returns 1 in case of error or 0 otherwise.
 */
static inline int batadv_send_skb_unicast_4addr(struct batadv_priv *bat_priv,
						struct sk_buff *skb,
						int packet_subtype)
{
	return batadv_send_skb_generic_unicast(bat_priv, skb,
					       BATADV_UNICAST_4ADDR,
					       packet_subtype);
}

#endif /* _NET_BATMAN_ADV_SEND_H_ */
