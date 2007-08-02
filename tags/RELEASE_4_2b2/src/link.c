
/*
 * link.c
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#include "ppp.h"
#include "link.h"
#include "msg.h"
#include "lcp.h"
#include "phys.h"
#include "command.h"
#include "input.h"
#include "ngfunc.h"
#include "util.h"

/*
 * DEFINITIONS
 */

  /* Set menu options */
  enum {
    SET_DEVTYPE,
    SET_BANDWIDTH,
    SET_LATENCY,
    SET_ACCMAP,
    SET_MRU,
    SET_MTU,
    SET_FSM_RETRY,
    SET_MAX_RETRY,
    SET_KEEPALIVE,
    SET_IDENT,
    SET_ACCEPT,
    SET_DENY,
    SET_ENABLE,
    SET_DISABLE,
    SET_YES,
    SET_NO,
  };

  #define RBUF_SIZE		100

/*
 * INTERNAL FUNCTIONS
 */

  static int	LinkSetCommand(Context ctx, int ac, char *av[], void *arg);
  static void	LinkMsg(int type, void *cookie);

/*
 * GLOBAL VARIABLES
 */

  const struct cmdtab LinkSetCmds[] = {
    { "bandwidth bps",			"Link bandwidth",
	LinkSetCommand, NULL, (void *) SET_BANDWIDTH },
    { "type type",			"Device type",
	LinkSetCommand, NULL, (void *) SET_DEVTYPE },
    { "latency microsecs",		"Link latency",
	LinkSetCommand, NULL, (void *) SET_LATENCY },
    { "accmap hex-value",		"Accmap value",
	LinkSetCommand, NULL, (void *) SET_ACCMAP },
    { "mru value",			"Link MRU value",
	LinkSetCommand, NULL, (void *) SET_MRU },
    { "mtu value",			"Link MTU value",
	LinkSetCommand, NULL, (void *) SET_MTU },
    { "fsm-timeout seconds",		"FSM retry timeout",
	LinkSetCommand, NULL, (void *) SET_FSM_RETRY },
    { "max-redial num",			"Max connect attempts",
	LinkSetCommand, NULL, (void *) SET_MAX_RETRY },
    { "keep-alive secs max",		"LCP echo keep-alives",
	LinkSetCommand, NULL, (void *) SET_KEEPALIVE },
    { "ident ident-string",		"LCP ident string",
	LinkSetCommand, NULL, (void *) SET_IDENT },
    { "accept [opt ...]",		"Accept option",
	LinkSetCommand, NULL, (void *) SET_ACCEPT },
    { "deny [opt ...]",			"Deny option",
	LinkSetCommand, NULL, (void *) SET_DENY },
    { "enable [opt ...]",		"Enable option",
	LinkSetCommand, NULL, (void *) SET_ENABLE },
    { "disable [opt ...]",		"Disable option",
	LinkSetCommand, NULL, (void *) SET_DISABLE },
    { "yes [opt ...]",			"Enable and accept option",
	LinkSetCommand, NULL, (void *) SET_YES },
    { "no [opt ...]",			"Disable and deny option",
	LinkSetCommand, NULL, (void *) SET_NO },
    { NULL },
  };

/*
 * INTERNAL VARIABLES
 */

  static struct confinfo	gConfList[] = {
    { 1,	LINK_CONF_PAP,		"pap"		},
    { 1,	LINK_CONF_CHAPMD5,	"chap-md5"	},
    { 1,	LINK_CONF_CHAPMSv1,	"chap-msv1"	},
    { 1,	LINK_CONF_CHAPMSv2,	"chap-msv2"	},
    { 1,	LINK_CONF_EAP,		"eap"		},
    { 1,	LINK_CONF_ACFCOMP,	"acfcomp"	},
    { 1,	LINK_CONF_PROTOCOMP,	"protocomp"	},
    { 0,	LINK_CONF_MSDOMAIN,	"keep-ms-domain"},
    { 0,	LINK_CONF_MAGICNUM,	"magicnum"	},
    { 0,	LINK_CONF_PASSIVE,	"passive"	},
    { 0,	LINK_CONF_CHECK_MAGIC,	"check-magic"	},
    { 0,	LINK_CONF_NO_ORIG_AUTH,	"no-orig-auth"	},
    { 0,	LINK_CONF_CALLBACK,	"callback"	},
    { 0,	0,			NULL		},
  };

/*
 * LinkOpenCmd()
 */

void
LinkOpenCmd(Context ctx)
{
  RecordLinkUpDownReason(NULL, ctx->lnk, 1, STR_MANUALLY, NULL);
  LinkOpen(ctx->lnk);
}

/*
 * LinkCloseCmd()
 */

void
LinkCloseCmd(Context ctx)
{
  RecordLinkUpDownReason(NULL, ctx->lnk, 0, STR_MANUALLY, NULL);
  LinkClose(ctx->lnk);
}

/*
 * LinkOpen()
 */

void
LinkOpen(Link l)
{
  MsgSend(l->msgs, MSG_OPEN, l);
}

/*
 * LinkClose()
 */

void
LinkClose(Link l)
{
  MsgSend(l->msgs, MSG_CLOSE, l);
}

/*
 * LinkUp()
 */

void
LinkUp(Link l)
{
  MsgSend(l->msgs, MSG_UP, l);
}

/*
 * LinkDown()
 */

void
LinkDown(Link l)
{
  MsgSend(l->msgs, MSG_DOWN, l);
}

/*
 * LinkMsg()
 *
 * Deal with incoming message to this link
 */

static void
LinkMsg(int type, void *arg)
{
    Link	l = (Link)arg;

  Log(LG_LINK, ("[%s] link: %s event", l->name, MsgName(type)));
  switch (type) {
    case MSG_OPEN:
      l->last_open = time(NULL);
      l->num_redial = 0;
      LcpOpen(l);
      break;
    case MSG_CLOSE:
      LcpClose(l);
      break;
    case MSG_UP:
      l->originate = PhysGetOriginate(l->phys);
      Log(LG_LINK, ("[%s] link: origination is %s",
	l->name, LINK_ORIGINATION(l->originate)));
      LcpUp(l);
      break;
    case MSG_DOWN:
      if (OPEN_STATE(l->lcp.fsm.state)) {
	if ((l->conf.max_redial != 0) && (l->num_redial >= l->conf.max_redial)) {
	  if (l->conf.max_redial >= 0)
	    Log(LG_LINK, ("[%s] link: giving up after %d reconnection attempts",
		l->name, l->num_redial));
	  LcpClose(l);
          LcpDown(l);
	} else {
	  l->num_redial++;
	  Log(LG_LINK, ("[%s] link: reconnection attempt %d",
	    l->name, l->num_redial));
	  RecordLinkUpDownReason(NULL, l, 1, STR_REDIAL, NULL);
    	  LcpDown(l);
	  if (!gShutdownInProgress)	/* Giveup on shutdown */
	    PhysOpen(l->phys);		/* Try again */
	}
      } else {
        LcpDown(l);
      }
      /* reset Link-stats */
      LinkResetStats(l);  /* XXX: I don't think this is a right place */
      break;
  }
}

/*
 * LinkNew()
 *
 * Allocate a new link for the specified device, then
 * read in any device-specific commands from ppp.links.
 */

Link
LinkNew(char *name, Bund b, int bI)
{
    Link lnk;

  /* Create and initialize new link */
  lnk = Malloc(MB_LINK, sizeof(*lnk));
  snprintf(lnk->name, sizeof(lnk->name), "%s", name);
  lnk->bund = b;
  lnk->bundleIndex = bI;
  lnk->msgs = MsgRegister(LinkMsg, 0);

  /* Initialize link configuration with defaults */
  lnk->conf.mru = LCP_DEFAULT_MRU;
  lnk->conf.mtu = LCP_DEFAULT_MRU;
  lnk->conf.accmap = 0x000a0000;
  lnk->conf.max_redial = -1;
  lnk->conf.retry_timeout = LINK_DEFAULT_RETRY;
  lnk->bandwidth = LINK_DEFAULT_BANDWIDTH;
  lnk->latency = LINK_DEFAULT_LATENCY;
  lnk->upReason = NULL;
  lnk->upReasonValid = 0;
  lnk->downReason = NULL;
  lnk->downReasonValid = 0;

  Disable(&lnk->conf.options, LINK_CONF_CHAPMD5);
  Accept(&lnk->conf.options, LINK_CONF_CHAPMD5);

  Disable(&lnk->conf.options, LINK_CONF_CHAPMSv1);
  Deny(&lnk->conf.options, LINK_CONF_CHAPMSv1);

  Disable(&lnk->conf.options, LINK_CONF_CHAPMSv2);
  Accept(&lnk->conf.options, LINK_CONF_CHAPMSv2);

  Disable(&lnk->conf.options, LINK_CONF_PAP);
  Accept(&lnk->conf.options, LINK_CONF_PAP);

  Disable(&lnk->conf.options, LINK_CONF_EAP);
  Accept(&lnk->conf.options, LINK_CONF_EAP);

  Disable(&lnk->conf.options, LINK_CONF_MSDOMAIN);

  Enable(&lnk->conf.options, LINK_CONF_ACFCOMP);
  Accept(&lnk->conf.options, LINK_CONF_ACFCOMP);

  Enable(&lnk->conf.options, LINK_CONF_PROTOCOMP);
  Accept(&lnk->conf.options, LINK_CONF_PROTOCOMP);

  Enable(&lnk->conf.options, LINK_CONF_MAGICNUM);
  Disable(&lnk->conf.options, LINK_CONF_PASSIVE);
  Enable(&lnk->conf.options, LINK_CONF_CHECK_MAGIC);

  LcpInit(lnk);
  EapInit(lnk);

  /* Initialize link layer stuff */
  lnk->phys = PhysInit(lnk->name, lnk, NULL);

  /* Hang out and be a link */
  return(lnk);
}

/*
 * LinkShutdown()
 *
 */

void
LinkShutdown(Link l)
{
    MsgUnRegister(&l->msgs);
    if (l->phys)
      PhysShutdown(l->phys);
    Freee(MB_LINK, l);
}

/*
 * LinkFind()
 *
 * Find a link structure
 */

Link
LinkFind(char *name)
{
    int		k;

    k = gNumPhyses;
    if ((sscanf(name, "[%x]", &k) != 1) || (k < 0) || (k >= gNumPhyses)) {
        /* Find link */
	for (k = 0;
	    k < gNumPhyses && (gPhyses[k] == NULL || gPhyses[k]->link == NULL || 
		strcmp(gPhyses[k]->link->name, name));
	    k++);
    };
    if (k == gNumPhyses || gPhyses[k] == NULL || gPhyses[k]->link == NULL) {
	return (NULL);
    }

    return (gPhyses[k]->link);
}

/*
 * LinkCommand()
 */

int
LinkCommand(Context ctx, int ac, char *av[], void *arg)
{
    Link	l;

    if (ac != 1)
	return(-1);

    if ((l = LinkFind(av[0])) == NULL) {
	Printf("Link \"%s\" is not defined\r\n", av[0]);
	return(0);
    }

    /* Change default link and bundle */
    ctx->lnk = l;
    ctx->bund = l->bund;
    ctx->phys = l->phys;
    ctx->rep = NULL;
  return(0);
}

/*
 * RecordLinkUpDownReason()
 *
 * This is called whenever a reason for the link going up or
 * down has just become known. Record this reason so that when
 * the link actually goes up or down, we can record it.
 *
 * If this gets called more than once in the "down" case,
 * the first call prevails.
 */
static void
RecordLinkUpDownReason2(Link l, int up, const char *key, const char *fmt, va_list args)
{
  char	**const cpp = up ? &l->upReason : &l->downReason;
  char	*buf;

  /* First reason overrides later ones */
  if (up) {
    if (l->upReasonValid) {
	return;
    } else {
	l->upReasonValid = 1;
    }
  } else {
    if (l->downReasonValid) {
	return;
    } else {
	l->downReasonValid = 1;
    }
  }

  /* Allocate buffer if necessary */
  if (!*cpp)
    *cpp = Malloc(MB_UTIL, RBUF_SIZE);
  buf = *cpp;

  /* Record reason */
  if (fmt) {
    snprintf(buf, RBUF_SIZE, "%s:", key);
    vsnprintf(buf + strlen(buf), RBUF_SIZE - strlen(buf), fmt, args);
  } else 
    snprintf(buf, RBUF_SIZE, "%s", key);
}

void
RecordLinkUpDownReason(Bund b, Link l, int up, const char *key, const char *fmt, ...)
{
  va_list	args;
  int		k;

  if (l != NULL) {
    va_start(args, fmt);
    RecordLinkUpDownReason2(l, up, key, fmt, args);
    va_end(args);

  } else if (b != NULL) {
    for (k = 0; k < b->n_links; k++) {
      if (b->links[k]) {
	va_start(args, fmt);
	RecordLinkUpDownReason2(b->links[k], up, key, fmt, args);
	va_end(args);
      }
    }
  }

}

/*
 * LinkStat()
 */

int
LinkStat(Context ctx, int ac, char *av[], void *arg)
{
    Link 	l = ctx->lnk;

  Printf("Link %s:\r\n", l->name);

  Printf("Configuration\r\n");
  Printf("\tMRU            : %d bytes\r\n", l->conf.mru);
  Printf("\tCtrl char map  : 0x%08x bytes\r\n", l->conf.accmap);
  Printf("\tRetry timeout  : %d seconds\r\n", l->conf.retry_timeout);
  Printf("\tMax redial     : ");
  if (l->conf.max_redial < 0)
    Printf("no redial\r\n");
  else if (l->conf.max_redial == 0) 
    Printf("unlimited\r\n");
  else
    Printf("%d connect attempts\r\n", l->conf.max_redial);
  Printf("\tBandwidth      : %d bits/sec\r\n", l->bandwidth);
  Printf("\tLatency        : %d usec\r\n", l->latency);
  Printf("\tKeep-alive     : ");
  if (l->lcp.fsm.conf.echo_int == 0)
    Printf("disabled\r\n");
  else
    Printf("every %d secs, timeout %d\r\n",
      l->lcp.fsm.conf.echo_int, l->lcp.fsm.conf.echo_max);
  Printf("\tIdent string   : \"%s\"\r\n", l->conf.ident ? l->conf.ident : "");
  Printf("\tSession-Id     : %s\r\n", l->session_id);
  Printf("Link level options\r\n");
  OptStat(ctx, &l->conf.options, gConfList);
  LinkUpdateStats(l);
  Printf("Up/Down stats:\r\n");
  if (l->downReason && (!l->downReasonValid))
    Printf("\tDown Reason    : %s\r\n", l->downReason);
  if (l->upReason)
    Printf("\tUp Reason      : %s\r\n", l->upReason);
  if (l->downReason && l->downReasonValid)
    Printf("\tDown Reason    : %s\r\n", l->downReason);
  
  Printf("Traffic stats:\r\n");

  Printf("\tOctets input   : %llu\r\n", l->stats.recvOctets);
  Printf("\tFrames input   : %llu\r\n", l->stats.recvFrames);
  Printf("\tOctets output  : %llu\r\n", l->stats.xmitOctets);
  Printf("\tFrames output  : %llu\r\n", l->stats.xmitFrames);
  Printf("\tBad protocols  : %llu\r\n", l->stats.badProtos);
#if NGM_PPP_COOKIE >= 940897794
  Printf("\tRunts          : %llu\r\n", l->stats.runts);
#endif
  Printf("\tDup fragments  : %llu\r\n", l->stats.dupFragments);
#if NGM_PPP_COOKIE >= 940897794
  Printf("\tDrop fragments : %llu\r\n", l->stats.dropFragments);
#endif
  return(0);
}

/* 
 * LinkUpdateStats()
 */

void
LinkUpdateStats(Link l)
{
  struct ng_ppp_link_stat	stats;

  if (NgFuncGetStats(l->bund, l->bundleIndex, FALSE, &stats) != -1) {
    l->stats.xmitFrames += abs(stats.xmitFrames - l->oldStats.xmitFrames);
    l->stats.xmitOctets += abs(stats.xmitOctets - l->oldStats.xmitOctets);
    l->stats.recvFrames += abs(stats.recvFrames - l->oldStats.recvFrames);
    l->stats.recvOctets += abs(stats.recvOctets - l->oldStats.recvOctets);
    l->stats.badProtos  += abs(stats.badProtos - l->oldStats.badProtos);
#if NGM_PPP_COOKIE >= 940897794
    l->stats.runts	  += abs(stats.runts - l->oldStats.runts);
#endif
    l->stats.dupFragments += abs(stats.dupFragments - l->oldStats.dupFragments);
#if NGM_PPP_COOKIE >= 940897794
    l->stats.dropFragments += abs(stats.dropFragments - l->oldStats.dropFragments);
#endif
  }

  l->oldStats = stats;
}

/* 
 * LinkUpdateStatsTimer()
 */

void
LinkUpdateStatsTimer(void *cookie)
{
    Link l = (Link)cookie;

  TimerStop(&l->statsUpdateTimer);
  LinkUpdateStats(l);
  TimerStart(&l->statsUpdateTimer);
}

/*
 * LinkResetStats()
 */

void
LinkResetStats(Link l)
{
  NgFuncGetStats(l->bund, l->bundleIndex, TRUE, NULL);
  memset(&l->stats, 0, sizeof(struct linkstats));
  memset(&l->oldStats, 0, sizeof(l->oldStats));
}

/*
 * LinkSetCommand()
 */

static int
LinkSetCommand(Context ctx, int ac, char *av[], void *arg)
{
    Link	l = ctx->lnk;
  int		val, nac = 0;
  const char	*name;
  char		*nav[ac];
  const char	*av2[] = { "chap-md5", "chap-msv1", "chap-msv2" };

  if (ac == 0)
    return(-1);

  /* make "chap" as an alias for all chap-variants, this should keep BC */
  switch ((intptr_t)arg) {
    case SET_ACCEPT:
    case SET_DENY:
    case SET_ENABLE:
    case SET_DISABLE:
    case SET_YES:
    case SET_NO:
    {
      int	i = 0;
      for ( ; i < ac; i++)
      {
	if (strcasecmp(av[i], "chap") == 0) {
	  LinkSetCommand(ctx, 3, (char **)av2, arg);
	} else {
	  nav[nac++] = av[i];
	} 
      }
      av = nav;
      ac = nac;
      break;
    }
  }

  switch ((intptr_t)arg) {
    case SET_BANDWIDTH:
      val = atoi(*av);
      if (val <= 0)
	Log(LG_ERR, ("[%s] Bandwidth must be positive", l->name));
      else if (val > NG_PPP_MAX_BANDWIDTH * 10 * 8) {
	l->bandwidth = NG_PPP_MAX_BANDWIDTH * 10 * 8;
	Log(LG_ERR, ("[%s] Bandwidth truncated to %d bit/s", l->name, 
	    l->bandwidth));
      } else
	l->bandwidth = val;
      break;

    case SET_LATENCY:
      val = atoi(*av);
      if (val < 0)
	Log(LG_ERR, ("[%s] Latency must be not negative", l->name));
      else if (val > NG_PPP_MAX_LATENCY * 1000) {
	Log(LG_ERR, ("[%s] Latency truncated to %d usec", l->name, 
	    NG_PPP_MAX_LATENCY * 1000));
	l->latency = NG_PPP_MAX_LATENCY * 1000;
      } else
        l->latency = val;
      break;

    case SET_DEVTYPE:
      PhysSetDeviceType(ctx->phys, *av);
      break;

    case SET_MRU:
    case SET_MTU:
      val = atoi(*av);
      name = ((intptr_t)arg == SET_MTU) ? "MTU" : "MRU";
      if (!l->phys->type)
	Log(LG_ERR, ("[%s] this link has no type set", l->name));
      else if (val < LCP_MIN_MRU)
	Log(LG_ERR, ("[%s] the min %s is %d", l->name, name, LCP_MIN_MRU));
      else if (l->phys->type && (val > l->phys->type->mru))
	Log(LG_ERR, ("[%s] the max %s on type \"%s\" links is %d",
	  l->name, name, l->phys->type->name, l->phys->type->mru));
      else if ((intptr_t)arg == SET_MTU)
	l->conf.mtu = val;
      else
	l->conf.mru = val;
      break;

    case SET_FSM_RETRY:
      l->conf.retry_timeout = atoi(*av);
      if (l->conf.retry_timeout < 1 || l->conf.retry_timeout > 10)
	l->conf.retry_timeout = LINK_DEFAULT_RETRY;
      break;

    case SET_MAX_RETRY:
      l->conf.max_redial = atoi(*av);
      break;

    case SET_ACCMAP:
      sscanf(*av, "%x", &val);
      l->conf.accmap = val;
      break;

    case SET_KEEPALIVE:
      if (ac != 2)
	return(-1);
      l->lcp.fsm.conf.echo_int = atoi(av[0]);
      l->lcp.fsm.conf.echo_max = atoi(av[1]);
      break;

    case SET_IDENT:
      if (ac != 1)
	return(-1);
      if (l->conf.ident != NULL) {
	Freee(MB_FSM, l->conf.ident);
	l->conf.ident = NULL;
      }
      if (*av[0] != '\0')
	strcpy(l->conf.ident = Malloc(MB_FSM, strlen(av[0]) + 1), av[0]);
      break;

    case SET_ACCEPT:
      AcceptCommand(ac, av, &l->conf.options, gConfList);
      break;

    case SET_DENY:
      DenyCommand(ac, av, &l->conf.options, gConfList);
      break;

    case SET_ENABLE:
      EnableCommand(ac, av, &l->conf.options, gConfList);
      break;

    case SET_DISABLE:
      DisableCommand(ac, av, &l->conf.options, gConfList);
      break;

    case SET_YES:
      YesCommand(ac, av, &l->conf.options, gConfList);
      break;

    case SET_NO:
      NoCommand(ac, av, &l->conf.options, gConfList);
      break;

    default:
      assert(0);
  }

  return(0);
}
