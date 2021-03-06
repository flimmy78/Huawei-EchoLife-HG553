/*
 *  ebt_ip
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 *  Changes:
 *    added ip-sport and ip-dport
 *    Innominate Security Technologies AG <mhopf@innominate.com>
 *    September, 2002
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_ip.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/in.h>
#include <linux/module.h>

struct tcpudphdr {
	__be16 src;
	__be16 dst;
};

static int ebt_filter_ip(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data,
   unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;
	struct iphdr _iph, *ih;
	struct tcpudphdr _ports, *pptr;
#ifdef CONFIG_BRIDGE_EBT_QINQ
    /* Start of macfilter support QinQ by c47036 20060714 */
    unsigned char* p8021q = skb->data;
    int ipoffset = 0;
    if (skb->protocol == __constant_htons(ETH_P_8021Q))
    {
        p8021q += 2;
        ipoffset = p8021q - skb->data;

        if (*(unsigned short*)p8021q == __constant_htons(ETH_P_PPP_SES))
        {
            if (*(unsigned short*)(p8021q + 2 + 6) == __constant_htons(0x0021))
            {
                p8021q += (2 + 6 + 2); // VLAN+PPP+IP
                ipoffset = p8021q - skb->data;
            }
            else
            {
                // �������
		        return EBT_NOMATCH;
            }
        }
        else if (*(unsigned short*)p8021q == __constant_htons(ETH_P_IP))
        {
            p8021q += 2; // VLAN+IP
            ipoffset = p8021q - skb->data;
        }
        else
        {
	        return EBT_NOMATCH;
        }
    }
    else if(skb->protocol == __constant_htons(ETH_P_PPP_SES))
    {
        if(*(unsigned short*)(p8021q + 6) == __constant_htons(0x0021))
        {
            p8021q += (6 + 2); // PPP+IP
            ipoffset = p8021q - skb->data;
        }
        else
        {
	        return EBT_NOMATCH;
        }
    }
    else if(skb->protocol == __constant_htons(ETH_P_IP))
    {
        ipoffset = 0;
    }
    else
    {
        return EBT_NOMATCH;
    }
	ih = skb_header_pointer(skb, ipoffset, sizeof(_iph), &_iph);
    /* End of macfilter support QinQ by c47036 20060714 */
#else
	ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
#endif
	if (ih == NULL)
		return EBT_NOMATCH;
    /* dscp check */
#if 0//def CONFIG_BRIDGE_EBT_DSCP    
	if (info->bitmask & EBT_IP_TOS &&
	   FWINV(info->tos != (ih->tos & 0xFC), EBT_IP_TOS))
		return EBT_NOMATCH;
#else
	if (info->bitmask & EBT_IP_TOS &&
	   FWINV(info->tos != ih->tos, EBT_IP_TOS))
		return EBT_NOMATCH;
#endif
	if (info->bitmask & EBT_IP_SOURCE &&
	   FWINV((ih->saddr & info->smsk) !=
	   info->saddr, EBT_IP_SOURCE))
		return EBT_NOMATCH;
	if ((info->bitmask & EBT_IP_DEST) &&
	   FWINV((ih->daddr & info->dmsk) !=
	   info->daddr, EBT_IP_DEST))
		return EBT_NOMATCH;

    //ip range
    if (info->bitmask & EBT_IPRANGE_SRC)
    {
        if (FWINV((ntohl(ih->saddr) < ntohl(info->src.min_ip)) 
                   || (ntohl(ih->saddr) > ntohl(info->src.max_ip)), 
                   EBT_IPRANGE_SRC))
        {
            return EBT_NOMATCH;
        }
    }
    if (info->bitmask & EBT_IPRANGE_DST)
    {
        if (FWINV((ntohl(ih->daddr) < ntohl(info->dst.min_ip))
                    || (ntohl(ih->daddr) > ntohl(info->dst.max_ip)),
                    EBT_IPRANGE_DST))
        {
            return EBT_NOMATCH;
        }
    }
    
	if (info->bitmask & EBT_IP_PROTO) {
		if (FWINV(info->protocol != ih->protocol, EBT_IP_PROTO))
			return EBT_NOMATCH;
		if (!(info->bitmask & EBT_IP_DPORT) &&
		    !(info->bitmask & EBT_IP_SPORT))
			return EBT_MATCH;
		if (ntohs(ih->frag_off) & IP_OFFSET)
			return EBT_NOMATCH;
#ifdef CONFIG_BRIDGE_EBT_QINQ        
		//support Q & QinQ by c47036 20060714
		pptr = skb_header_pointer(skb, ipoffset + ih->ihl*4,
					  sizeof(_ports), &_ports);
#else
		pptr = skb_header_pointer(skb, ih->ihl*4,
					  sizeof(_ports), &_ports);
#endif		
		if (pptr == NULL)
			return EBT_NOMATCH;
		if (info->bitmask & EBT_IP_DPORT) {
			u32 dst = ntohs(pptr->dst);
			if (FWINV(dst < info->dport[0] ||
				  dst > info->dport[1],
				  EBT_IP_DPORT))
			return EBT_NOMATCH;
		}
		if (info->bitmask & EBT_IP_SPORT) {
			u32 src = ntohs(pptr->src);
			if (FWINV(src < info->sport[0] ||
				  src > info->sport[1],
				  EBT_IP_SPORT))
			return EBT_NOMATCH;
		}
	}
	return EBT_MATCH;
}

static int ebt_ip_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_ip_info)))
		return -EINVAL;
	if (e->ethproto != htons(ETH_P_IP) ||
	   e->invflags & EBT_IPROTO)
		return -EINVAL;
	if (info->bitmask & ~EBT_IP_MASK || info->invflags & ~EBT_IP_MASK)
		return -EINVAL;
	if (info->bitmask & (EBT_IP_DPORT | EBT_IP_SPORT)) {
		if (info->invflags & EBT_IP_PROTO)
			return -EINVAL;
		if (info->protocol != IPPROTO_TCP &&
		    info->protocol != IPPROTO_UDP &&
		    info->protocol != IPPROTO_UDPLITE &&
		    info->protocol != IPPROTO_SCTP &&
		    info->protocol != IPPROTO_DCCP)
			 return -EINVAL;
	}
	if (info->bitmask & EBT_IP_DPORT && info->dport[0] > info->dport[1])
		return -EINVAL;
	if (info->bitmask & EBT_IP_SPORT && info->sport[0] > info->sport[1])
		return -EINVAL;
	return 0;
}

static struct ebt_match filter_ip =
{
	.name		= EBT_IP_MATCH,
	.match		= ebt_filter_ip,
	.check		= ebt_ip_check,
	.me		= THIS_MODULE,
};

static int __init ebt_ip_init(void)
{
	return ebt_register_match(&filter_ip);
}

static void __exit ebt_ip_fini(void)
{
	ebt_unregister_match(&filter_ip);
}

module_init(ebt_ip_init);
module_exit(ebt_ip_fini);
MODULE_LICENSE("GPL");
