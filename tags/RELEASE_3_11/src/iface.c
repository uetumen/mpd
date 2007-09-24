
/*
 * iface.c
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#include "ppp.h"
#include "iface.h"
#include "ipcp.h"
#include "auth.h"
#include "custom.h"
#include "ngfunc.h"
#include "netgraph.h"
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_iface.h>
#include <netgraph/ng_bpf.h>

/*
 * DEFINITIONS
 */

  #define MAX_INTERFACES	32
  #define IFACE_MIN_MTU		296
  #define IFACE_MAX_MTU		65536

/* Set menu options */

  enum {
    SET_IDLE,
    SET_ADDRS,
    SET_ROUTE,
    SET_MTU,
    SET_UP_SCRIPT,
    SET_DOWN_SCRIPT,
    SET_ENABLE,
    SET_DISABLE,
  };

/* Configuration options */

  enum {
    IFACE_CONF_ONDEMAND,
    IFACE_CONF_PROXY,
  };

/*
 * INTERNAL FUNCTIONS
 */

  static int	IfaceSetCommand(int ac, char *av[], void *arg);
  static void	IfaceIpIfaceUp(int ready);
  static void	IfaceIpIfaceDown(void);
  static void	IfaceIpIfaceReady(int ready);
  static void	IfaceIdleTimeout(void *arg);
  static void	IfaceIdleTimerExpired(void *arg);

  static void	IfaceCacheSend(void);
  static void	IfaceCachePkt(int proto, Mbuf pkt);
  static int	IfaceIsDemand(int proto, Mbuf pkt);

/*
 * GLOBAL VARIABLES
 */

  const struct cmdtab IfaceSetCmds[] = {
    { "addrs self peer",		"Set interface addresses",
	IfaceSetCommand, NULL, (void *) SET_ADDRS },
    { "route dest[/width]",		"Add IP route",
	IfaceSetCommand, NULL, (void *) SET_ROUTE },
    { "mtu size",			"Set max allowed interface MTU",
	IfaceSetCommand, NULL, (void *) SET_MTU },
    { "up-script [progname]",		"Interface up script",
	IfaceSetCommand, NULL, (void *) SET_UP_SCRIPT },
    { "down-script [progname]",		"Interface down script",
	IfaceSetCommand, NULL, (void *) SET_DOWN_SCRIPT },
    { "idle seconds",			"Idle timeout",
	IfaceSetCommand, NULL, (void *) SET_IDLE },
    { "enable [opt ...]",		"Enable option",
	IfaceSetCommand, NULL, (void *) SET_ENABLE },
    { "disable [opt ...]",		"Disable option",
	IfaceSetCommand, NULL, (void *) SET_DISABLE },
    { NULL },
  };

/*
 * INTERNAL VARIABLES
 */

  static const struct confinfo	gConfList[] = {
    { 0,	IFACE_CONF_ONDEMAND,	"on-demand"	},
    { 0,	IFACE_CONF_PROXY,	"proxy-arp"	},
    { 0,	0,			NULL		},
  };

/*
 * IfaceInit()
 */

void
IfaceInit(void)
{
  IfaceState	const iface = &bund->iface;

  /* Default configuration */
  iface->mtu = NG_IFACE_MTU_DEFAULT;
  iface->max_mtu = NG_IFACE_MTU_DEFAULT;
  Disable(&iface->options, IFACE_CONF_ONDEMAND);
  Disable(&iface->options, IFACE_CONF_PROXY);
  Log(LG_BUND|LG_IFACE, ("[%s] using interface %s",
    bund->name, bund->iface.ifname));
}

/*
 * IfaceOpen()
 *
 * Open the interface layer
 */

void
IfaceOpen(void)
{
  IfaceState	const iface = &bund->iface;

  Log(LG_IFACE, ("[%s] IFACE: Open event", bund->name));

  /* If interface is already open do nothing */
  if (iface->open)
    return;
  iface->open = TRUE;

  /* If on-demand, bring up system interface immediately and start
     listening for outgoing packets. The next outgoing packet will
     cause us to open the lower layer(s) */
  if (Enabled(&iface->options, IFACE_CONF_ONDEMAND)) {
    IfaceIpIfaceUp(0);
    NgFuncConfigBPF(bund, BPF_MODE_DEMAND);
    SetStatus(ADLG_WAN_WAIT_FOR_DEMAND, STR_READY_TO_DIAL);
    return;
  }

  /* Open lower layer(s) and wait for them to come up */
  IfaceOpenNcps();
}

/*
 * IfaceClose()
 *
 * Close the interface layer
 */

void
IfaceClose(void)
{
  IfaceState	const iface = &bund->iface;

  Log(LG_IFACE, ("[%s] IFACE: Close event", bund->name));

  /* If interface is already closed do nothing */
  if (!iface->open)
    return;
  iface->open = FALSE;

  /* Take down system interface */
  if (iface->ip_up) {
    NgFuncConfigBPF(bund, BPF_MODE_OFF);
    IfaceIpIfaceDown();
  }

  /* Close lower layer(s) */
  IfaceCloseNcps();
}

/*
 * IfaceUp()
 *
 * Our underlying PPP bundle is ready for traffic.
 * Note, while this assumes we're talking about IP traffic
 * here, in general a parameter could specify which type
 * of traffic, IP vs. AppleTalk vs. whatever, along with
 * additional protocol specific information (in this case,
 * the IP addresses of each end of the point-to-point link).
 */

void
IfaceUp(struct in_addr self, struct in_addr peer)
{
  IfaceState	const iface = &bund->iface;

  Log(LG_IFACE, ("[%s] IFACE: Up event", bund->name));
  SetStatus(ADLG_WAN_CONNECTED, STR_CONN_ESTAB);

  /* Open ourselves if necessary (we in effect slave off IPCP) */
  if (!iface->open) {
    Log(LG_IFACE, ("[%s] IFACE: Opening", bund->name));
    iface->open = TRUE;		/* Would call IfaceOpen(); effect is same */
  }

  /* Start idle timer */
  TimerStop(&iface->idleTimer);
  if (iface->idle_timeout > 0) {
    char	path[NG_PATHLEN + 1];

    TimerInit(&iface->idleTimer, "IfaceIdle",
      iface->idle_timeout * SECONDS / IFACE_IDLE_SPLIT, IfaceIdleTimeout, NULL);
    TimerStart(&iface->idleTimer);
    iface->traffic[1] = TRUE;
    iface->traffic[0] = FALSE;

    /* Reset bpf node statistics */
    memset(&iface->idleStats, 0, sizeof(iface->idleStats));
    snprintf(path, sizeof(path), "%s:%s", iface->ifname, NG_IFACE_HOOK_INET);
    if (NgSendMsg(bund->csock, path, NGM_BPF_COOKIE,
	NGM_BPF_CLR_STATS, BPF_HOOK_IFACE, sizeof(BPF_HOOK_IFACE)) < 0)
      Log(LG_ERR, ("[%s] can't clear %s stats: %s",
	bund->name, NG_BPF_NODE_TYPE, strerror(errno)));
  }

  /* (Re)number interface as necessary */
  if (!iface->ip_up
    || self.s_addr != iface->self_addr.s_addr
    || peer.s_addr != iface->peer_addr.s_addr) {

    /* Bring down interface if already up */
    if (iface->ip_up)
      IfaceIpIfaceDown();

    /* Bring up interface with new addresses */
    iface->self_addr = self;
    iface->peer_addr = peer;
    IfaceIpIfaceUp(1);
  } else {
    if (!iface->ready)
      IfaceIpIfaceReady(1);
  }

  /* Customization */
#ifdef IA_CUSTOM
  CustomIpIfaceUp(iface->self_addr, iface->peer_addr);
#endif

  /* Turn on interface traffic flow */
  NgFuncConfigBPF(bund, BPF_MODE_ON);

  /* Send any cached packets */
  IfaceCacheSend();
}

/*
 * IfaceDown()
 *
 * Our packet transport mechanism is no longer ready for traffic.
 */

void
IfaceDown(void)
{
  IfaceState	const iface = &bund->iface;

  Log(LG_IFACE, ("[%s] IFACE: Down event", bund->name));

  /* Customization */
#ifdef IA_CUSTOM
  CustomIpIfaceDown();
#endif

  /* If we're not open, it doesn't matter to us anyway */
  TimerStop(&iface->idleTimer);
  if (!iface->open)
    return;

  /* If dial-on-demand, this is OK; just listen for future demand */
  if (Enabled(&iface->options, IFACE_CONF_ONDEMAND)) {
    SetStatus(ADLG_WAN_WAIT_FOR_DEMAND, STR_READY_TO_DIAL);
    NgFuncConfigBPF(bund, BPF_MODE_DEMAND);
    IfaceIpIfaceReady(0);
    IfaceCloseNcps();
    return;
  }

  /* Take down system interface */
  if (iface->ip_up)
    IfaceIpIfaceDown();
  NgFuncConfigBPF(bund, BPF_MODE_OFF);
}

/*
 * IfaceListenInput()
 *
 * A packet was received on our demand snooping hook. Stimulate a connection.
 */

void
IfaceListenInput(int proto, Mbuf pkt)
{
  IfaceState	const iface = &bund->iface;
  int		const isDemand = IfaceIsDemand(proto, pkt);
  Fsm		fsm;

  /* Does this count as demand traffic? */
  if (isDemand)
    iface->traffic[0] = TRUE;

  /* Get FSM for protocol (for now, we know it's IP) */
  assert(proto == PROTO_IP);
  fsm = &bund->ipcp.fsm;

  /* Maybe do dial-on-demand here */
  if (OPEN_STATE(fsm->state)) {
    if (bund->bm.n_up > 0) {
      Log(LG_IFACE, ("[%s] unexpected outgoing packet, len=%d",
	bund->name, MBLEN(pkt)));
      NgFuncWriteFrame(bund->name, MPD_HOOK_DEMAND_TAP, pkt);
    } else {
      IfaceCachePkt(proto, pkt);
    }
  } else if (iface->open && isDemand) {
    Log(LG_IFACE, ("[%s] outgoing packet is demand", bund->name));
    RecordLinkUpDownReason(NULL, 1, STR_DEMAND, "%s", AsciifyPacket(pkt));
    IfaceOpenNcps();
    IfaceCachePkt(proto, pkt);
  } else {
    PFREE(pkt);
  }
}

/*
 * IfaceIpIfaceUp()
 *
 * Bring up the IP interface. The "ready" flag means that
 * IPCP is also up and we can deliver packets immediately. We signal
 * that the interface is not "ready" with the IFF_LINK0 flag.
 */

static void
IfaceIpIfaceUp(int ready)
{
  IfaceState		const iface = &bund->iface;
  int			k;
  struct sockaddr_dl	hwa;
  char			hisaddr[20];
  u_char		*ether;

  /* Sanity */
  assert(!iface->ip_up);

  /* For good measure */
  BundUpdateParams();

  /* Set addresses and bring interface up */
  snprintf(hisaddr, sizeof(hisaddr), "%s", inet_ntoa(iface->peer_addr));
  ExecCmd(LG_IFACE, "%s %s %s %s netmask 0xffffffff %slink0",
    PATH_IFCONFIG, iface->ifname, inet_ntoa(iface->self_addr), hisaddr,
    ready ? "-" : "");
  iface->ready = ready;

  /* Proxy ARP for peer if desired and peer's address is known */
  iface->proxy_addr.s_addr = 0;
  if (Enabled(&iface->options, IFACE_CONF_PROXY)) {
    if (iface->peer_addr.s_addr == 0) {
      Log(LG_IFACE,
	("[%s] can't proxy arp for %s",
	bund->name, inet_ntoa(iface->peer_addr)));
    } else if (IfaceGetEther(&iface->peer_addr, &hwa) < 0) {
      Log(LG_IFACE,
	("[%s] no interface to proxy arp on for %s",
	bund->name, inet_ntoa(iface->peer_addr)));
    } else {
      ether = (u_char *) LLADDR(&hwa);
      if (ExecCmd(LG_IFACE,
	  "%s -s %s %x:%x:%x:%x:%x:%x pub",
	  PATH_ARP, inet_ntoa(iface->peer_addr),
	  ether[0], ether[1], ether[2],
	  ether[3], ether[4], ether[5]) == 0)
	iface->proxy_addr = iface->peer_addr;
    }
  }

  /* Add routes */
  for (k = 0; k < iface->n_routes; k++) {
    IfaceRoute	const r = &iface->routes[k];
    char	nmbuf[40], peerbuf[40];

    if (r->netmask.s_addr) {
      snprintf(nmbuf, sizeof(nmbuf),
	" -netmask 0x%08lx", (u_long)ntohl(r->netmask.s_addr));
    } else
      *nmbuf = 0;
    snprintf(peerbuf, sizeof(peerbuf), "%s", inet_ntoa(iface->peer_addr));
    r->ok = (ExecCmd(LG_IFACE, "%s add %s %s%s",
      PATH_ROUTE, inet_ntoa(r->dest), peerbuf, nmbuf) == 0);
  }

  /* Call "up" script */
  if (*iface->up_script) {
    char	peerbuf[40];

    snprintf(peerbuf, sizeof(peerbuf), "%s", inet_ntoa(iface->peer_addr));
    ExecCmd(LG_IFACE, "%s %s inet %s %s %s",
      iface->up_script, iface->ifname, inet_ntoa(iface->self_addr),
      peerbuf, bund->peer_authname);
  }

  /* Done */
  iface->ip_up = TRUE;
}

/*
 * IfaceIpIfaceReady()
 *
 * (Un)set the interface IFF_LINK0 flag because IPCP is now up or down.
 * Call this when the addressing is already set correctly and you
 * just want to change the flag.
 */

static void
IfaceIpIfaceReady(int ready)
{
  IfaceState	const iface = &bund->iface;

  ExecCmd(LG_IFACE, "%s %s %slink0",
    PATH_IFCONFIG, iface->ifname, ready ? "-" : "");
  iface->ready = ready;
}

/*
 * IfaceIpIfaceDown()
 *
 * Bring down the IP interface. This implies we're no longer ready.
 */

static void
IfaceIpIfaceDown(void)
{
  IfaceState	const iface = &bund->iface;
  int		k;

  /* Sanity */
  assert(iface->ip_up);

  /* Call "down" script */
  if (*iface->down_script) {
    ExecCmd(LG_IFACE, "%s %s inet %s",
      iface->down_script, iface->ifname, bund->peer_authname);
  }

  /* Delete routes */
  for (k = 0; k < iface->n_routes; k++) {
    IfaceRoute	const r = &iface->routes[k];
    char	nmbuf[40], peerbuf[40];

    if (!r->ok)
      continue;
    if (r->netmask.s_addr) {
      snprintf(nmbuf, sizeof(nmbuf),
	" -netmask 0x%08lx", (u_long)ntohl(r->netmask.s_addr));
    } else
      *nmbuf = 0;
    snprintf(peerbuf, sizeof(peerbuf), "%s", inet_ntoa(iface->peer_addr));
    ExecCmd(LG_IFACE, "%s delete %s %s%s",
      PATH_ROUTE, inet_ntoa(r->dest), peerbuf, nmbuf);
    r->ok = 0;
  }

  /* Delete any proxy arp entry */
  if (iface->proxy_addr.s_addr)
    ExecCmd(LG_IFACE, "%s -d %s", PATH_ARP, inet_ntoa(iface->proxy_addr));
  iface->proxy_addr.s_addr = 0;

  /* Bring down system interface */
  ExecCmd(LG_IFACE, "%s %s down delete -link0", PATH_IFCONFIG, iface->ifname);
  iface->ready = 0;

  /* Done */
  iface->ip_up = FALSE;
}

/*
 * IfaceIdleTimeout()
 */

static void
IfaceIdleTimeout(void *arg)
{
  IfaceState			const iface = &bund->iface;
  char				path[NG_PATHLEN + 1];
  struct ng_bpf_hookstat	oldStats;
  union {
      u_char			buf[sizeof(struct ng_mesg) + sizeof(oldStats)];
      struct ng_mesg		reply;
  }				u;
  int				k;

  /* Get updated bpf node traffic statistics */
  oldStats = iface->idleStats;
  snprintf(path, sizeof(path), "%s:%s", iface->ifname, NG_IFACE_HOOK_INET);
  if (NgSendMsg(bund->csock, path, NGM_BPF_COOKIE,
      NGM_BPF_GET_STATS, BPF_HOOK_IFACE, sizeof(BPF_HOOK_IFACE)) < 0) {
    Log(LG_ERR, ("[%s] can't get %s stats: %s",
      bund->name, NG_BPF_NODE_TYPE, strerror(errno)));
    return;
  }
  if (NgRecvMsg(bund->csock, &u.reply, sizeof(u), NULL) < 0) {
    Log(LG_ERR, ("[%s] node \"%s\" reply: %s",
      bund->name, path, strerror(errno)));
    return;
  }
  memcpy(&iface->idleStats, u.reply.data, sizeof(iface->idleStats));

  /* Mark current traffic period if there was traffic */
  if (iface->idleStats.recvMatchFrames > oldStats.recvMatchFrames)
    iface->traffic[0] = TRUE;
  else {		/* no demand traffic for a whole idle timeout period? */
    for (k = 0; k < IFACE_IDLE_SPLIT && !iface->traffic[k]; k++);
    if (k == IFACE_IDLE_SPLIT) {
      IfaceIdleTimerExpired(NULL);
      return;
    }
  }

  /* Shift traffic history */
  memmove(iface->traffic + 1,
    iface->traffic, (IFACE_IDLE_SPLIT - 1) * sizeof(*iface->traffic));
  iface->traffic[0] = FALSE;

  /* Restart timer */
  TimerStart(&iface->idleTimer);
}

/*
 * IfaceIdleTimerExpired()
 *
 * The idle timeout expired with no demand traffic. Shutdown the
 * link gracefully. Give custom code a chance to do any last minute
 * things before shutting down though. At this point, the shutdown
 * is going to happen, even if there is subsequent demand.
 */

static void
IfaceIdleTimerExpired(void *arg)
{
  IfaceState	const iface = &bund->iface;
#ifdef IA_CUSTOM
  int		delay;
#endif

  /* We already did the final short delay, really shut down now */
  if (arg != NULL) {
    RecordLinkUpDownReason(NULL, 0, STR_IDLE_TIMEOUT, NULL);
    IfaceCloseNcps();
    return;
  }

  /* Idle timeout first detected */
  Log(LG_BUND, ("[%s] idle timeout after %d seconds",
    bund->name, iface->idleTimer.load * IFACE_IDLE_SPLIT / SECONDS));

  /* Get delay and do it */
#ifdef IA_CUSTOM
  if ((delay = CustomIdleTimeoutAction()) > 0) {
    TimerInit(&iface->idleTimer, "IfaceIdle",
      delay * SECONDS, IfaceIdleTimerExpired, (void *)1);
    TimerStart(&iface->idleTimer);
  } else
#endif
  IfaceIdleTimerExpired((void *)1);
}

/*
 * IfaceOpenNcps()
 */

void
IfaceOpenNcps(void)
{
  IpcpOpen();
}

/*
 * IfaceCloseNcps()
 */

void
IfaceCloseNcps(void)
{
  IfaceState	const iface = &bund->iface;

  TimerStop(&iface->idleTimer);
  IpcpClose();
}

/*
 * IfaceCachePkt()
 *
 * A packet caused dial-on-demand; save it for later if possible.
 * Consumes the mbuf in any case.
 */

static void
IfaceCachePkt(int proto, Mbuf pkt)
{
  IfaceState	const iface = &bund->iface;

  /* Only cache network layer data */
  if (!PROT_NETWORK_DATA(proto)) {
    PFREE(pkt);
    return;
  }

  /* Release previously cached packet, if any, and save this one */
  if (iface->dodCache.pkt)
    PFREE(iface->dodCache.pkt);
  iface->dodCache.pkt = pkt;
  iface->dodCache.proto = proto;
  iface->dodCache.ts = time(NULL);
}

/*
 * IfaceCacheSend()
 *
 * Send cached packet
 */

static void
IfaceCacheSend(void)
{
  IfaceState	const iface = &bund->iface;

  if (iface->dodCache.pkt) {
    if (iface->dodCache.ts + MAX_DOD_CACHE_DELAY < time(NULL))
      PFREE(iface->dodCache.pkt);
    else {
      assert(iface->dodCache.proto == PROTO_IP);
      if (NgFuncWriteFrame(bund->name,
	  MPD_HOOK_DEMAND_TAP, iface->dodCache.pkt) < 0) {
	Log(LG_ERR, ("[%s] can't write cached pkt: %s",
	  bund->name, strerror(errno)));
      }
    }
    iface->dodCache.pkt = NULL;
  }
}

/*
 * IfaceIsDemand()
 *
 * Determine if this outgoing packet qualifies for dial-on-demand
 * Packet must be contiguous
 */

static int
IfaceIsDemand(int proto, Mbuf pkt)
{
  switch (proto) {
    case PROTO_IP:
      {
	struct ip	iphdr;
	struct ip	*const ip = &iphdr;

	memcpy(&iphdr, MBDATA(pkt), sizeof(iphdr));
	switch (ip->ip_p) {
	  case IPPROTO_IGMP:		/* No multicast stuff */
	    return(0);
	  case IPPROTO_ICMP:
	    {
	      struct icmp	*const icmp =
		(struct icmp *) ((u_int32_t *) ip + ip->ip_hl);

	      switch (icmp->icmp_type)	/* No ICMP replies */
	      {
		case ICMP_ECHOREPLY:
		case ICMP_UNREACH:
		case ICMP_REDIRECT:
		  return(0);
		default:
		  break;
	      }
	    }
	    break;
	  case IPPROTO_UDP:
	    {
	      struct udphdr	*const udp =
		(struct udphdr *) ((u_int32_t *) ip + ip->ip_hl);

#define NTP_PORT	123
	      if (ntohs(udp->uh_dport) == NTP_PORT)	/* No NTP packets */
		return(0);
	    }
	    break;
	  case IPPROTO_TCP:
	    {
	      struct tcphdr	*const tcp =
		(struct tcphdr *) ((u_int32_t *) ip + ip->ip_hl);

	      if (tcp->th_flags & TH_RST)	/* No TCP reset packets */
		return(0);
	    }
	    break;
	  default:
	    break;
	}
	break;
      }
    default:
      break;
  }
  return(1);
}

/*
 * IfaceSetCommand()
 */

static int
IfaceSetCommand(int ac, char *av[], void *arg)
{
  IfaceState	const iface = &bund->iface;

  if (ac == 0)
    return(-1);
  switch ((intptr_t)arg) {
    case SET_IDLE:
      iface->idle_timeout = atoi(*av);
      break;
    case SET_ADDRS:
      {
	struct in_addr	self_addr;
	struct in_addr	peer_addr;

	/* Parse */
	if (ac != 2)
	  return(-1);
	if (!inet_aton(av[0], &self_addr)) {
	  Log(LG_ERR, ("mpd: bad IP address \"%s\"", av[0]));
	  break;
	}
	if (!inet_aton(av[1], &peer_addr)) {
	  Log(LG_ERR, ("mpd: bad IP address \"%s\"", av[1]));
	  break;
	}

	/* OK */
	iface->self_addr = self_addr;
	iface->peer_addr = peer_addr;
      }
      break;

    case SET_ROUTE:
      {
	struct ifaceroute	r;
	struct in_range		range;

	/* Check */
	if (ac != 1)
	  return(-1);
	if (iface->n_routes >= IFACE_MAX_ROUTES) {
	  Log(LG_ERR, ("iface: too many routes"));
	  break;
	}

	/* Get dest address */
	if (!strcasecmp(av[0], "default"))
	  memset(&range, 0, sizeof(range));
	else if (!ParseAddr(av[0], &range)) {
	  Log(LG_ERR, ("route: bad dest address \"%s\"", av[0]));
	  break;
	}
	r.netmask.s_addr = range.width ?
	  htonl(~0 << (32 - range.width)) : 0;
	r.dest.s_addr = (range.ipaddr.s_addr & r.netmask.s_addr);
	iface->routes[iface->n_routes++] = r;
      }
      break;

    case SET_MTU:
      {
	int	max_mtu;

	max_mtu = atoi(av[0]);
	if (max_mtu < IFACE_MIN_MTU || max_mtu > IFACE_MAX_MTU) {
	  Log(LG_ERR, ("invalid interface mtu %d", max_mtu));
	  break;
	}
	iface->max_mtu = max_mtu;
      }
      break;

    case SET_UP_SCRIPT:
      switch (ac) {
	case 0:
	  *iface->up_script = 0;
	  break;
	case 1:
	  snprintf(iface->up_script,
	    sizeof(iface->up_script), "%s", av[0]);
	  break;
	default:
	  return(-1);
      }
      break;

    case SET_DOWN_SCRIPT:
      switch (ac) {
	case 0:
	  *iface->down_script = 0;
	  break;
	case 1:
	  snprintf(iface->down_script,
	    sizeof(iface->down_script), "%s", av[0]);
	  break;
	default:
	  return(-1);
      }
      break;

    case SET_ENABLE:
      EnableCommand(ac, av, &iface->options, gConfList);
      break;

    case SET_DISABLE:
      DisableCommand(ac, av, &iface->options, gConfList);
      break;

    default:
      assert(0);
  }
  return(0);
}

/*
 * IfaceStat()
 */

int
IfaceStat(int ac, char *av[], void *arg)
{
  IfaceState	const iface = &bund->iface;
  int		k;

  printf("Interface %s:\n", iface->ifname);
  printf("\tStatus       : %s\n", iface->open ? "OPEN" : "CLOSED");
  printf("\tIP Addresses : %s -> ", inet_ntoa(iface->self_addr));
  printf("%s\n", inet_ntoa(iface->peer_addr));
  printf("\tMaximum MTU  : %d bytes\n", iface->max_mtu);
  printf("\tCurrent MTU  : %d bytes\n", iface->mtu);
  printf("\tIdle timeout : %d seconds\n", iface->idle_timeout);
  printf("\tEvent scripts: UP: \"%s\"  DOWN: \"%s\"\n",
    *iface->up_script ? iface->up_script : "<none>",
    *iface->down_script ? iface->down_script : "<none>");
  printf("Static routes via peer:\n");
  for (k = 0; k < iface->n_routes; k++) {
    printf("\t%s ", iface->routes[k].dest.s_addr ?
      inet_ntoa(iface->routes[k].dest) : "default");
    if (iface->routes[k].netmask.s_addr)
      printf("\tnetmask %s", inet_ntoa(iface->routes[k].netmask));
    printf("\n");
  }
  printf("Interface level options:\n");
  OptStat(&iface->options, gConfList);
  return(0);
}

/*
 * IfaceSetMTU()
 *
 * Set MTU and bandwidth on bundle's interface
 */

void
IfaceSetMTU(int mtu, int speed)
{
  IfaceState	const iface = &bund->iface;
  struct ifreq	ifr;
  int		s;

  /* Get socket */
  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    Perror("socket");
    DoExit(EX_ERRDEAD);
  }

  /* Limit MTU to configured maximum */
  if (mtu > iface->max_mtu)
    mtu = iface->max_mtu;

  /* Set MTU on interface */
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, bund->iface.ifname, sizeof(ifr.ifr_name));
  ifr.ifr_mtu = mtu;
  Log(LG_BUND|LG_IFACE, ("[%s] setting interface %s MTU to %d bytes",
    bund->name, bund->iface.ifname, mtu));
  if (ioctl(s, SIOCSIFMTU, (char *)&ifr) < 0)
    Perror("ioctl(%s, %s)", bund->iface.ifname, "SIOCSIFMTU");
  close(s);

  /* Save MTU */
  iface->mtu = mtu;
}

/*
 * IfaceGetAnyIpAddress()
 *
 * Get any non-loopback IP address owned by this machine
 * Prefer addresses from non-point-to-point interfaces.
 */

int
IfaceGetAnyIpAddress(struct in_addr *ipaddr)
{
  int			s, p2p = 0;
  struct in_addr	ipa = { 0 };
  struct ifreq		*ifr, *ifend;
  struct ifreq		ifreq;
  struct ifconf		ifc;
  struct ifreq		ifs[MAX_INTERFACES];

  /* Get interface list */
  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    Perror("socket");
    DoExit(EX_ERRDEAD);
  }

  ifc.ifc_len = sizeof(ifs);
  ifc.ifc_req = ifs;
  if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
    Perror("ioctl(SIOCGIFCONF)");
    close(s);
    return(-1);
  }

  for (ifend = (struct ifreq *)(void *)(ifc.ifc_buf + ifc.ifc_len),
	ifr = ifc.ifc_req;
      ifr < ifend;
      ifr = (struct ifreq *)(void *)((char *) &ifr->ifr_addr
	+ MAX(ifr->ifr_addr.sa_len, sizeof(ifr->ifr_addr)))) {
    if (ifr->ifr_addr.sa_family == AF_INET) {

      /* Check that the interface is up, and not loopback; prefer non-p2p */
      strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
      if (ioctl(s, SIOCGIFFLAGS, &ifreq) < 0)
	continue;
      if ((ifreq.ifr_flags & (IFF_UP|IFF_LOOPBACK)) != IFF_UP)
	continue;
      if ((ifreq.ifr_flags & IFF_POINTOPOINT) && ipa.s_addr && !p2p)
	continue;

      /* Save IP address and interface name */
      ipa = ((struct sockaddr_in *)(void *)&ifr->ifr_addr)->sin_addr;
      p2p = (ifreq.ifr_flags & IFF_POINTOPOINT) != 0;
    }
  }
  close(s);

  /* Found? */
  if (ipa.s_addr == 0)
    return(-1);
  *ipaddr = ipa;
  return(0);
}

/*
 * IfaceGetEther()
 *
 * Get the hardware address of an interface on the the same subnet as addr.
 * If addr == NULL, finds the address of any local ethernet interface.
 */

int
IfaceGetEther(struct in_addr *addr, struct sockaddr_dl *hwaddr)
{
  int			s;
  struct ifreq		*ifr, *ifend, *ifp;
  u_long		ina, mask;
  struct ifreq		ifreq;
  struct ifconf		ifc;
  struct ifreq		ifs[MAX_INTERFACES];

  /* Get interface list */
  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    Perror("socket");
    DoExit(EX_ERRDEAD);
  }

  ifc.ifc_len = sizeof(ifs);
  ifc.ifc_req = ifs;
  if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
    Perror("ioctl(SIOCGIFCONF)");
    close(s);
    return(-1);
  }

  /*
   * Scan through looking for an interface with an IP
   * address on same subnet as `addr'.
   */
  for (ifend = (struct ifreq *)(void *)(ifc.ifc_buf + ifc.ifc_len),
	ifr = ifc.ifc_req;
      ifr < ifend;
      ifr = (struct ifreq *)(void *)((char *) &ifr->ifr_addr
	+ MAX(ifr->ifr_addr.sa_len, sizeof(ifr->ifr_addr)))) {
    if (ifr->ifr_addr.sa_family == AF_INET) {

      /* Save IP address and interface name */
      ina = ((struct sockaddr_in *)(void *)&ifr->ifr_addr)->sin_addr.s_addr;
      strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));

      /* Check that the interface is up, and not point-to-point or loopback */
      if (ioctl(s, SIOCGIFFLAGS, &ifreq) < 0)
	continue;
      if ((ifreq.ifr_flags &
	  (IFF_UP|IFF_BROADCAST|IFF_POINTOPOINT|IFF_LOOPBACK|IFF_NOARP))
	  != (IFF_UP|IFF_BROADCAST))
	continue;

      /* Get its netmask and check that it's on the right subnet */
      if (ioctl(s, SIOCGIFNETMASK, &ifreq) < 0)
	continue;
      mask = ((struct sockaddr_in *)(void *)&ifreq.ifr_addr)->sin_addr.s_addr;
      if (addr && (addr->s_addr & mask) != (ina & mask))
	continue;

      /* OK */
      break;
    }
  }
  close(s);

  /* Found? */
  if (ifr >= ifend)
    return(-1);

  /* Now scan again looking for a link-level address for this interface */
  for (ifp = ifr, ifr = ifc.ifc_req; ifr < ifend; ) {
    if (strcmp(ifp->ifr_name, ifr->ifr_name) == 0
	&& ifr->ifr_addr.sa_family == AF_LINK) {
      memcpy(hwaddr, (struct sockaddr_dl *)(void *)&ifr->ifr_addr,
	sizeof(*hwaddr));
      return(0);
    }
    ifr = (struct ifreq *)(void *)((char *)&ifr->ifr_addr
      + MAX(ifr->ifr_addr.sa_len, sizeof(ifr->ifr_addr)));
  }

  /* Not found! */
  return(-1);
}

