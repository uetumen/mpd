
/*
 * ccp_pred1.h
 *
 * Written by Archie Cobbs <archie@whistle.com>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#ifndef _PRED_H_
#define _PRED_H_

#include "defs.h"
#include "mbuf.h"
#include "comp.h"

/*
 * DEFINITIONS
 */

  #define PRED1_TABLE_SIZE	0x10000

  struct pred1info
  {
    u_short	iHash;
    u_short	oHash;
    u_char	*InputGuessTable;
    u_char	*OutputGuessTable;
  };
  typedef struct pred1info	*Pred1Info;

/*
 * VARIABLES
 */

  extern const struct compinfo	gCompPred1Info;

#endif

