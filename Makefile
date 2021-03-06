# $Id$

VERSION!=	cat src/Makefile | grep ^VERSION | awk '{ print $$2 }'

DISTNAME=	mpd-${VERSION}
TARBALL=	${DISTNAME}.tar.gz
PORTBALL=	port.tgz
SVN?=		svn
SVNROOT?=	https://svn.code.sf.net/p/mpd/svn/tags

all:		${TARBALL} ${PORTBALL}

${TARBALL}:	.vcsexport-done
	cd mpd && ${MAKE} .tarball
	cp mpd/${TARBALL} ./${TARBALL}

.tarball:	.dist-done
	rm -f ${TARBALL}
	tar cvf - ${DISTNAME} | gzip --best > ${TARBALL}

${PORTBALL}:	.vcsexport-done
	cd mpd && ${MAKE} .portball
	cp mpd/${PORTBALL} ./${PORTBALL}

.portball:	.dist-done
	cd port && ${MAKE} port

.vcsexport-done:
	@if [ -z ${TAG} ]; then						\
		echo ERROR: Please specify TAG in environment;		\
		false;							\
	fi
	${SVN} export ${SVNROOT}/${TAG} mpd
	touch ${.TARGET}

.dist-done:	.doc-done
	rm -rf ${DISTNAME} ${.TARGET}
	mkdir ${DISTNAME} ${DISTNAME}/src ${DISTNAME}/doc ${DISTNAME}/conf
	cp dist/Makefile ${DISTNAME}
	cp dist/Makefile.conf ${DISTNAME}/conf/Makefile
	cp dist/Makefile.doc ${DISTNAME}/doc/Makefile
	cp -r src/COPYRIGHT* src/Makefile src/[a-z]* ${DISTNAME}/src
	sed 's/@VERSION@/${VERSION}/g' < src/Makefile > ${DISTNAME}/src/Makefile
	cp doc/mpd*.html doc/mpd.ps ${DISTNAME}/doc
	cp doc/mpd.8 ${DISTNAME}/doc/mpd5.8.in
	cp conf/[a-z]* ${DISTNAME}/conf
	sed 's/@VERSION@/${VERSION}/g' < dist/README > ${DISTNAME}/README
	touch ${.TARGET}

.doc-done:
	rm -f ${.TARGET}
	cd doc && ${MAKE}
	touch ${.TARGET}

regen:		clean ${TARBALL}

send:	${TARBALL}
		tar cvf - ${.ALLSRC} | blow gatekeeper

clean cleandir:
	rm -rf mpd
	rm -f .vcsexport-done
	cd doc && ${MAKE} clean
	rm -f .doc-done
	rm -rf ${DISTNAME} ${TARBALL} ${PORTBALL}
	rm -f .dist-done
	cd src && ${MAKE} cleandir
	cd port && ${MAKE} cleandir

distclean:	clean
	rm -f ${TARBALL}

vers:
	@echo The version is: ${VERSION}

