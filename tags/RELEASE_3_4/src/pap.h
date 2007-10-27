
/*
 * pap.h
 *
 * Written by Toshiharu OHNO <tony-o@iij.ad.jp>
 * Copyright (c) 1993, Internet Initiative Japan, Inc. All rights reserved.
 * See ``COPYRIGHT.iij''
 * 
 * Rewritten by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#ifndef _PAP_H_
#define	_PAP_H_

#include "mbuf.h"
#include "timer.h"

/*
 * DEFINITIONS
 */

  struct papinfo {
    short		next_id;			/* Packet id */
    short		retry;				/* Resend count */
    struct pppTimer	timer;				/* Resend timer */
  };
  typedef struct papinfo	*PapInfo;

  struct authprot;

/*
 * FUNCTIONS
 */

  extern void	PapStart(PapInfo pap, int which);
  extern void	PapStop(PapInfo pap);
  extern void	PapInput(Mbuf bp);

#endif
