
/*
 *      Protocol initializer table. Here separately for convenience
 *
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/fs.h>

#ifdef  CONFIG_INET
#include <linux/inet.h>
#endif



struct net_proto protocols[] = {
#ifdef  CONFIG_UNIX
  { "UNIX",     unix_proto_init },                      /* Unix domain socket fa
mily    */
#endif
#if defined(CONFIG_IPX)   || defined(CONFIG_IPX_MODULE) || \
    defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)
  { "802.2",    p8022_proto_init },                     /* 802.2 demultiplexor
        */
  { "802.2TR",  p8022tr_proto_init },                   /* 802.2 demultiplexor
        */
  { "SNAP",     snap_proto_init },                      /* SNAP demultiplexor
        */
#endif
#ifdef CONFIG_TR
  { "RIF",      rif_init },                             /* RIF for Token ring
        */
#endif
#ifdef CONFIG_AX25
  { "AX.25",    ax25_proto_init },
#ifdef CONFIG_NETROM
  { "NET/ROM",  nr_proto_init },
#endif
#endif
#ifdef  CONFIG_INET
  { "INET",     inet_proto_init },                      /* TCP/IP
        */
#endif
#ifdef  CONFIG_IPX
  { "IPX",      ipx_proto_init },                       /* IPX
        */
#endif
#ifdef CONFIG_ATALK
  { "DDP",      atalk_proto_init },                     /* Netatalk Appletalk dr
iver    */
#endif
  { NULL,       NULL            }                       /* End marker
        */
};

