
include config.make

SUBDIRS = backend frontend docs
CLEANDIRS = $(SUBDIRS:%=clean-%)

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)
	@echo "Done."

$(SUBDIRS): %:
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
	cov-build --dir cov-int make backend
	@tar caf jittertrap-coverity-build.lzma cov-int
	@echo Coverity build archive: jittertrap-coverity-build.lzma

coverity-clean:
	rm -rf cov-int jittertrap-coverity-build.lzma

cppcheck:
	cppcheck --enable=style,warning,performance,portability backend/
	#cppcheck deps/fossa/fossa.c

clang-analyze:
	scan-build make backend

clean: $(CLEANDIRS)
$(CLEANDIRS):
	@echo "Cleaning $@"
	@$(MAKE) --silent -C $(@:clean-%=%) clean
