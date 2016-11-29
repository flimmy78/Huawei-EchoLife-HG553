/*
 * RTSP extension for NAT alteration.
 *
 * Copyright (c) 2008 Broadcom Corporation.
 *
 * This source code is licensed under General Public License version 2.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tcp.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_nat_rule.h>
#include <linux/netfilter/nf_conntrack_rtsp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/****************************************************************************/
static int modify_ports(struct sk_buff **pskb, struct nf_conn *ct,
			enum ip_conntrack_info ctinfo,
			int matchoff, int matchlen,
			u_int16_t rtpport, u_int16_t rtcpport,
			char dash, int *delta)
{
	char buf[sizeof("65535-65535")];
	int len;

	if (dash)
		len = sprintf(buf, "%hu%c%hu", rtpport, dash, rtcpport);
	else
		len = sprintf(buf, "%hu", rtpport);
	if (!nf_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff, matchlen,
				      buf, len)) {
		if (net_ratelimit())
			printk("nf_nat_rtsp: nf_nat_mangle_tcp_packet error\n");
		return -1;
	}
	*delta = len - matchlen;
	return 0;
}

/****************************************************************************/
/* One data channel */
static int nat_rtsp_channel (struct sk_buff **pskb, struct nf_conn *ct,
			     enum ip_conntrack_info ctinfo,
			     unsigned int matchoff, unsigned int matchlen,
			     struct nf_conntrack_expect *rtp_exp, int *delta)
{
	struct nf_conntrack_expect *exp;
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port = 0;
	int exp_exist = 0;

	/* Set expectations for NAT */
	rtp_exp->saved_proto.udp.port = rtp_exp->tuple.dst.u.udp.port;
	rtp_exp->expectfn = nf_nat_follow_master;
	rtp_exp->dir = !dir;

	/* Lookup existing expects */
	write_lock_bh(&nf_conntrack_lock);
	list_for_each_entry(exp, &ct->expectations, master_list) {
		if (exp->saved_proto.udp.port == rtp_exp->saved_proto.udp.port){
			/* Expectation already exists */ 
			rtp_exp->tuple.dst.u.udp.port = 
				exp->tuple.dst.u.udp.port;
			nated_port = ntohs(exp->tuple.dst.u.udp.port);
			exp_exist = 1;
			break;
		}
	}
	write_unlock_bh(&nf_conntrack_lock);

	if (exp_exist) {
		nf_conntrack_expect_related(rtp_exp);
		goto modify_message;
	}

	/* Try to get a port. */
	for (nated_port = ntohs(rtp_exp->tuple.dst.u.udp.port);
	     nated_port != 0; nated_port++) {
		rtp_exp->tuple.dst.u.udp.port = htons(nated_port);
		if (nf_conntrack_expect_related(rtp_exp) == 0)
			break;
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("nf_nat_rtsp: out of UDP ports\n");
		return 0;
	}

modify_message:
	/* Modify message */
	if (modify_ports(pskb, ct, ctinfo, matchoff, matchlen,
			 nated_port, 0, 0, delta) < 0) {
		nf_conntrack_unexpect_related(rtp_exp);
		return -1;
	}

	/* Success */
	DEBUGP("nf_nat_rtsp: expect RTP %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(rtp_exp->tuple.src.u3.ip),
	       ntohs(rtp_exp->tuple.src.u.udp.port),
	       NIPQUAD(rtp_exp->tuple.dst.u3.ip),
	       ntohs(rtp_exp->tuple.dst.u.udp.port));

	return 0;
}

/****************************************************************************/
/* A pair of data channels (RTP/RTCP) */
static int nat_rtsp_channel2 (struct sk_buff **pskb, struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo,
			      unsigned int matchoff, unsigned int matchlen,
			      struct nf_conntrack_expect *rtp_exp,
			      struct nf_conntrack_expect *rtcp_exp,
			      char dash, int *delta)
{
	struct nf_conntrack_expect *exp;
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port = 0;
	int exp_exist = 0;

	/* Set expectations for NAT */
	rtp_exp->saved_proto.udp.port = rtp_exp->tuple.dst.u.udp.port;
	rtp_exp->expectfn = nf_nat_follow_master;
	rtp_exp->dir = !dir;
	rtcp_exp->saved_proto.udp.port = rtcp_exp->tuple.dst.u.udp.port;
	rtcp_exp->expectfn = nf_nat_follow_master;
	rtcp_exp->dir = !dir;

	/* Lookup existing expects */
	write_lock_bh(&nf_conntrack_lock);
	list_for_each_entry(exp, &ct->expectations, master_list) {
		if (exp->saved_proto.udp.port == rtp_exp->saved_proto.udp.port){
			/* Expectation already exists */ 
			rtp_exp->tuple.dst.u.udp.port = 
				exp->tuple.dst.u.udp.port;
			rtcp_exp->tuple.dst.u.udp.port = 
				htons(ntohs(exp->tuple.dst.u.udp.port) + 1);
			nated_port = ntohs(exp->tuple.dst.u.udp.port);
			exp_exist = 1;
			break;
		}
	}
	write_unlock_bh(&nf_conntrack_lock);

	if (exp_exist) {
		nf_conntrack_expect_related(rtp_exp);
		nf_conntrack_expect_related(rtcp_exp);
		goto modify_message;
	}

	/* Try to get a pair of ports. */
	for (nated_port = ntohs(rtp_exp->tuple.dst.u.udp.port) & (~1);
	     nated_port != 0; nated_port += 2) {
		rtp_exp->tuple.dst.u.udp.port = htons(nated_port);
		if (nf_conntrack_expect_related(rtp_exp) == 0) {
			rtcp_exp->tuple.dst.u.udp.port =
			    htons(nated_port + 1);
			if (nf_conntrack_expect_related(rtcp_exp) == 0)
				break;
			nf_conntrack_unexpect_related(rtp_exp);
		}
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("nf_nat_rtsp: out of RTP/RTCP ports\n");
		return 0;
	}

modify_message:
	/* Modify message */
	if (modify_ports(pskb, ct, ctinfo, matchoff, matchlen,
			 nated_port, nated_port + 1, dash, delta) < 0) {
		nf_conntrack_unexpect_related(rtp_exp);
		nf_conntrack_unexpect_related(rtcp_exp);
		return -1;
	}

	/* Success */
	DEBUGP("nf_nat_rtsp: expect RTP %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(rtp_exp->tuple.src.u3.ip),
	       ntohs(rtp_exp->tuple.src.u.udp.port),
	       NIPQUAD(rtp_exp->tuple.dst.u3.ip),
	       ntohs(rtp_exp->tuple.dst.u.udp.port));
	DEBUGP("nf_nat_rtsp: expect RTCP %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(rtcp_exp->tuple.src.u3.ip),
	       ntohs(rtcp_exp->tuple.src.u.udp.port),
	       NIPQUAD(rtcp_exp->tuple.dst.u3.ip),
	       ntohs(rtcp_exp->tuple.dst.u.udp.port));

	return 0;
}

/****************************************************************************/
static __be16 lookup_mapping_port(struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  __be16 port)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_conntrack_expect *exp;
	struct nf_conn *child;

	/* Lookup existing expects */
	DEBUGP("nf_nat_rtsp: looking up existing expectations...\n");
	list_for_each_entry(exp, &ct->expectations, master_list) {
		if (exp->tuple.dst.u.udp.port == port) {
			DEBUGP("nf_nat_rtsp: found port %hu mapped from %hu\n",
			       ntohs(exp->tuple.dst.u.udp.port),
			       ntohs(exp->saved_proto.all));
			return exp->saved_proto.all;
		}
	}

	/* Lookup existing connections */
	DEBUGP("nf_nat_rtsp: looking up existing connections...\n");
	list_for_each_entry(child, &ct->derived_connections, derived_list) {
		if (child->tuplehash[dir].tuple.dst.u.udp.port == port) {
			DEBUGP("nf_nat_rtsp: found port %hu mapped from %hu\n",
			       ntohs(child->tuplehash[dir].
			       tuple.dst.u.udp.port),
			       ntohs(child->tuplehash[!dir].
			       tuple.src.u.udp.port));
			return child->tuplehash[!dir].tuple.src.u.udp.port;
		}
	}

	return htons(0);
}

/****************************************************************************/
static int nat_rtsp_modify_port (struct sk_buff **pskb, struct nf_conn *ct,
			      	 enum ip_conntrack_info ctinfo,
				 unsigned int matchoff, unsigned int matchlen,
			      	 __be16 rtpport, int *delta)
{
	__be16 orig_port;

	orig_port = lookup_mapping_port(ct, ctinfo, rtpport);
	if (orig_port == htons(0)) {
		*delta = 0;
		return 0;
	}
	if (modify_ports(pskb, ct, ctinfo, matchoff, matchlen,
			 ntohs(orig_port), 0, 0, delta) < 0)
		return -1;
	DEBUGP("nf_nat_rtsp: Modified client_port from %hu to %hu\n",
	       ntohs(rtpport), ntohs(orig_port));
	return 0;
}

/****************************************************************************/
static int nat_rtsp_modify_port2 (struct sk_buff **pskb, struct nf_conn *ct,
			       	  enum ip_conntrack_info ctinfo,
				  unsigned int matchoff, unsigned int matchlen,
			       	  __be16 rtpport, __be16 rtcpport,
				  char dash, int *delta)
{
	__be16 orig_port;

	orig_port = lookup_mapping_port(ct, ctinfo, rtpport);
	if (orig_port == htons(0)) {
		*delta = 0;
		return 0;
	}
	if (modify_ports(pskb, ct, ctinfo, matchoff, matchlen,
			 ntohs(orig_port), ntohs(orig_port)+1, dash, delta) < 0)
		return -1;
	DEBUGP("nf_nat_rtsp: Modified client_port from %hu to %hu\n",
	       ntohs(rtpport), ntohs(orig_port));
	return 0;
}

/****************************************************************************/
static int nat_rtsp_modify_addr(struct sk_buff **pskb, struct nf_conn *ct,
				enum ip_conntrack_info ctinfo,
				int matchoff, int matchlen, int *delta)
{
	char buf[sizeof("255.255.255.255")];
	int dir = CTINFO2DIR(ctinfo);
	int len;

	/* Change the destination address to FW's WAN IP address */
	len = sprintf(buf, "%u.%u.%u.%u",
		      NIPQUAD(ct->tuplehash[!dir].tuple.dst.u3.ip));
	if (!nf_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff, matchlen,
				      buf, len)) {
		if (net_ratelimit())
			printk("nf_nat_rtsp: nf_nat_mangle_tcp_packet error\n");
		return -1;
	}
	*delta = len - matchlen;
	return 0;
}

/****************************************************************************/
static int __init init(void)
{
	BUG_ON(rcu_dereference(nat_rtsp_channel_hook) != NULL);
	BUG_ON(rcu_dereference(nat_rtsp_channel2_hook) != NULL);
	BUG_ON(rcu_dereference(nat_rtsp_modify_port_hook) != NULL);
	BUG_ON(rcu_dereference(nat_rtsp_modify_port2_hook) != NULL);
	BUG_ON(rcu_dereference(nat_rtsp_modify_addr_hook) != NULL);
	rcu_assign_pointer(nat_rtsp_channel_hook, nat_rtsp_channel);
	rcu_assign_pointer(nat_rtsp_channel2_hook, nat_rtsp_channel2);
	rcu_assign_pointer(nat_rtsp_modify_port_hook, nat_rtsp_modify_port);
	rcu_assign_pointer(nat_rtsp_modify_port2_hook, nat_rtsp_modify_port2);
	rcu_assign_pointer(nat_rtsp_modify_addr_hook, nat_rtsp_modify_addr);

	DEBUGP("nf_nat_rtsp: init success\n");
	return 0;
}

/****************************************************************************/
static void __exit fini(void)
{
	rcu_assign_pointer(nat_rtsp_channel_hook, NULL);
	rcu_assign_pointer(nat_rtsp_channel2_hook, NULL);
	rcu_assign_pointer(nat_rtsp_modify_port_hook, NULL);
	rcu_assign_pointer(nat_rtsp_modify_port2_hook, NULL);
	rcu_assign_pointer(nat_rtsp_modify_addr_hook, NULL);
	synchronize_rcu();
}

/****************************************************************************/
module_init(init);
module_exit(fini);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("RTSP NAT helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_nat_rtsp");
