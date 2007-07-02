
/*
 * ccp_mppc.c
 *
 * Written by Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 1998-1999 Whistle Communications, Inc. All rights reserved.
 * See ``COPYRIGHT.whistle''
 */

#include "ppp.h"
#include "ccp.h"
#include "msoft.h"
#include "ngfunc.h"
#include "bund.h"
#include <md4.h>

#include <netgraph/ng_message.h>
#ifdef __DragonFly__
#include <netgraph/ppp/ng_ppp.h>
#else
#include <netgraph/ng_ppp.h>
#endif
#include <netgraph.h>

/*
 * This implements both MPPC compression and MPPE encryption.
 */

/*
 * DEFINITIONS
 */

  /* #define DEBUG_KEYS */

#define MPPC_SUPPORTED	(MPPC_BIT | MPPE_BITS | MPPE_STATELESS)

/*
 * INTERNAL FUNCTIONS
 */

  static int	MppcInit(int dir);
  static char	*MppcDescribe(int xmit);
  static int	MppcSubtractBloat(int size);
  static void	MppcCleanup(int dir);
  static u_char	*MppcBuildConfigReq(u_char *cp);
  static void	MppcDecodeConfigReq(Fsm fp, FsmOption opt, int mode);
  static Mbuf	MppcRecvResetReq(int id, Mbuf bp, int *noAck);
  static char	*MppcDescribeBits(u_int32_t bits);
  static int	MppcNegotiated(int xmit);

  /* Encryption stuff */
  static void	MppeInitKey(MppcInfo mppc, int dir);
  static void	MppeInitKeyv2(MppcInfo mppc, int dir);
  static short	MppcEnabledMppeType(short type);
  static short	MppcAcceptableMppeType(short type);

#ifdef DEBUG_KEYS
  static void	KeyDebug(const u_char *data, int len, const char *fmt, ...);
  #define KEYDEBUG(x)	KeyDebug x
#else
  #define KEYDEBUG(x)
#endif

/*
 * GLOBAL VARIABLES
 */

  const struct comptype	gCompMppcInfo = {
    "mppc",
    CCP_TY_MPPC,
    MppcInit,
    NULL,
    MppcDescribe,
    MppcSubtractBloat,
    MppcCleanup,
    MppcBuildConfigReq,
    MppcDecodeConfigReq,
    NULL,
    MppcRecvResetReq,
    NULL,
    MppcNegotiated,
  };

/*
 * MppcInit()
 */

static int
MppcInit(int dir)
{
  MppcInfo		const mppc = &bund->ccp.mppc;
  struct ng_mppc_config	conf;
  struct ngm_mkpeer	mp;
  char			path[NG_PATHLEN + 1];
  const char		*mppchook, *ppphook;
  int			mschap;
  int			cmd;

  /* Which type of MS-CHAP did we do? */
  if (bund->links[0]->originate == LINK_ORIGINATE_LOCAL)
    mschap = lnk->lcp.peer_chap_alg;
  else
    mschap = lnk->lcp.want_chap_alg;

  /* Initialize configuration structure */
  memset(&conf, 0, sizeof(conf));
  conf.enable = 1;
  switch (dir) {
    case COMP_DIR_XMIT:
      cmd = NGM_MPPC_CONFIG_COMP;
      ppphook = NG_PPP_HOOK_COMPRESS;
      mppchook = NG_MPPC_HOOK_COMP;
      conf.bits = mppc->xmit_bits;
      if (mschap == CHAP_ALG_MSOFT)
	MppeInitKey(mppc, dir);
      else
        MppeInitKeyv2(mppc, dir);
      memcpy(conf.startkey, mppc->xmit_key0, sizeof(conf.startkey));
      break;
    case COMP_DIR_RECV:
      cmd = NGM_MPPC_CONFIG_DECOMP;
      ppphook = NG_PPP_HOOK_DECOMPRESS;
      mppchook = NG_MPPC_HOOK_DECOMP;
      conf.bits = mppc->recv_bits;
      if (mschap == CHAP_ALG_MSOFT)
	MppeInitKey(mppc, dir);
      else
	MppeInitKeyv2(mppc, dir);
      memcpy(conf.startkey, mppc->recv_key0, sizeof(conf.startkey));
      break;
    default:
      assert(0);
      return(-1);
  }

  /* Attach a new MPPC node to the PPP node */
  snprintf(mp.type, sizeof(mp.type), "%s", NG_MPPC_NODE_TYPE);
  snprintf(mp.ourhook, sizeof(mp.ourhook), "%s", ppphook);
  snprintf(mp.peerhook, sizeof(mp.peerhook), "%s", mppchook);
  if (NgSendMsg(bund->csock, MPD_HOOK_PPP,
      NGM_GENERIC_COOKIE, NGM_MKPEER, &mp, sizeof(mp)) < 0) {
    Log(LG_ERR, ("[%s] can't create %s node: %s",
      bund->name, mp.type, strerror(errno)));
    return(-1);
  }

  /* Configure MPPC node */
  snprintf(path, sizeof(path), "%s.%s", MPD_HOOK_PPP, ppphook);
  if (NgSendMsg(bund->csock, path,
      NGM_MPPC_COOKIE, cmd, &conf, sizeof(conf)) < 0) {
    Log(LG_ERR, ("[%s] can't config %s node at %s: %s",
      bund->name, NG_MPPC_NODE_TYPE, path, strerror(errno)));
    NgFuncDisconnect(MPD_HOOK_PPP, ppphook);
    return(-1);
  }

  /* Done */
  return(0);
}

/*
 * MppcDescribe()
 */

static char *
MppcDescribe(int dir)
{
  MppcInfo	const mppc = &bund->ccp.mppc;

  switch (dir) {
    case COMP_DIR_XMIT:
      return(MppcDescribeBits(mppc->xmit_bits));
    case COMP_DIR_RECV:
      return(MppcDescribeBits(mppc->recv_bits));
    default:
      assert(0);
      return(NULL);
  }
}

/*
 * MppcSubtractBloat()
 */

static int
MppcSubtractBloat(int size)
{

  /* Account for MPPC header */
  size -= 2;

  /* Account for possible expansion with MPPC compression */
  if ((bund->ccp.mppc.xmit_bits & MPPC_BIT) != 0) {
    int	l, h, size0 = size;

    while (1) {
      l = MPPC_MAX_BLOWUP(size0);
      h = MPPC_MAX_BLOWUP(size0 + 1);
      if (l > size) {
	size0 -= 20;
      } else if (h > size) {
	size = size0;
	break;
      } else {
	size0++;
      }
    }
  }

  /* Done */
  return(size);
}

/*
 * MppcNegotiated()
 */

static int
MppcNegotiated(int dir)
{
  MppcInfo	const mppc = &bund->ccp.mppc;

  switch (dir) {
    case COMP_DIR_XMIT:
      return(mppc->xmit_bits != 0);
    case COMP_DIR_RECV:
      return(mppc->recv_bits != 0);
    default:
      assert(0);
      return(0);
  }
}

/*
 * MppcCleanup()
 */

static void
MppcCleanup(int dir)
{
  const char	*ppphook;
  char		path[NG_PATHLEN + 1];

  /* Remove node */
  switch (dir) {
    case COMP_DIR_XMIT:
      ppphook = NG_PPP_HOOK_DECOMPRESS;
      break;
    case COMP_DIR_RECV:
      ppphook = NG_PPP_HOOK_COMPRESS;
      break;
    default:
      assert(0);
      return;
  }
  snprintf(path, sizeof(path), "%s.%s", MPD_HOOK_PPP, ppphook);
  (void)NgFuncShutdownNode(bund, bund->name, path);
}

/*
 * MppcBuildConfigReq()
 */

static u_char *
MppcBuildConfigReq(u_char *cp)
{
  CcpState	const ccp = &bund->ccp;
  MppcInfo	const mppc = &ccp->mppc;
  u_int32_t	bits = 0;

  /* Compression */
  if (Enabled(&ccp->options, gMppcCompress)
      && !CCP_PEER_REJECTED(ccp, gMppcCompress))
    bits |= MPPC_BIT;

  /* Encryption */
  if (MppcEnabledMppeType(40)) bits |= MPPE_40;
#ifndef MPPE_56_UNSUPPORTED
  if (MppcEnabledMppeType(56)) bits |= MPPE_56;
#endif
  if (MppcEnabledMppeType(128)) bits |= MPPE_128;

  /* Stateless mode */
  if (Enabled(&ccp->options, gMppcStateless)
      && !CCP_PEER_REJECTED(ccp, gMppcStateless)
      && bits != 0)
    bits |= MPPE_STATELESS;

  /* Ship it */
  mppc->recv_bits = bits;
  if (bits != 0)
    cp = FsmConfValue(cp, CCP_TY_MPPC, -4, &bits);
  return(cp);
}

/*
 * MppcDecodeConfigReq()
 */

static void
MppcDecodeConfigReq(Fsm fp, FsmOption opt, int mode)
{
  CcpState	const ccp = &bund->ccp;
  MppcInfo	const mppc = &ccp->mppc;
  u_int32_t	orig_bits;
  u_int32_t	bits;

  /* Get bits */
  memcpy(&orig_bits, opt->data, 4);
  orig_bits = ntohl(orig_bits);
  bits = orig_bits;

  /* Sanity check */
  if (opt->len != 6) {
    Log(LG_CCP, ("   bogus length %d", opt->len));
    if (mode == MODE_REQ)
      FsmRej(fp, opt);
    return;
  }

  /* Display it */
  Log(LG_CCP, ("   0x%08x:%s", bits, MppcDescribeBits(bits)));

  /* Deal with it */
  switch (mode) {
    case MODE_REQ:

      /* Check for supported bits */
      if (bits & ~MPPC_SUPPORTED) {
	Log(LG_CCP, ("   Bits 0x%08x not supported", bits & ~MPPC_SUPPORTED));
	bits &= MPPC_SUPPORTED;
      }

      /* Check compression */
      if ((bits & MPPC_BIT) && !Acceptable(&ccp->options, gMppcCompress))
	bits &= ~MPPC_BIT;

      /* Check encryption */
      if ((bits & MPPE_40) && !MppcAcceptableMppeType(40))
	bits &= ~MPPE_40;
#ifndef MPPE_56_UNSUPPORTED
      if ((bits & MPPE_56) && !MppcAcceptableMppeType(56))
#endif
	bits &= ~MPPE_56;
      if ((bits & MPPE_128) && !MppcAcceptableMppeType(128))
	bits &= ~MPPE_128;

      /* Choose the strongest encryption available */
      if (bits & MPPE_128)
	bits &= ~(MPPE_40|MPPE_56);
      else if (bits & MPPE_56)
	bits &= ~(MPPE_40|MPPE_128);
      else if (bits & MPPE_40)
	bits &= ~(MPPE_56|MPPE_128);

      /* It doesn't really make sense to encrypt in only one direction.
	 Also, Win95/98 PPTP can't handle uni-directional encryption. So
	 if the remote side doesn't request encryption, try to prompt it.
	 This is broken wrt. normal PPP negotiation: typical Microsoft. */
      if ((bits & MPPE_BITS) == 0) {
	if (MppcEnabledMppeType(40)) bits |= MPPE_40;
#ifndef MPPE_56_UNSUPPORTED
	if (MppcEnabledMppeType(56)) bits |= MPPE_56;
#endif
	if (MppcEnabledMppeType(128)) bits |= MPPE_128;
      }

      /* Stateless mode */
      if ((bits & MPPE_STATELESS) && !Acceptable(&ccp->options, gMppcStateless))
	bits &= ~MPPE_STATELESS;

      /* See if what we want equals what was sent */
      mppc->xmit_bits = bits;
      if (bits != orig_bits) {
	bits = htonl(bits);
	memcpy(opt->data, &bits, 4);
	FsmNak(fp, opt);
      }
      else
	FsmAck(fp, opt);
      break;

    case MODE_NAK:
      if (!(bits & MPPC_BIT))
	CCP_PEER_REJ(ccp, gMppcCompress);
      if (!(bits & MPPE_40))
	CCP_PEER_REJ(ccp, gMppe40);
      if (!(bits & MPPE_56))
	CCP_PEER_REJ(ccp, gMppe56);
      if (!(bits & MPPE_128))
	CCP_PEER_REJ(ccp, gMppe128);
      if (!(bits & MPPE_STATELESS))
	CCP_PEER_REJ(ccp, gMppcStateless);
      break;
  }
}

/*
 * MppcRecvResetReq()
 */

static Mbuf
MppcRecvResetReq(int id, Mbuf bp, int *noAck)
{
  char	path[NG_PATHLEN + 1];

  /* Forward ResetReq to the MPPC compression node */
  snprintf(path, sizeof(path), "%s.%s", MPD_HOOK_PPP, NG_PPP_HOOK_COMPRESS);
  if (NgSendMsg(bund->csock, path,
      NGM_MPPC_COOKIE, NGM_MPPC_RESETREQ, NULL, 0) < 0) {
    Log(LG_ERR, ("[%s] reset-req to %s node: %s",
      bund->name, NG_MPPC_NODE_TYPE, strerror(errno)));
  }

  /* No ResetAck required for MPPC */
  if (noAck)
    *noAck = 1;
  return(NULL);
}

/*
 * MppcDescribeBits()
 */

static char *
MppcDescribeBits(u_int32_t bits)
{
  static char	buf[100];

  *buf = 0;
  if (bits & MPPC_BIT)
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " MPPC");
  if (bits & MPPE_BITS) {
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " MPPE");
    if (bits & MPPE_40)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ", 40 bit");
    if (bits & MPPE_56)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ", 56 bit");
    if (bits & MPPE_128)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ", 128 bit");
    if (bits & MPPE_STATELESS)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ", stateless");
  }
  return(buf);
}

static short
MppcEnabledMppeType(short type)
{
  CcpState	const ccp = &bund->ccp;
  Auth		const a = &lnk->lcp.auth;
  short		ret, policy_auth = FALSE;
 
  switch (type) {
  case 40:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;
      ret = (a->msoft.types & MPPE_TYPE_40BIT) && !CCP_PEER_REJECTED(ccp, gMppe40);
    } else {
      ret = Enabled(&ccp->options, gMppe40) && !CCP_PEER_REJECTED(ccp, gMppe40);
    }
    break;

#ifndef MPPE_56_UNSUPPORTED
  case 56:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;    
      ret = (a->msoft.types & MPPE_TYPE_56BIT) && !CCP_PEER_REJECTED(ccp, gMppe56);
    } else {
      ret = Enabled(&ccp->options, gMppe56) && !CCP_PEER_REJECTED(ccp, gMppe56);
    }

    break;
#endif
      
  case 128:
  default:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;    
      ret = (a->msoft.types & MPPE_TYPE_128BIT) && !CCP_PEER_REJECTED(ccp, gMppe128);
    } else {
      ret = Enabled(&ccp->options, gMppe128) && !CCP_PEER_REJECTED(ccp, gMppe128);
    }
  }
  Log(LG_CCP, ("[%s] CCP: Checking whether %d bits are enabled -> %s%s", 
    lnk->name, type, ret ? "yes" : "no", policy_auth ? " (AUTH)" : "" ));
  return ret;
}

static short
MppcAcceptableMppeType(short type)
{
  CcpState	const ccp = &bund->ccp;
  Auth		const a = &lnk->lcp.auth;
  short		ret, policy_auth = FALSE;
  
  switch (type) {
  case 40:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;
      ret = a->msoft.types & MPPE_TYPE_40BIT;
    } else {
      ret = Acceptable(&ccp->options, gMppe40);
    }
    break;

#ifndef MPPE_56_UNSUPPORTED
  case 56:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;
      ret = a->msoft.types & MPPE_TYPE_56BIT;
    } else {
      ret = Acceptable(&ccp->options, gMppe56);
    }

    break;
#endif
      
  case 128:
  default:
    if (Enabled(&bund->conf.auth.options, AUTH_CONF_MPPC_POL)) {
      policy_auth = TRUE;    
      ret = a->msoft.types & MPPE_TYPE_128BIT;
    } else {
      ret = Acceptable(&ccp->options, gMppe128);
    }
  }

  Log(LG_CCP, ("[%s] CCP: Checking whether %d bits are acceptable -> %s%s",
    lnk->name, type, ret ? "yes" : "no", policy_auth ? " (AUTH)" : "" ));
  return ret;

}

#define KEYLEN(b)	(((b) & MPPE_128) ? 16 : 8)

/*
 * MppeInitKey()
 */

static void
MppeInitKey(MppcInfo mppc, int dir)
{
  CcpState	const ccp = &bund->ccp;
  u_int32_t	const bits = (dir == COMP_DIR_XMIT) ?
			mppc->xmit_bits : mppc->recv_bits;
  u_char	*const key0 = (dir == COMP_DIR_XMIT) ?
			mppc->xmit_key0 : mppc->recv_key0;
  u_char	hash[16];
  u_char	*chal;

  /* The secret comes from the originating caller's credentials */
  switch (lnk->originate) {
    case LINK_ORIGINATE_LOCAL:
      chal = bund->ccp.mppc.peer_msChal;
      break;
    case LINK_ORIGINATE_REMOTE:
      chal = bund->ccp.mppc.self_msChal;
      break;
    case LINK_ORIGINATE_UNKNOWN:
    default:
      Log(LG_ERR, ("[%s] can't determine link direction for MPPE", lnk->name));
      goto fail;
  }

  /* Compute basis for the session key (ie, "start key" or key0) */
  if (bits & MPPE_128) {
    if (!lnk->lcp.auth.msoft.has_nt_hash) {
      Log(LG_ERR, ("[%s] The NT-Hash is not set, but needed for MS-CHAPv1 and MPPE 128", 
        lnk->name));
      goto fail;
    }
    memcpy(hash, lnk->lcp.auth.msoft.nt_hash_hash, sizeof(hash));
    KEYDEBUG((hash, sizeof(hash), "NT Password Hash Hash"));
    KEYDEBUG((chal, CHAP_MSOFT_CHAL_LEN, "Challenge"));
    MsoftGetStartKey(chal, hash);
    KEYDEBUG((hash, sizeof(hash), "NT StartKey"));
  } else {
    if (!lnk->lcp.auth.msoft.has_lm_hash) {
      Log(LG_ERR, ("[%s] The LM-Hash is not set, but needed for MS-CHAPv1 and MPPE 40, 56", 
        lnk->name));
      goto fail;
    }

    memcpy(hash, lnk->lcp.auth.msoft.lm_hash, 8);
    KEYDEBUG((hash, sizeof(hash), "LM StartKey"));
  }
  memcpy(key0, hash, MPPE_KEY_LEN);
  KEYDEBUG((key0, (bits & MPPE_128) ? 16 : 8, "InitialKey"));
  return;

fail:
  FsmFailure(&ccp->fsm, FAIL_CANT_ENCRYPT);
  FsmFailure(&bund->ipcp.fsm, FAIL_CANT_ENCRYPT);
}

/*
 * MppeInitKeyv2()
 */

static void
MppeInitKeyv2(MppcInfo mppc, int dir)
{
  CcpState	const ccp = &bund->ccp;
  u_char	*const key0 = (dir == COMP_DIR_XMIT) ?
			mppc->xmit_key0 : mppc->recv_key0;
  u_char	hash[16];
  u_char	*resp;

  if (lnk->lcp.auth.msoft.has_keys)
  { 
    memcpy(mppc->xmit_key0, lnk->lcp.auth.msoft.xmit_key, MPPE_KEY_LEN);
    memcpy(mppc->recv_key0, lnk->lcp.auth.msoft.recv_key, MPPE_KEY_LEN);
    return;
  }

  /* The secret comes from the originating caller's credentials */
  switch (lnk->originate) {
    case LINK_ORIGINATE_LOCAL:
      resp = bund->ccp.mppc.self_ntResp;
      break;
    case LINK_ORIGINATE_REMOTE:
      resp = bund->ccp.mppc.peer_ntResp;
      break;
    case LINK_ORIGINATE_UNKNOWN:
    default:
      Log(LG_ERR, ("[%s] can't determine link direction for MPPE", lnk->name));
      goto fail;
  }

  if (!lnk->lcp.auth.msoft.has_nt_hash) {
    Log(LG_ERR, ("[%s] The NT-Hash is not set, but needed for MS-CHAPv2 and MPPE", 
      lnk->name));
    goto fail;
  }

  /* Compute basis for the session key (ie, "start key" or key0) */
  memcpy(hash, lnk->lcp.auth.msoft.nt_hash_hash, sizeof(hash));
  KEYDEBUG((hash, sizeof(hash), "NT Password Hash Hash"));
  KEYDEBUG((resp, CHAP_MSOFTv2_CHAL_LEN, "Response"));
  MsoftGetMasterKey(resp, hash);
  KEYDEBUG((hash, sizeof(hash), "GetMasterKey"));
  MsoftGetAsymetricStartKey(hash,
    (dir == COMP_DIR_RECV) ^
      (bund->links[0]->originate == LINK_ORIGINATE_LOCAL));
  KEYDEBUG((hash, sizeof(hash), "GetAsymmetricKey"));
  memcpy(key0, hash, MPPE_KEY_LEN);
  KEYDEBUG((key0, MPPE_KEY_LEN, "InitialKey"));
  return;

fail:
  FsmFailure(&ccp->fsm, FAIL_CANT_ENCRYPT);
  FsmFailure(&bund->ipcp.fsm, FAIL_CANT_ENCRYPT);
}

#ifdef DEBUG_KEYS

/*
 * KeyDebug()
 */

static void
KeyDebug(const u_char *data, int len, const char *fmt, ...)
{
  char		buf[100];
  int		k;
  va_list	args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ":");
  for (k = 0; k < len; k++) {
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
      " %02x", (u_char) data[k]);
  }
  Log(LG_ERR, ("%s", buf));
}

#endif	/* DEBUG_KEYS */
