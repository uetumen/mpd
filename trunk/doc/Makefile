# $Id$

.NOTPARALLEL:

all:		mpd.8

DOC=		mpd
FORMATS=	html ps
DATE!=		date +%Y-%m-%d
PREFIX?=	/usr/local
VERSION!=	awk '/^VERSION=/ { print $$2 }' ../src/Makefile

SRCS+=		mpd.sgml version.sgml overview.sgml layers.sgml manorg.sgml
SRCS+=		changes.sgml getting.sgml building.sgml installing.sgml
SRCS+=		invoke.sgml configfmt.sgml cli.sgml nat.sgml
SRCS+=		gencmd.sgml console.sgml web.sgml bundle.sgml
SRCS+=		interface.sgml links.sgml ipcp.sgml  ipv6cp.sgml 
SRCS+=		compression.sgml encryption.sgml modem.sgml nat.sgml
SRCS+=		netgraph.sgml tcp.sgml udp.sgml pptp.sgml l2tp.sgml pppoe.sgml
SRCS+=		chat.sgml trouble.sgml refs.sgml int-auth.sgml
SRCS+=		todo.sgml trafacct.sgml ippool.sgml auth.sgml dynauth.sgml

CLEANFILES+=	version.sgml mpd.8 mpd.ps

.SUFFIXES:	.in

.in:
	sed \
	  -e "s/@DATE@/${DATE}/g" \
	  -e "s,@PREFIX@,${PREFIX},g" \
	  -e "s/@VERSION@/${VERSION}/g" \
	  < ${.ALLSRC} > ${.TARGET}

.include "bsd.sgml.mk"
