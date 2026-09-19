
/* $Id: isdn.h,v 1.1.1.1 2005/03/30 08:40:49 motorbreathing Exp $
 *
 * Main header for the Linux ISDN subsystem (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: isdn.h,v $
 * Revision 1.1.1.1  2005/03/30 08:40:49  motorbreathing
 * Initial import.
 *
 * Revision 1.3  2005/02/15 02:57:41  warrier
 * *** empty log message ***
 *
 * Revision 1.2  2005/02/08 03:46:16  warrier
 * added license comments
 *
 * Revision 1.1.1.1  2005/02/03 05:17:51  warrier
 * SIX Source code
 *
 *
 * Revision 1.1.1.1  2005/02/03 04:20:03  warrier
 * SIX source code
 *
 *
 * Revision 1.1.1.1  2002/09/17 02:34:12  warrier
 * full six
 *
 * Revision 1.1.1.1  2002/09/10 03:23:24  suresh
 * ver 1
 *
 * Revision 1.15  1996/06/15 14:56:57  fritz
 * Added version signatures for data structures used
 * by userlevel programs.
 *
 * Revision 1.14  1996/06/06 21:24:23  fritz
 * Started adding support for suspend/resume.
 *
 * Revision 1.13  1996/06/05 02:18:20  fritz
 * Added DTMF decoding stuff.
 *
 * Revision 1.12  1996/06/03 19:55:08  fritz
 * Fixed typos.
 *
 * Revision 1.11  1996/05/31 01:37:47  fritz
 * Minor changes, due to changes in isdn_tty.c
 *
 * Revision 1.10  1996/05/18 01:37:18  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.9  1996/05/17 03:58:20  fritz
 * Added flags for DLE handling.
 *
 * Revision 1.8  1996/05/11 21:49:55  fritz
 * Removed queue management variables.
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.7  1996/05/07 09:10:06  fritz
 * Reorganized tty-related structs.
 *
*
 * Revision 1.6  1996/05/06 11:38:27  hipp
 * minor change in ippp struct
 *
 * Revision 1.5  1996/04/30 11:03:16  fritz
 * Added Michael's ippp-bind patch.
 *
 * Revision 1.4  1996/04/29 23:00:02  fritz
 * Added variables for voice-support.
 *
 * Revision 1.3  1996/04/20 16:54:58  fritz
 * Increased maximum number of channels.
 * Added some flags for isdn_net to handle callback more reliable.
 * Fixed delay-definitions to be more accurate.
 * Misc. typos
 *
 * Revision 1.2  1996/02/11 02:10:02  fritz
 * Changed IOCTL-names
 * Added rx_netdev, st_netdev, first_skb, org_hcb, and org_hcu to
 * Netdevice-local struct.
 *
 * Revision 1.1  1996/01/10 20:55:07  fritz
 * Initial revision
 *
 */

#ifndef isdn_h
#define isdn_h


#include <linux/ioctl.h>

#define ISDN_TTY_MAJOR    43
#define ISDN_TTYAUX_MAJOR 44
#define ISDN_MAJOR        45

/* The minor-devicenumbers for Channel 0 and 1 are used as arguments for
 * physical Channel-Mapping, so they MUST NOT be changed without changing
 * the correspondent code in isdn.c
 */

#define ISDN_MAX_DRIVERS    32
#define ISDN_MAX_CHANNELS   64
#define ISDN_MINOR_B        0
#define ISDN_MINOR_BMAX     (ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_CTRL     ISDN_MAX_CHANNELS
#define ISDN_MINOR_CTRLMAX  (2*ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_PPP      (2*ISDN_MAX_CHANNELS)
#define ISDN_MINOR_PPPMAX   (3*ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_STATUS   255

/* Utility-Macros */
#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)


#endif
