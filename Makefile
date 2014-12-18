SUBDIRS = src docs test

.PHONY: all $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS): %:
	@$(MAKE) -C $@

update-frozen:
	git subtree pull --prefix deps/frozen https://github.com/cesanta/frozen master --squash

update-fossa:
	git subtree pull --prefix deps/fossa https://github.com/cesanta/fossa master --squash


