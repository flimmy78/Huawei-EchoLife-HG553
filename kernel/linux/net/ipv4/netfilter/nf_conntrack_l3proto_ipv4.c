/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- move L3 protocol dependent part to this file.
 * 23 Mar 2004: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- add get_features() to support various size of conntrack
 *	  structures.
 *
 * Derived from net/ipv4/netfilter/ip_conntrack_standalone.c
 */

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>

#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/*start of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */
int rtsp_enable = 1; // default: enable h323 alg.
int pptp_enable = 1; // default: enable pptp alg.
int h323_enable = 1; // default: enable h323 alg.
/*e n d of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */

#if defined(CONFIG_IMQ) || defined(CONFIG_IMQ_MODULE)
int qos_enable = 0;
EXPORT_SYMBOL(qos_enable);
#endif

static int ipv4_pkt_to_tuple(const struct sk_buff *skb, unsigned int nhoff,
			     struct nf_conntrack_tuple *tuple)
{
	__be32 _addrs[2], *ap;
	ap = skb_header_pointer(skb, nhoff + offsetof(struct iphdr, saddr),
				sizeof(u_int32_t) * 2, _addrs);
	if (ap == NULL)
		return 0;

	tuple->src.u3.ip = ap[0];
	tuple->dst.u3.ip = ap[1];

	return 1;
}

static int ipv4_invert_tuple(struct nf_conntrack_tuple *tuple,
			   const struct nf_conntrack_tuple *orig)
{
	tuple->src.u3.ip = orig->dst.u3.ip;
	tuple->dst.u3.ip = orig->src.u3.ip;

	return 1;
}

static int ipv4_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "src=%u.%u.%u.%u dst=%u.%u.%u.%u ",
			  NIPQUAD(tuple->src.u3.ip),
			  NIPQUAD(tuple->dst.u3.ip));
}

static int ipv4_print_conntrack(struct seq_file *s,
				const struct nf_conn *conntrack)
{
	return 0;
}

/* Returns new sk_buff, or NULL */
static struct sk_buff *
nf_ct_ipv4_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	skb_orphan(skb);

	local_bh_disable();
	skb = ip_defrag(skb, user);
	local_bh_enable();

	if (skb)
		ip_send_check(skb->nh.iph);

	return skb;
}

static int
ipv4_prepare(struct sk_buff **pskb, unsigned int hooknum, unsigned int *dataoff,
	     u_int8_t *protonum)
{
	/* Never happen */
	if ((*pskb)->nh.iph->frag_off & htons(IP_OFFSET)) {
		if (net_ratelimit()) {
			printk(KERN_ERR "ipv4_prepare: Frag of proto %u (hook=%u)\n",
			(*pskb)->nh.iph->protocol, hooknum);
		}
		return -NF_DROP;
	}

	*dataoff = (*pskb)->nh.raw - (*pskb)->data + (*pskb)->nh.iph->ihl*4;
	*protonum = (*pskb)->nh.iph->protocol;

	return NF_ACCEPT;
}

int nf_nat_module_is_loaded = 0;
EXPORT_SYMBOL_GPL(nf_nat_module_is_loaded);

static u_int32_t ipv4_get_features(const struct nf_conntrack_tuple *tuple)
{
	if (nf_nat_module_is_loaded)
		return NF_CT_F_NAT;

	return NF_CT_F_BASIC;
}

static unsigned int ipv4_confirm(unsigned int hooknum,
				 struct sk_buff **pskb,
				 const struct net_device *in,
				 const struct net_device *out,
				 int (*okfn)(struct sk_buff *))
{
/* start of ATP liliang67187 2008-1-91 for cone nat */
#ifdef CONFIG_ATP_CONENAT
    struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	/* dir = ����  or  ���� */
	int dir;
	struct nf_conn_help *help;

	ct = nf_ct_get(*pskb, &ctinfo);
	dir = CTINFO2DIR(ctinfo);
	if (ct && ctinfo != IP_CT_ESTABLISHED && dir == IP_CT_DIR_ORIGINAL)
	{
    	help = nfct_help(ct);
    	if (help && help->helper && help->helper->help2)
        {
		    unsigned int ret;
		    ret = help->helper->help2(pskb, 
		                              (*pskb)->nh.raw - (*pskb)->data + (*pskb)->nh.iph->ihl*4, 
		                              ct, ctinfo);
		    if (ret != NF_ACCEPT)
			    return ret;
    	}
	}
#endif
/* end for cone nat */
	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(pskb);
}

static unsigned int ipv4_conntrack_help(unsigned int hooknum,
				      struct sk_buff **pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_help *help;

	/* This is where we call the helper: as the packet goes out. */
	ct = nf_ct_get(*pskb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED + IP_CT_IS_REPLY)
		return NF_ACCEPT;

	help = nfct_help(ct);
/* start of ATP liliang67187 2008-1-91 for cone nat */
#ifdef CONFIG_ATP_CONENAT
	if (!help || !(help->helper) || !(help->helper->help))
#else
	if (!help || !help->helper)
#endif
/* end of ATP liliang67187 2008-1-91 for cone nat */
	{
		return NF_ACCEPT;
    }
	return help->helper->help(pskb,
			       (*pskb)->nh.raw - (*pskb)->data
					       + (*pskb)->nh.iph->ihl*4,
			       ct, ctinfo);
}

static unsigned int ipv4_conntrack_defrag(unsigned int hooknum,
					  struct sk_buff **pskb,
					  const struct net_device *in,
					  const struct net_device *out,
					  int (*okfn)(struct sk_buff *))
{
#if !defined(CONFIG_IP_NF_NAT) && !defined(CONFIG_IP_NF_NAT_MODULE)
	/* Previously seen (loopback)?  Ignore.  Do this before
	   fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;
#endif

	/* Gather fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = nf_ct_ipv4_gather_frags(*pskb,
						hooknum == NF_IP_PRE_ROUTING ?
						IP_DEFRAG_CONNTRACK_IN :
						IP_DEFRAG_CONNTRACK_OUT);
		if (!*pskb)
			return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static unsigned int ipv4_conntrack_in(unsigned int hooknum,
				      struct sk_buff **pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	return nf_conntrack_in(PF_INET, hooknum, pskb);
}

static unsigned int ipv4_conntrack_local(unsigned int hooknum,
					 struct sk_buff **pskb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
	return nf_conntrack_in(PF_INET, hooknum, pskb);
}


/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ipv4_conntrack_ops[] = {
	{
		.hook		= ipv4_conntrack_defrag,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv4_conntrack_in,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook           = ipv4_conntrack_defrag,
		.owner          = THIS_MODULE,
		.pf             = PF_INET,
		.hooknum        = NF_IP_LOCAL_OUT,
		.priority       = NF_IP_PRI_CONNTRACK_DEFRAG,
	},
	{
		.hook		= ipv4_conntrack_local,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= ipv4_conntrack_help,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv4_conntrack_help,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_HELPER,
	},
	{
		.hook		= ipv4_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
	{
		.hook		= ipv4_confirm,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

/*start of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */
extern int rtsp_enable;
extern int pptp_enable;
extern int h323_enable;
/*e n d of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */

#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;


static ctl_table ip_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_COUNT,
		.procname	= "ip_conntrack_count",
		.data		= &nf_conntrack_count,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_BUCKETS,
		.procname	= "ip_conntrack_buckets",
		.data		= &nf_conntrack_htable_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_CHECKSUM,
		.procname	= "ip_conntrack_checksum",
		.data		= &nf_conntrack_checksum,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_LOG_INVALID,
		.procname	= "ip_conntrack_log_invalid",
		.data		= &nf_ct_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
#ifdef CONFIG_ATP_CONNTRACK_CLEAN
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_CLEAN,
		.procname	= "ip_ct_clean",
		.data		= &nf_conntrack_clean,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CLEAN_DNS,
		.procname	= "ip_conntrack_dns",
		.data		= &nf_conntrack_clean,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
    /*start of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */
    {
        .ctl_name   = NET_IPV4_NF_CONNTRACK_RTSP_ENABLE,
        .procname   = "rtsp_enable",
        .data       = &rtsp_enable,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler   = &proc_dointvec,
    },  
    {
        .ctl_name   = NET_IPV4_NF_CONNTRACK_PPTP_ENABLE,
        .procname   = "pptp_enable",
        .data       = &pptp_enable,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler   = &proc_dointvec,
    },  
    {
        .ctl_name   = NET_IPV4_NF_CONNTRACK_H323_ENABLE,
        .procname   = "h323_enable",
        .data       = &h323_enable,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler   = &proc_dointvec,
    },  
    /*e n d of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */
#if defined(CONFIG_IMQ) || defined(CONFIG_IMQ_MODULE)
    {
        .ctl_name   = NET_IPV4_NF_QOS_SWITCH,
        .procname   = "qos_enable",
        .data       = &qos_enable,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler   = &proc_dointvec,
    }, 
#endif
	{
		.ctl_name	= 0
	}
};
#endif /* CONFIG_SYSCTL && CONFIG_NF_CONNTRACK_PROC_COMPAT */

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;

	NF_CT_TUPLE_U_BLANK(&tuple);
	tuple.src.u3.ip = inet->rcv_saddr;
	tuple.src.u.tcp.port = inet->sport;
	tuple.dst.u3.ip = inet->daddr;
	tuple.dst.u.tcp.port = inet->dport;
	tuple.src.l3num = PF_INET;
	tuple.dst.protonum = IPPROTO_TCP;

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->sk_prot->name, "TCP")) {
		DEBUGP("SO_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		DEBUGP("SO_ORIGINAL_DST: len %u not %u\n",
		       *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = nf_conntrack_find_get(&tuple, NULL);
	if (h) {
		struct sockaddr_in sin;
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		sin.sin_family = AF_INET;
		sin.sin_port = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u3.ip;
		memset(sin.sin_zero, 0, sizeof(sin.sin_zero));

		DEBUGP("SO_ORIGINAL_DST: %u.%u.%u.%u %u\n",
		       NIPQUAD(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		nf_ct_put(ct);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	DEBUGP("SO_ORIGINAL_DST: Can't find %u.%u.%u.%u/%u-%u.%u.%u.%u/%u.\n",
	       NIPQUAD(tuple.src.u3.ip), ntohs(tuple.src.u.tcp.port),
	       NIPQUAD(tuple.dst.u3.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int ipv4_tuple_to_nfattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	NFA_PUT(skb, CTA_IP_V4_SRC, sizeof(u_int32_t),
		&tuple->src.u3.ip);
	NFA_PUT(skb, CTA_IP_V4_DST, sizeof(u_int32_t),
		&tuple->dst.u3.ip);
	return 0;

nfattr_failure:
	return -1;
}

static const size_t cta_min_ip[CTA_IP_MAX] = {
	[CTA_IP_V4_SRC-1]       = sizeof(u_int32_t),
	[CTA_IP_V4_DST-1]       = sizeof(u_int32_t),
};

static int ipv4_nfattr_to_tuple(struct nfattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V4_SRC-1] || !tb[CTA_IP_V4_DST-1])
		return -EINVAL;

	if (nfattr_bad_size(tb, CTA_IP_MAX, cta_min_ip))
		return -EINVAL;

	t->src.u3.ip = *(__be32 *)NFA_DATA(tb[CTA_IP_V4_SRC-1]);
	t->dst.u3.ip = *(__be32 *)NFA_DATA(tb[CTA_IP_V4_DST-1]);

	return 0;
}
#endif

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET,
	.get_optmin	= SO_ORIGINAL_DST,
	.get_optmax	= SO_ORIGINAL_DST+1,
	.get		= &getorigdst,
};

struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv4 = {
	.l3proto	 = PF_INET,
	.name		 = "ipv4",
	.pkt_to_tuple	 = ipv4_pkt_to_tuple,
	.invert_tuple	 = ipv4_invert_tuple,
	.print_tuple	 = ipv4_print_tuple,
	.print_conntrack = ipv4_print_conntrack,
	.prepare	 = ipv4_prepare,
	.get_features	 = ipv4_get_features,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr = ipv4_tuple_to_nfattr,
	.nfattr_to_tuple = ipv4_nfattr_to_tuple,
#endif
#if defined(CONFIG_SYSCTL) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	.ctl_table_path  = nf_net_ipv4_netfilter_sysctl_path,
	.ctl_table	 = ip_ct_sysctl_table,
#endif
	.me		 = THIS_MODULE,
};

MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET));
MODULE_ALIAS("ip_conntrack");
MODULE_LICENSE("GPL");

static int __init nf_conntrack_l3proto_ipv4_init(void)
{
	int ret = 0;

	need_conntrack();

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register netfilter socket option\n");
		return ret;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_tcp4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register tcp.\n");
		goto cleanup_sockopt;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_udp4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register udp.\n");
		goto cleanup_tcp;
	}

	ret = nf_conntrack_l4proto_register(&nf_conntrack_l4proto_icmp);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register icmp.\n");
		goto cleanup_udp;
	}

	ret = nf_conntrack_l3proto_register(&nf_conntrack_l3proto_ipv4);
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register ipv4\n");
		goto cleanup_icmp;
	}

	ret = nf_register_hooks(ipv4_conntrack_ops,
				ARRAY_SIZE(ipv4_conntrack_ops));
	if (ret < 0) {
		printk("nf_conntrack_ipv4: can't register hooks.\n");
		goto cleanup_ipv4;
	}
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	ret = nf_conntrack_ipv4_compat_init();
	if (ret < 0)
		goto cleanup_hooks;
#endif
	return ret;
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
 cleanup_hooks:
	nf_unregister_hooks(ipv4_conntrack_ops, ARRAY_SIZE(ipv4_conntrack_ops));
#endif
 cleanup_ipv4:
	nf_conntrack_l3proto_unregister(&nf_conntrack_l3proto_ipv4);
 cleanup_icmp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_icmp);
 cleanup_udp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udp4);
 cleanup_tcp:
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_tcp4);
 cleanup_sockopt:
	nf_unregister_sockopt(&so_getorigdst);
	return ret;
}

static void __exit nf_conntrack_l3proto_ipv4_fini(void)
{
	synchronize_net();
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NF_CONNTRACK_PROC_COMPAT)
	nf_conntrack_ipv4_compat_fini();
#endif
	nf_unregister_hooks(ipv4_conntrack_ops, ARRAY_SIZE(ipv4_conntrack_ops));
	nf_conntrack_l3proto_unregister(&nf_conntrack_l3proto_ipv4);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_icmp);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_udp4);
	nf_conntrack_l4proto_unregister(&nf_conntrack_l4proto_tcp4);
	nf_unregister_sockopt(&so_getorigdst);
}

/*start of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */
EXPORT_SYMBOL(rtsp_enable); 
EXPORT_SYMBOL(pptp_enable); 
EXPORT_SYMBOL(h323_enable); 
/*e n d of HGW by d37981  2009.4.17 HG522V100R001C08B115  A36D06508 */

module_init(nf_conntrack_l3proto_ipv4_init);
module_exit(nf_conntrack_l3proto_ipv4_fini);
