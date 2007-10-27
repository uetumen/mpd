
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

/*
 * INTERNAL FUNCTIONS
 */

  static int	LinkSetCommand(int ac, char *av[], void *arg);
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
    { 1,	LINK_CONF_CHAP,		"chap"		},
    { 1,	LINK_CONF_ACFCOMP,	"acfcomp"	},
    { 1,	LINK_CONF_PROTOCOMP,	"protocomp"	},
    { 0,	LINK_CONF_MAGICNUM,	"magicnum"	},
    { 0,	LINK_CONF_PASSIVE,	"passive"	},
    { 0,	LINK_CONF_CHECK_MAGIC,	"check-magic"	},
    { 0,	LINK_CONF_NO_ORIG_AUTH,	"no-orig-auth"	},
    { 0,	LINK_CONF_CALLBACK,	"callback"	},
    { 0,	0,			NULL		},
  };

/*
 * LinkOpen()
 */

void
LinkOpen(Link l)
{
  MsgSend(l->msgs, MSG_OPEN, NULL);
}

/*
 * LinkClose()
 */

void
LinkClose(Link l)
{
  MsgSend(l->msgs, MSG_CLOSE, NULL);
}

/*
 * LinkUp()
 */

void
LinkUp(Link l)
{
  MsgSend(l->msgs, MSG_UP, NULL);
}

/*
 * LinkDown()
 */

void
LinkDown(Link l)
{
  MsgSend(l->msgs, MSG_DOWN, NULL);
}

/*
 * LinkMsg()
 *
 * Deal with incoming message to this link
 */

static void
LinkMsg(int type, void *arg)
{
  Log(LG_LINK, ("[%s] link: %s event", lnk->name, MsgName(type)));
  switch (type) {
    case MSG_OPEN:
      lnk->num_redial = 0;
      LcpOpen();
      break;
    case MSG_CLOSE:
      LcpClose();
      break;
    case MSG_UP:
      lnk->originate = PhysGetOriginate();
      Log(LG_LINK, ("[%s] link: origination is %s",
	lnk->name, LINK_ORIGINATION(lnk->originate)));
      LcpUp();
      break;
    case MSG_DOWN:
      LcpDown();
      if (OPEN_STATE(lnk->lcp.fsm.state)) {
	if (lnk->conf.max_redial == -1) {
	  SetStatus(ADLG_WAN_WAIT_FOR_DEMAND, STR_READY_TO_DIAL);
	  LcpClose();
	  BundLinkGaveUp();
	} else if (!lnk->conf.max_redial
	    || lnk->num_redial < lnk->conf.max_redial) {
	  lnk->num_redial++;
	  RecordLinkUpDownReason(lnk, 1, STR_REDIAL, NULL);
	  PhysOpen();					/* Try again */
	} else {
	  Log(LG_LINK, ("[%s] giving up after %d connection attempts",
	    lnk->name, lnk->num_redial));
	  SetStatus(ADLG_WAN_WAIT_FOR_DEMAND, STR_READY_TO_DIAL);
	  LcpClose();
	  BundLinkGaveUp();
	}
      }
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
LinkNew(char *name)
{
  int k;

  /* Check if name is already used */
  for (k = 0; k < gNumLinks; k++) {
    if (gLinks[k] && !strcmp(gLinks[k]->name, name)) {
      Log(LG_ERR, ("mpd: link \"%s\" already defined in bundle \"%s\"",
	name, gLinks[k]->bund->name));
      return(NULL);
    }
  }

  /* Find a free link pointer */
  for (k = 0; k < gNumLinks && gLinks[k] != NULL; k++);
  if (k == gNumLinks)			/* add a new link pointer */
    LengthenArray(&gLinks, sizeof(*gLinks), &gNumLinks, MB_BUND);

  /* Create and initialize new link */
  lnk = Malloc(MB_BUND, sizeof(*lnk));
  gLinks[k] = lnk;
  snprintf(lnk->name, sizeof(lnk->name), "%s", name);
  lnk->msgs = MsgRegister(LinkMsg, LINK_PRIO);

  /* Initialize link configuration with defaults */
  lnk->conf.mru = LCP_DEFAULT_MRU;
  lnk->conf.mtu = LCP_DEFAULT_MRU;
  lnk->conf.accmap = 0x000a0000;
  lnk->conf.retry_timeout = LINK_DEFAULT_RETRY;
  lnk->bandwidth = LINK_DEFAULT_BANDWIDTH;
  lnk->latency = LINK_DEFAULT_LATENCY;

  Disable(&lnk->conf.options, LINK_CONF_CHAP);
  Accept(&lnk->conf.options, LINK_CONF_CHAP);

  Disable(&lnk->conf.options, LINK_CONF_PAP);
  Accept(&lnk->conf.options, LINK_CONF_PAP);
 
  Enable(&lnk->conf.options, LINK_CONF_ACFCOMP);
  Accept(&lnk->conf.options, LINK_CONF_ACFCOMP);

  Enable(&lnk->conf.options, LINK_CONF_PROTOCOMP);
  Accept(&lnk->conf.options, LINK_CONF_PROTOCOMP);

  Enable(&lnk->conf.options, LINK_CONF_MAGICNUM);
  Disable(&lnk->conf.options, LINK_CONF_PASSIVE);
  Enable(&lnk->conf.options, LINK_CONF_CHECK_MAGIC);

  /* Initialize link layer stuff */
  lnk->phys = PhysInit();
  LcpInit();

  /* Read special configuration for link, if any */
  (void) ReadFile(LINKS_FILE, name, DoCommand);

  /* Hang out and be a link */
  return(lnk);
}

/*
 * LinkShow()
 */

int
LinkCommand(int ac, char *av[], void *arg)
{
  int	k;

  if (ac != 1)
    return(-1);

  /* Find link */
  for (k = 0;
    k < gNumLinks && (!gLinks[k] || strcmp(gLinks[k]->name, av[0]));
    k++);
  if (k == gNumLinks) {
    printf("Link \"%s\" is not defined\n", av[0]);
    return(0);
  }

  /* Change default link and bundle */
  lnk = gLinks[k];
  bund = lnk->bund;
  return(0);
}

/*
 * LinkStat()
 */

int
LinkStat(int ac, char *av[], void *arg)
{
  struct ng_ppp_link_stat	stats;

  printf("Link %s:\n", lnk->name);

  printf("Configuration\n");
  printf("\tMRU            : %d bytes\n", lnk->conf.mru);
  printf("\tCtrl char map  : 0x%08x bytes\n", lnk->conf.accmap);
  printf("\tRetry timeout  : %d seconds\n", lnk->conf.retry_timeout);
  printf("\tMax redial     : %d connect attempts\n", lnk->conf.max_redial);
  printf("\tBandwidth      : %d bits/sec\n", lnk->bandwidth);
  printf("\tLatency        : %d usec\n", lnk->latency);
  printf("\tKeep-alive     : ");
  if (lnk->lcp.fsm.conf.echo_int == 0)
    printf("disabled\n");
  else
    printf("every %d secs, timeout %d\n",
      lnk->lcp.fsm.conf.echo_int, lnk->lcp.fsm.conf.echo_max);
  printf("\tIdent string   : \"%s\"\n", lnk->conf.ident ? lnk->conf.ident : "");
  printf("Link level options\n");
  OptStat(&lnk->conf.options, gConfList);
  if (NgFuncGetStats(lnk->bundleIndex, 0, &stats) >= 0) {
    printf("Traffic stats:\n");

    printf("\tOctets input   : %8u\n", stats.recvOctets);
    printf("\tFrames input   : %8u\n", stats.recvFrames);
    printf("\tOctets output  : %8u\n", stats.xmitOctets);
    printf("\tFrames output  : %8u\n", stats.xmitFrames);
    printf("\tBad protocols  : %8u\n", stats.badProtos);
#if NGM_PPP_COOKIE >= 940897794
    printf("\tRunts          : %8u\n", stats.runts);
#endif
    printf("\tDup fragments  : %8u\n", stats.dupFragments);
#if NGM_PPP_COOKIE >= 940897794
    printf("\tDrop fragments : %8u\n", stats.dropFragments);
#endif
  }

  printf("Device specific info:\n");
  PhysStat(0, NULL, NULL);
  return(0);
}

/*
 * LinkSetCommand()
 */

static int
LinkSetCommand(int ac, char *av[], void *arg)
{
  int	val;
  char	*name;

  if (ac == 0)
    return(-1);
  switch ((int) arg) {
    case SET_BANDWIDTH:
      val = atoi(*av);
      if (val <= 0)
	Log(LG_ERR, ("Bandwidth must be positive"));
      else
	lnk->bandwidth = val;
      break;

    case SET_LATENCY:
      val = atoi(*av);
      lnk->latency = val;
      break;

    case SET_DEVTYPE:
      PhysSetDeviceType(*av);
      break;

    case SET_MRU:
    case SET_MTU:
      val = atoi(*av);
      name = ((int)arg == SET_MTU) ? "MTU" : "MRU";
      if (!lnk->phys->type)
	Log(LG_ERR, ("[%s] this link has no type set", lnk->name));
      else if (val < LCP_MIN_MRU)
	Log(LG_ERR, ("[%s] the min %s is %d", lnk->name, name, LCP_MIN_MRU));
      else if (val + LCP_MRU_MARGIN > lnk->phys->type->mru)
	Log(LG_ERR, ("[%s] the max %s on type \"%s\" links is %d",
	  lnk->name, name, lnk->phys->type->name,
	  lnk->phys->type->mru - LCP_MRU_MARGIN));
      else if ((int)arg == SET_MTU)
	lnk->conf.mtu = val;
      else
	lnk->conf.mru = val;
      break;

    case SET_FSM_RETRY:
      lnk->conf.retry_timeout = atoi(*av);
      if (lnk->conf.retry_timeout < 1 || lnk->conf.retry_timeout > 10)
	lnk->conf.retry_timeout = LINK_DEFAULT_RETRY;
      break;

    case SET_MAX_RETRY:
      lnk->conf.max_redial = atoi(*av);
      break;

    case SET_ACCMAP:
      sscanf(*av, "%x", &val);
      lnk->conf.accmap = val;
      break;

    case SET_KEEPALIVE:
      if (ac != 2)
	return(-1);
      lnk->lcp.fsm.conf.echo_int = atoi(av[0]);
      lnk->lcp.fsm.conf.echo_max = atoi(av[1]);
      break;

    case SET_IDENT:
      if (ac != 1)
	return(-1);
      if (lnk->conf.ident != NULL) {
	Freee(lnk->conf.ident);
	lnk->conf.ident = NULL;
      }
      if (*av[0] != '\0')
	strcpy(lnk->conf.ident = Malloc(MB_FSM, strlen(av[0]) + 1), av[0]);
      break;

    case SET_ACCEPT:
      AcceptCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    case SET_DENY:
      DenyCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    case SET_ENABLE:
      EnableCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    case SET_DISABLE:
      DisableCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    case SET_YES:
      YesCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    case SET_NO:
      NoCommand(ac, av, &lnk->conf.options, gConfList);
      break;

    default:
      assert(0);
  }
  return(0);
}

