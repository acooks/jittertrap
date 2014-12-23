SUBDIRS = src docs

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS): %:
	echo "Making $@"
	@$(MAKE) -C $@

update-fossa:
	git subtree pull --prefix deps/fossa https://github.com/cesanta/fossa master --squash

