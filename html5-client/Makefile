include ../make.config

.PHONY: all

# BE CAREFUL! Check the 'rm -rf' if you edit this!
OUT = output

all: clean createdirs version coreconfig concat copydeps copyhtml copycss

createdirs: clean
	mkdir ${OUT}
	mkdir ${OUT}/js
	mkdir ${OUT}/css
	mkdir ${OUT}/fonts

CONCAT = ${OUT}/js/jittertrap-concat.js
concat: createdirs
	cat src/js/jittertrap-welcome.js                   > ${CONCAT}
	cat src/js/jittertrap-coreconfig.js                >> ${CONCAT}
	cat src/js/jittertrap-core.js                      >> ${CONCAT}
	cat src/js/jittertrap-measure.js                   >> ${CONCAT}
	cat src/js/jittertrap-websocket.js                 >> ${CONCAT}
	cat src/js/jittertrap-charting.js                  >> ${CONCAT}
	cat src/js/jittertrap-chart-pgaps.js               >> ${CONCAT}
	cat src/js/jittertrap-chart-toptalk.js             >> ${CONCAT}
	cat src/js/jittertrap-traps.js                     >> ${CONCAT}
	cat src/js/jittertrap-programs.js                  >> ${CONCAT}
	cat src/js/jittertrap-version.js                   >> ${CONCAT}
	cat src/js/jittertrap.js                           >> ${CONCAT}

copydeps: createdirs
	cp deps/js/jquery-2.2.1.min.js ${OUT}/js/
	cp deps/js/cbuffer.js ${OUT}/js/
	cp deps/js/bootstrap.min.js ${OUT}/js/
	cp deps/js/mustache.min.js ${OUT}/js/
	cp deps/js/d3.min.js ${OUT}/js/
	cp deps/css/bootstrap.min.css ${OUT}/css/
	cp deps/css/bootstrap-theme.min.css ${OUT}/css/
	cp deps/css/bootstrap.css.map ${OUT}/css/
	cp deps/css/bootstrap-theme.css.map ${OUT}/css/
	cp deps/fonts/glyphicons-halflings-regular.* ${OUT}/fonts/

copyhtml: createdirs assemble-html
	cp -r src/html/templates ${OUT}/

copycss: createdirs
	cp src/css/jittertrap.css ${OUT}/css/

clean:
	rm -rf ${OUT} || true

GIT_REPO = $(shell ../scripts/git_remote_url.sh)
GIT_BRANCH = $(shell git symbolic-ref --short -q HEAD || true)
GIT_COMMIT = $(shell git rev-parse --short=12 HEAD || true)
GIT_COMMIT_TS = $(shell git show -s --format=%ct HEAD || true)
GIT_ISCLEAN = $(shell ../scripts/git_clean_test.sh)
MAINTAINER_VERSION = $(shell cat ../VERSION)

version:
	cp src/js/jittertrap-version-template.js src/js/jittertrap-version.js
	sed -i 's/##GIT-REPO##/${GIT_REPO}/g' src/js/jittertrap-version.js
	sed -i 's/##GIT-BRANCH##/${GIT_BRANCH}/g' src/js/jittertrap-version.js
	sed -i 's/##GIT-COMMIT##/${GIT_COMMIT}/g' src/js/jittertrap-version.js
	sed -i 's/##GIT-COMMIT-TS##/${GIT_COMMIT_TS}/g' src/js/jittertrap-version.js
	sed -i 's/##GIT-CLEAN##/${GIT_ISCLEAN}/g' src/js/jittertrap-version.js
	sed -i 's/##MAINTAINER-VERSION##/${MAINTAINER_VERSION}/g' src/js/jittertrap-version.js

coreconfig: version
	cp src/js/jittertrap-coreconfig-template.js src/js/jittertrap-coreconfig.js
	sed -i 's/##CORECONFIG-SAMPLEPERIOD##/${SAMPLE_PERIOD_US}/g' src/js/jittertrap-coreconfig.js


TPL = ${OUT}/index.html
assemble-html: createdirs
	cp src/html/index.tpl.html ${TPL}
	perl -pe 's|##MODAL-ERROR##|`cat src/html/modal-error.part.html`|ge' -i ${TPL}
	perl -pe 's|##MODAL-ABOUT##|`cat src/html/modal-about.part.html`|ge' -i ${TPL}
	perl -pe 's|##MODAL-HELP##|`cat src/html/modal-help.part.html`|ge' -i ${TPL}
	perl -pe 's|##PANEL-CHARTPARAMS##|`cat src/html/panel-chartparams.part.html`|ge' -i ${TPL}
	perl -pe 's|##PANEL-IMPAIRMENTS##|`cat src/html/panel-impairments.part.html`|ge' -i ${TPL}
	perl -pe 's|##PANEL-TRAPS##|`cat src/html/panel-traps.part.html`|ge' -i ${TPL}
	perl -pe 's|##PRODUCT-BRANDING##|${PRODUCT_BRANDING}|ge' -i ${TPL}

install:
	install -d ${DESTDIR}/var/lib/jittertrap/
	install -d ${DESTDIR}/var/lib/jittertrap/fonts
	install -d ${DESTDIR}/var/lib/jittertrap/css
	install -d ${DESTDIR}/var/lib/jittertrap/js
	install -d ${DESTDIR}/var/lib/jittertrap/templates
	install -m 0644 ${OUT}/fonts/* ${DESTDIR}/var/lib/jittertrap/fonts/
	install -m 0644 ${OUT}/css/* ${DESTDIR}/var/lib/jittertrap/css/
	install -m 0644 ${OUT}/js/* ${DESTDIR}/var/lib/jittertrap/js/
	install -m 0644 ${OUT}/templates/* ${DESTDIR}/var/lib/jittertrap/templates/
	install -m 0644 ${OUT}/index.html ${DESTDIR}/var/lib/jittertrap/

test:
	true
