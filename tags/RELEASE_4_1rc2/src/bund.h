
/*
 * bund.h
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#ifndef _BUND_H_
#define _BUND_H_

#include "defs.h"
#include "ip.h"
#include "mp.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "chap.h"
#include "ccp.h"
#include "ecp.h"
#include "msg.h"
#include "auth.h"
#include "command.h"
#include <netgraph/ng_message.h>

/*
 * DEFINITIONS
 */

  #define BUND_MAX_SCRIPT	32

  /* Configuration options */
  enum {
    BUND_CONF_MULTILINK,	/* multi-link */
    BUND_CONF_SHORTSEQ,		/* multi-link short sequence numbers */
    BUND_CONF_IPCP,		/* IPCP */
    BUND_CONF_IPV6CP,		/* IPV6CP */
    BUND_CONF_COMPRESSION,	/* compression */
    BUND_CONF_ENCRYPTION,	/* encryption */
    BUND_CONF_CRYPT_REQD,	/* encryption is required */
    BUND_CONF_BWMANAGE,		/* dynamic bandwidth */
    BUND_CONF_ROUNDROBIN,	/* round-robin MP scheduling */
    BUND_CONF_NORETRY,		/* don't retry failed links */
  };

  /* Default bundle-layer FSM retry timeout */
  #define BUND_DEFAULT_RETRY	2

  enum {
    NCP_NONE = 0,
    NCP_IPCP,
    NCP_IPV6CP,
    NCP_ECP,
    NCP_CCP
  };

/*

  Bundle bandwidth management

  We treat the first link as different from the rest. It connects
  immediately when there is (qualifying) outgoing traffic. The
  idle timeout applies globally, no matter how many links are up.

  Additional links are connected/disconnected according to a simple
  algorithm that uses the following constants:

  S	Sampling interval. Number of seconds over which we average traffic.

  N	Number of sub-intervals we chop the S seconds into (granularity). 

  Hi	Hi water mark: if traffic is more than H% of total available
	bandwidth, averaged over S seconds, time to add the second link.

  Lo	Low water mark: if traffic is less than L% of total available
	bandwidth during all N sub-intervals, time to hang up the second link.

  Mc	Minimum amount of time after connecting a link before
	connecting next.

  Md	Minimum amount of time after disconnecting any link before
	disconnecting next.

  We treat incoming and outgoing traffic separately when comparing
  against Hi and Lo.

*/

  #define BUND_BM_DFL_S		60	/* Length of sampling interval (secs) */
  #define BUND_BM_DFL_Hi	80	/* High water mark % */
  #define BUND_BM_DFL_Lo	20	/* Low water mark % */
  #define BUND_BM_DFL_Mc	30	/* Min connect period (secs) */
  #define BUND_BM_DFL_Md	90	/* Min disconnect period (secs) */

  struct bundbm {
    short		n_up;		/* Number of links in NETWORK phase */
    short		n_open;		/* Number of links in an OPEN state */
    time_t		last_open;	/* Time we last open any link */
    time_t		last_close;	/* Time we last closed any link */
    struct pppTimer	bmTimer;	/* Bandwidth mgmt timer */
    u_char		links_open:1;	/* One or more links told to open */
    u_int		total_bw;	/* Total bandwidth available */
  };
  typedef struct bundbm	*BundBm;

  /* Configuration for a bundle */
  struct bundconf {
    int			mrru;			/* Initial MRU value */
    short		retry_timeout;		/* Timeout for retries */
    u_short		bm_S;			/* Bandwidth mgmt constants */
    u_short		bm_Hi;
    u_short		bm_Lo;
    u_short		bm_Mc;
    u_short		bm_Md;
    char		script[BUND_MAX_SCRIPT];/* Link change script */
    struct optinfo	options;		/* Configured options */
  };

  #define BUND_STATS_UPDATE_INTERVAL    65 * SECONDS

  /* internal 64 bit counters as workaround for the 32 bit 
   * limitation for ng_ppp_link_stat
   */
  struct bundstats {
	struct ng_ppp_link_stat
			oldStats;
	u_int64_t 	xmitFrames;	/* xmit frames on bundle */
	u_int64_t 	xmitOctets;	/* xmit octets on bundle */
	u_int64_t 	recvFrames;	/* recv frames on bundle */
	u_int64_t	recvOctets;	/* recv octets on bundle */
	u_int64_t 	badProtos;	/* frames rec'd with bogus protocol */
	u_int64_t 	runts;		/* Too short MP fragments */
	u_int64_t 	dupFragments;	/* MP frames with duplicate seq # */
	u_int64_t	dropFragments;	/* MP fragments we had to drop */
  };
  typedef struct bundstats *BundStats;

  /* Total state of a bundle */
  struct bundle {
    char		name[LINK_MAX_NAME];	/* Name of this bundle */
    char		session_id[AUTH_MAX_SESSIONID];	/* a uniq session-id */    
    MsgHandler		msgs;			/* Bundle events */
    char		interface[10];		/* Interface I'm using */
    short		n_links;		/* Number of links in bundle */
    int			csock;			/* Socket node control socket */
    int			dsock;			/* Socket node data socket */
    EventRef		ctrlEvent;		/* Socket node control event */
    EventRef		dataEvent;		/* Socket node data event */
    ng_ID_t		nodeID;			/* ID of ppp node */
    Link		*links;			/* Real links in this bundle */
    struct discrim	peer_discrim;		/* Peer's discriminator */
    u_char		numRecordUp;		/* # links recorded up */

    /* PPP node config */
#if NGM_PPP_COOKIE < 940897794
    struct ng_ppp_node_config	pppConfig;
#else
    struct ng_ppp_node_conf	pppConfig;
#endif

    /* Data chunks */
    struct bundbm	bm;		/* Bandwidth management state */
    struct bundconf	conf;		/* Configuration for this bundle */
    struct bundstats	stats;		/* Statistics for this bundle */
    struct pppTimer     statsUpdateTimer;       /* update Timer */
    struct mpstate	mp;		/* MP state for this bundle */
    struct ifacestate	iface;		/* IP state info */
    struct ipcpstate	ipcp;		/* IPCP state info */
    struct ipv6cpstate	ipv6cp;		/* IPV6CP state info */
    struct ccpstate	ccp;		/* CCP state info */
    struct ecpstate	ecp;		/* ECP state info */
    u_int		ncpstarted;	/* Bitmask of active NCPs wich is sufficient to keep bundle open */

    /* Link management stuff */
    struct pppTimer	bmTimer;		/* Bandwidth mgmt timer */
    struct pppTimer	msgTimer;		/* Status message timer */
    struct pppTimer	reOpenTimer;		/* Re-open timer */

    /* Boolean variables */
    u_char		open:1;		/* In the open state */
    u_char		multilink:1;	/* Doing multi-link on this bundle */
    u_char		tee:1;		/* Bundle has ng_tee(4). */
    u_char		netflow:2;	/* Bundle connects to ng_netflow(4). */
    u_char		nat:1;		/* Bundle has to ng_nat(4). */
    #define NETFLOW_IN	1
    #define NETFLOW_OUT	2
    
    struct authparams   params;         /* params to pass to from auth backend */
  };
  
/*
 * VARIABLES
 */

  extern struct discrim		self_discrim;	/* My discriminator */
  extern const struct cmdtab	BundSetCmds[];

/*
 * FUNCTIONS
 */

  extern void	BundOpen(void);
  extern void	BundClose(void);
  extern int	BundStat(int ac, char *av[], void *arg);
  extern void	BundUpdateParams(void);
  extern int	BundCommand(int ac, char *av[], void *arg);
  extern int	BundCreateCmd(int ac, char *av[], void *arg);
  extern void   BundUpdateStats(void);
  extern void	BundUpdateStatsTimer(void *cookie);
  extern void	BundResetStats(void);

  extern int	BundJoin(void);
  extern void	BundLeave(void);
  extern void	BundNcpsJoin(int proto);
  extern void	BundNcpsLeave(int proto);
  extern void	BundNcpsStart(int proto);
  extern void	BundNcpsFinish(int proto);
  extern void	BundLinkGaveUp(void);
  extern void	BundOpenLinks(void);
  extern void	BundCloseLinks(void);
  extern void	BundOpenLink(Link);

  extern void	BundNcpsOpen(void);
  extern void	BundNcpsClose(void);

#endif
