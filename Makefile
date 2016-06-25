
include make.config

SUBDIRS = deps/toptalk messages server cli-client html5-client docs
CLEANDIRS = $(SUBDIRS:%=clean-%)
TESTDIRS = $(SUBDIRS:%=test-%)

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)
	@echo "Done."

$(SUBDIRS): %: messages make.config
	@echo "Making $@"
	@$(MAKE) --silent -C $@

update-cbuffer:
	git subtree split --prefix deps/cbuffer --annotate='split ' --rejoin
	git subtree pull --prefix deps/cbuffer https://github.com/acooks/cbuffer.git master --squash

update-toptalk:
	git subtree split --prefix deps/toptalk --annotate='split ' --rejoin
	git subtree pull --prefix deps/toptalk https://github.com/acooks/toptalk.git master --squash


# Remember to add the coverity bin directory to your PATH
coverity-build: $(CLEANDIRS)
	cov-build --dir cov-int make messages server cli-client
	@tar caf jittertrap-coverity-build.lzma cov-int
	@echo Coverity build archive: jittertrap-coverity-build.lzma

coverity-clean:
	rm -rf cov-int jittertrap-coverity-build.lzma

cppcheck:
	cppcheck --enable=style,warning,performance,portability messages/ server/ cli-client/

clang-analyze:
	scan-build make messages server cli-client

clean: $(CLEANDIRS)
$(CLEANDIRS):
	@echo "Cleaning $@"
	@$(MAKE) --silent -C $(@:clean-%=%) clean

install: all
	install -d ${DESTDIR}/usr/bin/
	install -m 0755 server/jt-server ${DESTDIR}/usr/bin/
	$(MAKE) -C html5-client install

test: $(TESTDIRS)

$(TESTDIRS):
	@echo "Test $@"
	@$(MAKE) --silent -C $(@:test-%=%) test
