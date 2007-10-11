
/*
 * ng.c
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#include "ppp.h"
#include "ng.h"
#include "phys.h"
#include "ngfunc.h"
#include "log.h"

#include <netgraph/ng_message.h>
#ifdef __DragonFly__
#include <netgraph/socket/ng_socket.h>
#else
#include <netgraph/ng_socket.h>
#endif
#include <netgraph.h>

/*
 * DEFINITIONS
 */

  #define NG_MTU		1600
  #define NG_MRU		1600

  #define NG_REOPEN_PAUSE	5

  struct nginfo {
    char	path[NG_PATHSIZ];	/* Node that takes PPP frames */
    char	hook[NG_HOOKSIZ];	/* Hook on that node */
  };
  typedef struct nginfo	*NgInfo;

  /* Set menu options */
  enum {
    SET_NODE,
    SET_HOOK,
  };

/*
 * INTERNAL FUNCTIONS
 */

  static int	NgInit(Link l);
  static void	NgOpen(Link l);
  static void	NgClose(Link l);
  static void	NgShutdown(Link l);
  static void	NgStat(Context ctx);
  static int	NgSetCommand(Context ctx, int ac, char *av[], void *arg);
  static int	NgIsSync(Link l);
  static int	NgPeerAddr(Link l, void *buf, int buf_len);

/*
 * GLOBAL VARIABLES
 */

  const struct phystype gNgPhysType = {
    .name		= "ng",
    .descr		= "Netgraph hook",
    .minReopenDelay	= NG_REOPEN_PAUSE,
    .mtu		= NG_MTU,
    .mru		= NG_MRU,
    .tmpl		= 0,
    .init		= NgInit,
    .open		= NgOpen,
    .close		= NgClose,
    .shutdown		= NgShutdown,
    .showstat		= NgStat,
    .issync		= NgIsSync,
    .peeraddr		= NgPeerAddr,
    .callingnum		= NULL,
    .callednum		= NULL,
  };

  const struct cmdtab NgSetCmds[] = {
    { "node {path}",		"Set node to attach to",
	NgSetCommand, NULL, (void *) SET_NODE },
    { "hook {hook}",		"Set hook to attach to",
	NgSetCommand, NULL, (void *) SET_HOOK },
    { NULL },
  };

/*
 * NgInit()
 *
 * Initialize device-specific data in physical layer info
 */

static int
NgInit(Link l)
{
    NgInfo	ng;

    /* Allocate private struct */
    ng = (NgInfo) (l->info = Malloc(MB_PHYS, sizeof(*ng)));
    snprintf(ng->path, sizeof(ng->path), "undefined:");
    snprintf(ng->hook, sizeof(ng->hook), "undefined");

    /* Done */
    return(0);
}

/*
 * NgOpen()
 */

static void
NgOpen(Link l)
{
    NgInfo	const ng = (NgInfo) l->info;
    char	path[NG_PATHSIZ];
    int		csock = -1;
    struct ngm_connect	cn;

    if (!PhysGetUpperHook(l, path, cn.ourhook)) {
        Log(LG_PHYS, ("[%s] NG: can't get upper hook", l->name));
	goto fail;
    }
    
    /* Get a temporary netgraph socket node */
    if (NgMkSockNode(NULL, &csock, NULL) == -1) {
	Log(LG_ERR, ("[%s] NG: NgMkSockNode: %s", 
	    l->name, strerror(errno)));
	goto fail;
    }

    snprintf(cn.path, sizeof(cn.path), "%s", ng->path);
    snprintf(cn.peerhook, sizeof(cn.peerhook), "%s", ng->hook);
    if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_CONNECT, &cn, sizeof(cn)) < 0) {
	Log(LG_ERR, ("[%s] NG: can't connect \"%s\"->\"%s\" and \"%s\"->\"%s\": %s",
    	    l->name, path, cn.ourhook, cn.path, cn.peerhook, strerror(errno)));
	goto fail;
    }
    
    close(csock);
    l->state = PHYS_STATE_UP;
    PhysUp(l);
    return;

fail:
    if (csock>=0) {
	close(csock);
	csock = -1;
    }
    l->state = PHYS_STATE_DOWN;
    PhysDown(l, STR_CON_FAILED0, NULL);
}

/*
 * NgClose()
 */

static void
NgClose(Link l)
{
    NgInfo	const ng = (NgInfo) l->info;
    int		csock = -1;

    /* Get a temporary netgraph socket node */
    if (NgMkSockNode(NULL, &csock, NULL) == -1) {
	Log(LG_ERR, ("[%s] NG: NgMkSockNode: %s", 
	    l->name, strerror(errno)));
	goto fail;
    }

    NgFuncDisconnect(csock, l->name, ng->path, ng->hook);

    close(csock);
    /* FALL */

fail:
    l->state = PHYS_STATE_DOWN;
    PhysDown(l, 0, NULL);
}

/*
 * NgShutdown()
 */
static void
NgShutdown(Link l)
{
    Freee(MB_PHYS, l->info);
}

/*
 * NgStat()
 */

void
NgStat(Context ctx)
{
    NgInfo	const ng = (NgInfo) ctx->lnk->info;

    Printf("Netgraph node configuration:\r\n");
    Printf("\tNode : %s\r\n", ng->path);
    Printf("\tHook : %s\r\n", ng->hook);
}

/*
 * NgSetCommand()
 */

static int
NgSetCommand(Context ctx, int ac, char *av[], void *arg)
{
    NgInfo	const ng = (NgInfo) ctx->lnk->info;

    switch ((intptr_t)arg) {
	case SET_NODE:
    	    if (ac != 1)
		return(-1);
    	    snprintf(ng->path, sizeof(ng->path), "%s", av[0]);
    	    break;
	case SET_HOOK:
    	    if (ac != 1)
		return(-1);
    	    snprintf(ng->hook, sizeof(ng->hook), "%s", av[0]);
    	    break;
	default:
    	    assert(0);
    }
    return(0);
}

/*
 * NgIsSync()
 */

static int
NgIsSync(Link l)
{
    return (1);
}

/*
 * NgPeerAddr()
 */

static int
NgPeerAddr(Link l, void *buf, int buf_len)
{
    NgInfo	const ng = (NgInfo) l->info;

    if (buf_len < sizeof(ng->path))
	return(-1);

    memcpy(buf, ng->path, sizeof(ng->path));

    return(0);
}
