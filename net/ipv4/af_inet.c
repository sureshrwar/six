
/*
 * INET         An implementation of the TCP/IP protocol suite for the LINUX
 *              operating system.  INET is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              AF_INET protocol family socket handler.
 *
 * Version:     @(#)af_inet.c   (from sock.c) 1.0.17    06/02/93
 *
 * Authors:     Ross Biro, <bir7@leland.Stanford.Edu>
 *              Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *              Florian La Roche, <flla@stud.uni-sb.de>
 *              Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Changes (see also sock.c)
 *
 *              A.N.Kuznetsov   :       Socket death error in accept().
 *              John Richardson :       Fix non blocking error in connect()
 *                                      so sockets that fail to connect
 *                                      don't return -EINPROGRESS.
 *              Alan Cox        :       Asynchronous I/O support
 *              Alan Cox        :       Keep correct socket pointer on sock stru
ctures
 *                                      when accept() ed
 *              Alan Cox        :       Semantics of SO_LINGER aren't state move
d
 *                                      to close when you look carefully. With
 *                                      this fixed and the accept bug fixed
 *                                      some RPC stuff seems happier.
 *              Niibe Yutaka    :       4.4BSD style write async I/O
 *              Alan Cox,
 *              Tony Gale       :       Fixed reuse semantics.
 *              Alan Cox        :       bind() shouldn't abort existing but dead
 *                                      sockets. Stops FTP netin:.. I hope.
 *              Alan Cox        :       bind() works correctly for RAW sockets.
Note
 *                                      that FreeBSD at least was broken in this
 respect
 *                                      so be careful with compatibility tests..
.
 *              Alan Cox        :       routing cache support
 *              Alan Cox        :       memzero the socket structure for compact
ness.
 *              Matt Day        :       nonblock connect error handler
 *              Alan Cox        :       Allow large numbers of pending sockets
 *                                      (eg for big web sites), but only if
 *                                      specifically application requested.
 *              Alan Cox        :       New buffering throughout IP. Used dumbly
.
 *              Alan Cox        :       New buffering now used smartly.
 *              Alan Cox        :       BSD rather than common sense interpretat
ion of
 *                                      listen.
*              Germano Caronni :       Assorted small races.
 *              Alan Cox        :       sendmsg/recvmsg basic support.
 *              Alan Cox        :       Only sendmsg/recvmsg now supported.
 *              Alan Cox        :       Locked down bind (see security list).
 *              Alan Cox        :       Loosened bind a little.
 *              Mike McLagan    :       ADD/DEL DLCI Ioctls
 *      Willy Konynenberg       :       Transparent proxying support.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/rarp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#ifdef CONFIG_IP_ALIAS
#include <net/ip_alias.h>
#endif
#ifdef CONFIG_BRIDGE
#include <net/br.h>
#endif
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif


#define min(a,b)        ((a)<(b)?(a):(b))

/*
 *      Create an inet socket.
 *
 *      FIXME: Gcc would generate much better code if we set the parameters
 *      up in in-memory structure order. Gcc68K even more so
 */

static int inet_create(struct socket *sock, int protocol)
{

}

/*
 *      Duplicate a socket.
 */

static int inet_dup(struct socket *newsock, struct socket *oldsock)
{
     //return(inet_create(newsock,((struct sock *)(oldsock->data))->protocol));     
}

/*
 *      The peer socket should always be NULL (or else). When we call this
 *      function we are destroying the object and from then on nobody
 *      should refer to it.
 */

static int inet_release(struct socket *sock, struct socket *peer)
{

}

static int inet_bind(struct socket *sock, struct sockaddr *uaddr,
               int addr_len)
{

}

/*
 *      Connect to a remote host. There is regrettably still a little
 *      TCP 'magic' in here.
 */

static int inet_connect(struct socket *sock, struct sockaddr * uaddr,
                  int addr_len, int flags)
{

}


static int inet_socketpair(struct socket *sock1, struct socket *sock2)
{
         return(-EOPNOTSUPP);
}


/*
 *      Accept a pending connection. The TCP layer now gives BSD semantics.
 */

static int inet_accept(struct socket *sock, struct socket *newsock, int flags)
{

}

/*
 *      This does both peername and sockname.
 */

static int inet_getname(struct socket *sock, struct sockaddr *uaddr,
                 int *uaddr_len, int peer)
{

}

static int inet_select(struct socket *sock, int sel_type, select_table *wait )
{

}

/*
 *      ioctl() calls you can issue on an INET socket. Most of these are
 *      device configuration and stuff and very rarely used. Some ioctls
 *      pass on to the socket itself.
 *
 *      NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *      loads the devconfigure module does its configuring and unloads it.
 *      There's a good 20K of config code hanging around the kernel.
 */

static int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{

}

/*
 *      Move a socket into listening state.
 */

static int inet_listen(struct socket *sock, int backlog)
{

}

static int inet_shutdown(struct socket *sock, int how)
{

}


/*
 *      Set socket options on an inet socket.
 */

static int inet_setsockopt(struct socket *sock, int level, int optname,
                    char *optval, int optlen)
{

}

/*
 *      Get a socket option on an AF_INET socket.
 */

static int inet_getsockopt(struct socket *sock, int level, int optname,
                    char *optval, int *optlen)
{

}

/*
 *      The routines beyond this point handle the behaviour of an AF_INET
 *      socket object. Mostly it punts to the subprotocols of IP to do
 *      the work.
 */

static int inet_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{

}

static int inet_sendmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags)
{
  
}

static int inet_recvmsg(struct socket *sock, struct msghdr *ubuf, int size, int noblock, int flags, int *addr_len )
{

}

static struct proto_ops inet_proto_ops = {
        AF_INET,
        inet_create,
        inet_dup,
        inet_release,
        inet_bind,
        inet_connect,
        inet_socketpair,
        inet_accept,
        inet_getname,
        inet_select,
        inet_ioctl,
        inet_listen,
        inet_shutdown,
        inet_setsockopt,
        inet_getsockopt,
        inet_fcntl,
        inet_sendmsg,
        inet_recvmsg
};


void inet_proto_init(struct net_proto *pro)
{

	printk("Swansea University Computer Society TCP/IP for NET3.034\n");

}
