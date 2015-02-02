SUBDIRS = src docs
CLEANDIRS = $(SUBDIRS:%=clean-%)

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS): %:
	@echo "Making $@"
	@$(MAKE) -C $@

update-fossa:
	git subtree split --prefix deps/fossa --annotate='split ' --rejoin
	git subtree pull --prefix deps/fossa https://github.com/cesanta/fossa master --squash

# Remember to add the coverity bin directory to your PATH
coverity-build: $(CLEANDIRS)
	cov-build --dir cov-int make src
	@tar caf jittertrap-coverity-build.lzma cov-int
	@echo Coverity build archive: jittertrap-coverity-build.lzma

clean: $(CLEANDIRS)
$(CLEANDIRS):
	@echo "Cleaning $@"
	$(MAKE) -C $(@:clean-%=%) clean
