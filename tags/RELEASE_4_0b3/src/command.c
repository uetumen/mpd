
/*
 * command.c
 *
 * Written by Toshiharu OHNO <tony-o@iij.ad.jp>
 * Copyright (c) 1993, Internet Initiative Japan, Inc. All rights reserved.
 * See ``COPYRIGHT.iij''
 * 
 * Rewritten by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#include "ppp.h"
#include "console.h"
#include "command.h"
#include "ccp.h"
#include "iface.h"
#include "radius.h"
#include "bund.h"
#include "link.h"
#include "lcp.h"
#include "ipcp.h"
#include "ip.h"
#include "devices.h"
#include "netgraph.h"
#include "custom.h"
#include "ngfunc.h"

/*
 * DEFINITIONS
 */

  struct layer {
    const char	*name;
    void	(*opener)(void);
    void	(*closer)(void);
    const char	*desc;
  };
  typedef struct layer	*Layer;

  #define DEFAULT_OPEN_LAYER	"iface"

  /* Set menu options */
  enum {
    SET_ENABLE,
    SET_DISABLE,
  };


/*
 * INTERNAL FUNCTIONS
 */

  /* Commands */
  static int	ShowVersion(int ac, char *av[], void *arg);
  static int	ShowLayers(int ac, char *av[], void *arg);
  static int	ShowTypes(int ac, char *av[], void *arg);
  static int	ShowEvents(int ac, char *av[], void *arg);
  static int	ShowGlobals(int ac, char *av[], void *arg);
  static int	OpenCommand(int ac, char *av[], void *arg);
  static int	CloseCommand(int ac, char *av[], void *arg);
  static int	LoadCommand(int ac, char *av[], void *arg);
  static int	ExitCommand(int ac, char *av[], void *arg);
  static int	QuitCommand(int ac, char *av[], void *arg);
  static int	NullCommand(int ac, char *av[], void *arg);
  static int	GlobalSetCommand(int ac, char *av[], void *arg);
  static int	SetDebugCommand(int ac, char *av[], void *arg);

  /* Other stuff */
  static int	DoCommandTab(CmdTab cmdlist, int ac, char *av[]);
  const	char *FindCommand(CmdTab cmds,
			char* str, CmdTab *cp, int complain);
  static Layer	GetLayer(const char *name);

/*
 * INTERNAL VARIABLES
 */

  static int	exitflag;

  const struct cmdtab GlobalSetCmds[] = {
    { "enable [opt ...]", 		"Enable option" ,
       	GlobalSetCommand, NULL, (void *) SET_ENABLE },
    { "disable [opt ...]", 		"Disable option" ,
       	GlobalSetCommand, NULL, (void *) SET_DISABLE },
    { NULL },
  };

  static const struct confinfo	gGlobalConfList[] = {
    { 0,	GLOBAL_CONF_TCPWRAPPER,	"tcp-wrapper"	},
    { 0,	0,			NULL		},
  };

  static const struct cmdtab ShowCommands[] = {
    { "bundle [name]",			"Bundle status",
	BundStat, AdmitBund, NULL },
    { "ccp",				"CCP status",
	CcpStat, AdmitBund, NULL },
    { "ecp",				"ECP status",
	EcpStat, AdmitBund, NULL },
    { "eap",				"EAP status",
	EapStat, AdmitBund, NULL },
    { "events",				"Current events",
	ShowEvents, NULL, NULL },
    { "ipcp",				"IPCP status",
	IpcpStat, AdmitBund, NULL },
    { "iface",				"Interface status",
	IfaceStat, NULL, NULL },
    { "routes",				"IP routing table",
	IpShowRoutes, NULL, NULL },
    { "layers",				"Layers to open/close",
	ShowLayers, NULL, NULL },
    { "link",				"Link status",
	LinkStat, AdmitBund, NULL },
    { "auth",				"Auth status",
	AuthStat, AdmitBund, NULL },
    { "radius",				"RADIUS status",
	RadStat, AdmitBund, NULL },
    { "lcp",				"LCP status",
	LcpStat, AdmitBund, NULL },
    { "mem",				"Memory map",
	MemStat, NULL, NULL },
    { "mp",				"Multi-link status",
	MpStat, AdmitBund, NULL },
    { "console",			"Console status",
	ConsoleStat, NULL, NULL },
    { "globals",			"Global settings",
	ShowGlobals, NULL, NULL },
    { "types",				"Supported device types",
	ShowTypes, NULL, NULL },
    { "version",			"Version string",
	ShowVersion, NULL, NULL },
    { NULL },
  };

  static const struct cmdtab SetCommands[] = {
    { "bundle ...",			"Bundle specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) BundSetCmds },
    { "link ...",			"Link specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) LinkSetCmds },
    { "iface ...",			"Interface specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) IfaceSetCmds },
    { "ipcp ...",			"IPCP specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) IpcpSetCmds },
    { "ccp ...",			"CCP specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) CcpSetCmds },
    { "ecp ...",			"ECP specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) EcpSetCmds },
    { "eap ...",			"EAP specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) EapSetCmds },
    { "auth ...",			"Auth specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) AuthSetCmds },
    { "radius ...",			"RADIUS specific stuff",
	CMD_SUBMENU, AdmitBund, (void *) RadiusSetCmds },
    { "console ...",			"Console specific stuff",
	CMD_SUBMENU, NULL, (void *) ConsoleSetCmds },
    { "global ...",			"Global settings",
	CMD_SUBMENU, NULL, (void *) GlobalSetCmds },
#ifdef USE_NG_NETFLOW
    { "netflow ...", 			"NetFlow settings",
	CMD_SUBMENU, NULL, (void *) NetflowSetCmds },
#endif
    { "debug level",			"Set netgraph debug level",
	SetDebugCommand, NULL, NULL },
#define _WANT_DEVICE_CMDS
#include "devices.h"
    { NULL },
  };

  const struct cmdtab gCommands[] = {
    { "new [-nti ng#] bundle link ...",	"Create new bundle",
    	BundCreateCmd, NULL, NULL },
    { "bundle [name]",			"Choose/list bundles",
	BundCommand, AdmitBund, NULL },
    { "custom ...",			"Custom stuff",
	CMD_SUBMENU, NULL, (void *) CustomCmds },
    { "link name",			"Choose link",
	LinkCommand, AdmitBund, NULL },
    { "open [layer]",			"Open a layer",
	OpenCommand, AdmitBund, NULL },
    { "close [layer]",			"Close a layer",
	CloseCommand, AdmitBund, NULL },
    { "load label",			"Read from config file",
	LoadCommand, NULL, NULL },
    { "set ...",			"Set parameters",
	CMD_SUBMENU, NULL, (void *) SetCommands },
    { "show ...",			"Show status",
	CMD_SUBMENU, NULL, (void *) ShowCommands },
    { "exit",				"Exit console",
	ExitCommand, NULL, NULL },
    { "null",				"Do nothing",
	NullCommand, NULL, NULL },
    { "log [+/-opt ...]",		"Set/view log options",
	LogCommand, NULL, NULL },
    { "quit",				"Quit program",
	QuitCommand, NULL, NULL },
    { "help ...",			"Help on any command",
	HelpCommand, NULL, NULL },
    { NULL },
  };



/*
 * Layers
 */

  struct layer	gLayers[] = {
    { "iface",
      IfaceOpen,
      IfaceClose,
      "System interface"
    },
    { "ipcp",
      IpcpOpen,
      IpcpClose,
      "IPCP: IP control protocol"
    },
    { "ccp",
      CcpOpen,
      CcpClose,
      "CCP: compression ctrl prot."
    },
    { "ecp",
      EcpOpen,
      EcpClose,
      "ECP: encryption ctrl prot."
    },
    { "bund",
      BundOpen,
      BundClose,
      "Multilink bundle"
    },
    { "lcp",
      LcpOpen,
      LcpClose,
      "LCP: link control protocol"
    },
    { "phys",
      PhysOpen,
      PhysClose,
      "Physical link layer"
    },
  };

  #define NUM_LAYERS	(sizeof(gLayers) / sizeof(*gLayers))

/*
 * DoCommand()
 *
 * Executes command. Returns TRUE if user wants to quit.
 */

int
DoCommand(int ac, char *av[])
{
  exitflag = FALSE;
  DoCommandTab(gCommands, ac, av);
  return(exitflag);
}

/*
 * DoCommandTab()
 *
 * Execute command from given command menu
 */

static int
DoCommandTab(CmdTab cmdlist, int ac, char *av[])
{
  CmdTab	cmd;
  int		rtn = 0;

  /* Huh? */
  if (ac <= 0)
    return(-1);

  /* Find command */
  if (FindCommand(cmdlist, av[0], &cmd, cmdlist == gCommands))
    return(-1);

  /* Check command admissibility */
  if (cmd->admit && !(cmd->admit)(cmd))
    return(0);

  /* Find command and either execute or recurse into a submenu */
  if (cmd->func == CMD_SUBMENU)
    rtn = DoCommandTab((CmdTab) cmd->arg, ac - 1, av + 1);
  else if (cmd->func == CMD_UNIMPL)
    Log(LG_ERR, ("mpd: %s: unimplemented command", av[0]));
  else
    rtn = (cmd->func)(ac - 1, av + 1, cmd->arg);

  /* Bad usage? */
  if (cmdlist == gCommands && rtn < 0)
    HelpCommand(ac, av, NULL);
  return(rtn);
}

/*
 * FindCommand()
 */

const char *
FindCommand(CmdTab cmds, char *str, CmdTab *cmdp, int complain)
{
  int		found, nmatch;
  int		len = strlen(str);
  const char	*fmt;

  for (nmatch = 0, found = 0; cmds->name; cmds++) {
    if (cmds->name && !strncmp(str, cmds->name, len)) {
      *cmdp = cmds;
      nmatch++;
    }
  }
  switch (nmatch) {
    case 0:
      fmt = "%s: unknown command. Try \"help\".";
      if (complain)
	Log(LG_ERR, (fmt, str));
      return(fmt);
    case 1:
      return(NULL);
    default:
      fmt = "%s: ambiguous command";
      if (complain)
	Log(LG_ERR, (fmt, str));
      return(fmt);
  }
}

/********** COMMANDS **********/


/*
 * GlobalSetCommand()
 */

static int
GlobalSetCommand(int ac, char *av[], void *arg) 
{
  if (ac == 0)
    return(-1);

  switch ((int) arg) {
    case SET_ENABLE:
      EnableCommand(ac, av, &gGlobalConf.options, gGlobalConfList);
      break;

    case SET_DISABLE:
      DisableCommand(ac, av, &gGlobalConf.options, gGlobalConfList);
      break;

    default:
      return(-1);
  }

  return 0;
}

/*
 * HelpCommand()
 */

int
HelpCommand(int ac, char *av[], void *arg)
{
  int		depth;
  CmdTab	menu, cmd;
  char		*mark, *mark_save;
  const char	*errfmt;
  char		buf[100];

  for (mark = buf, depth = *buf = 0, menu = gCommands;
      depth < ac;
      depth++, menu = (CmdTab) cmd->arg) {
    if ((errfmt = FindCommand(menu, av[depth], &cmd, FALSE))) {
      int k;

      for (*buf = k = 0; k <= depth; k++)
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s%c",
	  av[k], k == depth ? '\0' : ' ');
      Log(LG_ERR, (errfmt, buf));
      return(0);
    }
    sprintf(mark, depth ? " %s" : "%s", cmd->name);
    mark_save = mark;
    if ((mark = strchr(mark + 1, ' ')) == NULL)
      mark = mark_save + strlen(mark_save);
    if (cmd->func != CMD_SUBMENU)
    {
      Printf("Usage: %s\r\n", buf);
      return(0);
    }
  }

  /* Show list of available commands in this submenu */
  *mark = 0;
  if (!*buf)
    Printf("Available commands:\r\n");
  else
    Printf("Commands available under \"%s\":\r\n", buf);
  for (cmd = menu; cmd->name; cmd++) {
    snprintf(buf, sizeof(buf), "%s", cmd->name);
    if ((mark = strchr(buf, ' ')))
      *mark = 0;
    Printf(" %-9s: %-20s%s", buf, cmd->desc,
      ((cmd - menu) & 1)? "\r\n" : "\t");
  }
  if ((cmd - menu) & 1)
    Printf("\r\n");
  return(0);
}

/*
 * SetDebugCommand()
 */

static int
SetDebugCommand(int ac, char *av[], void *arg)
{
  switch (ac) {
    case 1:
      NgSetDebug(atoi(av[0]));
      break;
    default:
      return(-1);
  }
  return(0);
}

/*
 * ShowVersion()
 */

static int
ShowVersion(int ac, char *av[], void *arg)
{
  Printf("MPD version: %s\r\n", gVersion);
  return(0);
}

/*
 * ShowEvents()
 */

static int
ShowEvents(int ac, char *av[], void *arg)
{
  EventDump("mpd events");
  return(0);
}

/*
 * ShowGlobals()
 */

static int
ShowGlobals(int ac, char *av[], void *arg)
{
  Printf("Global settings:\r\n");
  OptStat(&gGlobalConf.options, gGlobalConfList);
  return 0;
}


/*
 * ExitCommand()
 */

static int
ExitCommand(int ac, char *av[], void *arg)
{
  exitflag = TRUE;
  return(0);
}

/*
 * QuitCommand()
 */

static int
QuitCommand(int ac, char *av[], void *arg)
{
  RecordLinkUpDownReason(NULL, 0, STR_QUIT, NULL);
  DoExit(EX_NORMAL);
  return(0);
}

/*
 * NullCommand()
 */

static int
NullCommand(int ac, char *av[], void *arg)
{
  return(0);
}

/*
 * LoadCommand()
 */

static int
LoadCommand(int ac, char *av[], void *arg)
{
  if (ac != 1)
    Log(LG_ERR, ("Usage: load system"));
  else {
    if (ReadFile(gConfigFile, *av, DoCommand) < 0)
      Log(LG_ERR, ("mpd: entry \"%s\" not found in %s",
        *av, gConfigFile));
  }
  return(0);
}

/*
 * OpenCommand()
 */

static int
OpenCommand(int ac, char *av[], void *arg)
{
  Layer		layer;
  const char	*name;

  switch (ac) {
    case 0:
      name = DEFAULT_OPEN_LAYER;
      break;
    case 1:
      name = av[0];
      break;
    default:
      return(-1);
  }
  if ((layer = GetLayer(name)) != NULL)
    (*layer->opener)();
  return(0);
}

/*
 * CloseCommand()
 */

static int
CloseCommand(int ac, char *av[], void *arg)
{
  Layer		layer;
  const char	*name;

  switch (ac) {
    case 0:
      name = DEFAULT_OPEN_LAYER;
      break;
    case 1:
      name = av[0];
      break;
    default:
      return(-1);
  }
  if ((layer = GetLayer(name)) != NULL)
    (*layer->closer)();
  return(0);
}

/*
 * GetLayer()
 */

static Layer
GetLayer(const char *name)
{
  int	k, found;

  if (name == NULL)
    name = "iface";
  for (found = -1, k = 0; k < NUM_LAYERS; k++) {
    if (!strncasecmp(name, gLayers[k].name, strlen(name))) {
      if (found > 0) {
	Log(LG_ERR, ("%s: ambiguous", name));
	return(NULL);
      } else
	found = k;
    }
  }
  if (found < 0) {
    Log(LG_ERR, ("mpd: unknown layer \"%s\": try \"show layers\"", name));
    return(NULL);
  }
  return(&gLayers[found]);
}

/*
 * ShowLayers()
 */

static int
ShowLayers(int ac, char *av[], void *arg)
{
  int	k;

  Printf("\tName\t\tDescription\r\n"
	 "\t----\t\t-----------\r\n");
  for (k = 0; k < NUM_LAYERS; k++)
    Printf("\t%s\t\t%s\r\n", gLayers[k].name, gLayers[k].desc);
  return(0);
}

/*
 * ShowTypes()
 */

static int
ShowTypes(int ac, char *av[], void *arg)
{
  PhysType	pt;
  int		k;

  Printf("Supported device types:\r\n\t");
  for (k = 0; (pt = gPhysTypes[k]); k++)
    Printf(" %s", pt->name);
  Printf("\r\n");
  return(0);
}

/*
 * AdmitBund()
 */

int
AdmitBund(CmdTab cmd)
{
  if (!bund) {
    Log(LG_ERR, ("mpd: no bundles defined"));
    return(FALSE);
  }
  return(TRUE);
}

/*
 * AdmitDev()
 */

int
AdmitDev(CmdTab cmd)
{
  if (!AdmitBund(cmd))
    return(FALSE);
  if (lnk->phys->type == NULL) {
    Log(LG_ERR, ("mpd: type of link \"%s\" is unspecified", lnk->name));
    return(FALSE);
  }
  if (!strcmp(cmd->name, lnk->phys->type->name)) {
    Log(LG_ERR, ("mpd: link \"%s\" is type %s, not %s",
      lnk->name, lnk->phys->type->name, cmd->name));
    return(FALSE);
  }
  return(TRUE);
}
