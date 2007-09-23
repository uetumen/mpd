
/*
 * ccp.h
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1995-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#ifndef _CCP_H_
#define	_CCP_H_

#include "fsm.h"
#include "mbuf.h"
#include "comp.h"
#include "command.h"

#ifdef COMPRESSION_PRED1
#include "ccp_pred1.h"
#endif
#ifdef COMPRESSION_STAC
#include "ccp_stac.h"
#endif
#ifdef COMPRESSION_DEFLATE
#include "ccp_deflate.h"
#endif
#ifdef COMPRESSION_MPPC
#include "ccp_mppc.h"
#endif

#include <netgraph/ng_message.h>

/*
 * DEFINITIONS
 */

  /* Compression types */
  #define CCP_TY_OUI		0	/* OUI */
  #define CCP_TY_PRED1		1	/* Predictor type 1 */
  #define CCP_TY_PRED2		2	/* Predictor type 2 */
  #define CCP_TY_PUDDLE		3	/* Puddle Jumper */
  #define CCP_TY_HWPPC		16	/* Hewlett-Packard PPC */
  #define CCP_TY_STAC		17	/* Stac Electronics LZS */
  #define CCP_TY_MPPC		18	/* Microsoft PPC */
  #define CCP_TY_GAND		19	/* Gandalf FZA */
  #define CCP_TY_V42BIS		20	/* V.42bis compression */
  #define CCP_TY_BSD		21	/* BSD LZW Compress */
  #define CCP_TY_DEFLATE	24	/* Gzip "deflate" compression */

  /* CCP state */
  struct ccpstate {
    struct fsm		fsm;		/* CCP FSM */
    struct optinfo	options;	/* configured protocols */
    CompType		xmit;		/* xmit protocol */
    CompType		recv;		/* recv protocol */
    u_short		self_reject;	/* types rejected by me */
    u_short		peer_reject;	/* types rejected by peer */
#ifdef COMPRESSION_STAC
    struct stacinfo	stac;		/* STAC LZS state */
#endif
#ifdef COMPRESSION_PRED1
    struct pred1info	pred1;		/* Predictor-1 state */
#endif
#ifdef COMPRESSION_DEFLATE
    struct deflateinfo	deflate;	/* Deflate state */
#endif
#ifdef COMPRESSION_MPPC
    struct mppcinfo	mppc;		/* MPPC/MPPE state */
#endif
    u_char		crypt_check:1;	/* We checked for required encryption */
  };
  typedef struct ccpstate	*CcpState;

  #define CCP_PEER_REJECTED(p,x)	((p)->peer_reject & (1<<(x)))
  #define CCP_SELF_REJECTED(p,x)	((p)->self_reject & (1<<(x)))

  #define CCP_PEER_REJ(p,x)	do{(p)->peer_reject |= (1<<(x));}while(0)
  #define CCP_SELF_REJ(p,x)	do{(p)->self_reject |= (1<<(x));}while(0)

/*
 * VARIABLES
 */

  extern const struct cmdtab	CcpSetCmds[];

  extern int			gMppcCompress;
  extern int			gMppe40;
  extern int			gMppe56;
  extern int			gMppe128;
  extern int			gMppcStateless;
  extern int			gCcpRadius;

/*
 * FUNCTIONS
 */

  extern void	CcpInit(void);
  extern void	CcpUp(void);
  extern void	CcpDown(void);
  extern void	CcpOpen(void);
  extern void	CcpClose(void);
  extern void	CcpInput(Mbuf bp, int linkNum);
  extern int	CcpSubtractBloat(int size);
  extern void	CcpSendResetReq(void);
  extern void	CcpRecvMsg(struct ng_mesg *msg, int len);
  extern int	CcpStat(int ac, char *av[], void *arg);

#endif
