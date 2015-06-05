.PHONY: all

# BE CAREFUL! Check the 'rm -rf' if you edit this!
OUT = output

all: clean createdirs concat copydeps copyhtml copycss

createdirs:
	mkdir ${OUT}
	mkdir ${OUT}/js
	mkdir ${OUT}/css
	mkdir ${OUT}/fonts

CONCAT = ${OUT}/js/jittertrap-concat.js
concat:
	cat src/js/jittertrap-globals.js    > ${CONCAT}
	cat src/js/jittertrap-core.js    >> ${CONCAT}
	cat src/js/jittertrap-measure.js    >> ${CONCAT}
	cat src/js/jittertrap-websocket.js  >> ${CONCAT}
	cat src/js/jittertrap-charting.js   >> ${CONCAT}
	cat src/js/jittertrap-traps.js      >> ${CONCAT}
	cat src/js/jittertrap.js            >> ${CONCAT}

copydeps:
	cp deps/js/jquery-2.1.3.min.js ${OUT}/js/
	cp deps/js/cbuffer.js ${OUT}/js/
	cp deps/js/bootstrap.min.js ${OUT}/js/
	cp deps/js/jquery.canvasjs.min.js ${OUT}/js/
	cp deps/js/mustache.min.js ${OUT}/js/
	cp deps/css/bootstrap.min.css ${OUT}/css/
	cp deps/css/bootstrap-theme.min.css ${OUT}/css/
	cp deps/css/bootstrap.css.map ${OUT}/css/
	cp deps/css/bootstrap-theme.css.map ${OUT}/css/
	cp deps/fonts/glyphicons-halflings-regular.* ${OUT}/fonts/

copyhtml:
	cp src/html/index.html ${OUT}/
	cp -r src/html/templates ${OUT}/

copycss:
	cp src/css/jittertrap.css ${OUT}/css/

clean:
	rm -rf ${OUT} || true
