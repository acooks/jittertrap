
include make.config

SUBDIRS = messages backend server cli-client html5-client docs
CLEANDIRS = $(SUBDIRS:%=clean-%)

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)
	@echo "Done."

$(SUBDIRS): %: messages make.config
	@echo "Making $@"
	@$(MAKE) --silent -C $@

update-fossa:
	git subtree split --prefix deps/fossa --annotate='split ' --rejoin
	git subtree pull --prefix deps/fossa https://github.com/cesanta/fossa master --squash

update-cbuffer:
	git subtree split --prefix deps/cbuffer --annotate='split ' --rejoin
	git subtree pull --prefix deps/cbuffer https://github.com/acooks/cbuffer.git master --squash

# Remember to add the coverity bin directory to your PATH
coverity-build: $(CLEANDIRS)
	cov-build --dir cov-int make messages backend server cli-client
	@tar caf jittertrap-coverity-build.lzma cov-int
	@echo Coverity build archive: jittertrap-coverity-build.lzma

coverity-clean:
	rm -rf cov-int jittertrap-coverity-build.lzma

cppcheck:
	cppcheck --enable=style,warning,performance,portability messages/ backend/ server/ cli-client/
	#cppcheck deps/fossa/fossa.c

clang-analyze:
	scan-build make messages backend server cli-client

clean: $(CLEANDIRS)
$(CLEANDIRS):
	@echo "Cleaning $@"
	@$(MAKE) --silent -C $(@:clean-%=%) clean

install: all
	install -d ${DESTDIR}/usr/bin/
	install -m 0755 backend/jittertrap ${DESTDIR}/usr/bin/
	$(MAKE) -C html5-client install
