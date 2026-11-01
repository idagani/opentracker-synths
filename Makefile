# Convenience targets: build and test these modules with the OpenTracker core.
CORE ?= ../opentracker/reimpl
PROFILE ?= full

image:
	$(MAKE) -C $(CORE) PROFILE=$(PROFILE) MODULES_DIR=$(CURDIR)
test:
	$(MAKE) -C $(CORE) PROFILE=$(PROFILE) MODULES_DIR=$(CURDIR) host-test
perf:
	$(MAKE) -C $(CORE) PROFILE=$(PROFILE) MODULES_DIR=$(CURDIR) perf-gate

.PHONY: image test perf
