
/*
 * nat.c
 *
 * Written by Alexander Motin <mav@alkar.net>
 */

#include "ppp.h"
#include "nat.h"
#include "iface.h"
#include "netgraph.h"
#include "util.h"

#ifdef USE_NG_NAT
#include <netgraph/ng_nat.h>
#endif

/*
 * DEFINITIONS
 */

/* Set menu options */

  enum {
    SET_ADDR,
    SET_TARGET,
    SET_ENABLE,
    SET_DISABLE,
  };

static int	NatSetCommand(Context ctx, int ac, char *av[], void *arg);
  
/*
 * GLOBAL VARIABLES
 */

  const struct cmdtab NatSetCmds[] = {
    { "address addr",		"Set alias address",
	NatSetCommand, NULL, (void *) SET_ADDR },
    { "target addr",		"Set target address",
	NatSetCommand, NULL, (void *) SET_TARGET },
    { "enable [opt ...]",		"Enable option",
	NatSetCommand, NULL, (void *) SET_ENABLE },
    { "disable [opt ...]",		"Disable option",
	NatSetCommand, NULL, (void *) SET_DISABLE },
    { NULL },
  };

/*
 * INTERNAL VARIABLES
 */

  static const struct confinfo	gConfList[] = {
    { 0,	NAT_CONF_LOG,			"log"		},
    { 0,	NAT_CONF_INCOMING,		"incoming"	},
    { 0,	NAT_CONF_SAME_PORTS,		"same-ports"	},
    { 0,	NAT_CONF_UNREG_ONLY,		"unreg-only"	},
    { 0,	0,				NULL		},
  };

/*
 * NatInit()
 */

void
NatInit(Bund b)
{
  NatState	const nat = &b->iface.nat;

  /* Default configuration */
  u_addrclear(&nat->alias_addr);
  u_addrclear(&nat->target_addr);
  Disable(&nat->options, NAT_CONF_LOG);
  Enable(&nat->options, NAT_CONF_INCOMING);
  Enable(&nat->options, NAT_CONF_SAME_PORTS);
  Disable(&nat->options, NAT_CONF_UNREG_ONLY);
}


/*
 * NatSetCommand()
 */

static int
NatSetCommand(Context ctx, int ac, char *av[], void *arg)
{
  NatState	const nat = &ctx->bund->iface.nat;

  if (ac == 0)
    return(-1);
  switch ((intptr_t)arg) {
    case SET_TARGET:
#ifndef NG_NAT_LOG
	Log(LG_ERR, ("Target address setting is unsupported by current kernel"));
	return (-1);
#endif
    /* FALL */
    case SET_ADDR:
      {
	struct u_addr	addr;

	/* Parse */
	if (ac != 1)
	  return(-1);
	if (!ParseAddr(av[0], &addr, ALLOW_IPV4)) {
	  Log(LG_ERR, ("bad IP address \"%s\"", av[0]));
	  break;
	}

	/* OK */
	if ((intptr_t)arg == SET_ADDR) {
	    nat->alias_addr = addr;
	} else {
	    nat->target_addr = addr;
	}
      }
      break;

    case SET_ENABLE:
      EnableCommand(ac, av, &nat->options, gConfList);
      break;

    case SET_DISABLE:
      DisableCommand(ac, av, &nat->options, gConfList);
      break;

    default:
      assert(0);
  }
  return(0);
}

/*
 * NatStat()
 */

int
NatStat(Context ctx, int ac, char *av[], void *arg)
{
    NatState	const nat = &ctx->bund->iface.nat;
    char	buf[64];

    Printf("NAT configuration:\r\n");
    Printf("\tAlias addresses : %s\r\n", 
	u_addrtoa(&nat->alias_addr,buf,sizeof(buf)));
    Printf("\tTarget addresses: %s\r\n", 
	u_addrtoa(&nat->target_addr,buf,sizeof(buf)));
    Printf("NAT options:\r\n");
    OptStat(ctx, &nat->options, gConfList);
    return(0);
}