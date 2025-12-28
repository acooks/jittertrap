
include make.config

DEFINES += "-DMAX_FLOW_COUNT=$(MAX_FLOW_COUNT)"
DEFINES += "-DINTERVAL_COUNT=$(INTERVAL_COUNT)"
export DEFINES

SUBDIRS = deps/toptalk messages server cli-client html5-client docs
CLEANDIRS = $(SUBDIRS:%=clean-%)
TESTDIRS = $(SUBDIRS:%=test-%)

.PHONY: all $(SUBDIRS) help config

all: $(SUBDIRS)
	@echo "Done."

help:
	@echo "JitterTrap Build System"
	@echo "======================="
	@echo ""
	@echo "Targets:"
	@echo "  all              Build everything (default)"
	@echo "  clean            Remove build artifacts"
	@echo "  install          Install to DESTDIR (default: /)"
	@echo "  test             Run all tests"
	@echo "  config           Show current build configuration"
	@echo "  help             Show this help message"
	@echo ""
	@echo "Analysis targets:"
	@echo "  cppcheck         Run cppcheck static analysis"
	@echo "  clang-analyze    Run clang static analyzer"
	@echo "  coverity-build   Create Coverity analysis archive"
	@echo "  coverage         Generate code coverage report"
	@echo ""
	@echo "Configuration:"
	@echo "  Override settings on command line or edit make.config"
	@echo "  Example: make SAMPLE_PERIOD_US=500 WEB_SERVER_PORT=8080"
	@echo ""
	@echo "  Run 'make config' to see current settings"

config:
	@echo "Current Build Configuration"
	@echo "==========================="
	@echo ""
	@echo "Branding:"
	@echo "  PRODUCT_BRANDING        = $(PRODUCT_BRANDING)"
	@echo ""
	@echo "Network Capture:"
	@echo "  SAMPLE_PERIOD_US        = $(SAMPLE_PERIOD_US) (sampling interval in microseconds)"
	@echo "  ALLOWED_IFACES          = $(ALLOWED_IFACES) (empty = all interfaces)"
	@echo "  MAX_IFACE_LEN           = $(MAX_IFACE_LEN)"
	@echo ""
	@echo "Web Server:"
	@echo "  WEB_SERVER_PORT         = $(WEB_SERVER_PORT)"
	@echo "  WEB_SERVER_DOCUMENT_ROOT= $(WEB_SERVER_DOCUMENT_ROOT)"
	@echo ""
	@echo "Performance:"
	@echo "  RT_CPU                  = $(RT_CPU) (CPU core for sampling thread)"
	@echo "  RT_CPU_TOPTALK          = $(RT_CPU_TOPTALK) (CPU core for packet capture threads)"
	@echo "  INTERVAL_COUNT          = $(INTERVAL_COUNT) (time intervals to track)"
	@echo "  MAX_FLOW_COUNT          = $(MAX_FLOW_COUNT) (max concurrent flows)"
	@echo ""
	@echo "Feature Flags:"
	@echo "  DISABLE_IMPAIRMENTS     = $(DISABLE_IMPAIRMENTS) (set to 1 to disable)"
	@echo "  DISABLE_PCAP            = $(DISABLE_PCAP) (set to 1 to disable)"
	@echo "  ENABLE_WEBRTC_PLAYBACK  = $(ENABLE_WEBRTC_PLAYBACK) (set to 1 to enable, requires libdatachannel)"

$(SUBDIRS):
	@echo "Making $@"
	@$(MAKE) --silent -C $@

server: | messages deps/toptalk
cli-client: | messages

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
	scan-build $(MAKE) messages server cli-client

coverage:
	CFLAGS="-fprofile-arcs -ftest-coverage" $(MAKE) clean test
	lcov -c --no-external -d server -d cli-client -d messages -o jittertrap.lcov


.PHONY: clean $(CLEANDIRS)
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

# AddressSanitizer testing
.PHONY: test-asan
test-asan:
	@echo "Running AddressSanitizer tests..."
	@$(MAKE) -C server test-asan
	@echo "All ASan tests passed!"

.PHONY: test-video-asan
test-video-asan:
	@echo "Running video AddressSanitizer tests..."
	@$(MAKE) -C server test-video-asan
	@echo "All video ASan tests passed!"

# Fuzzing targets
.PHONY: fuzz-build
fuzz-build:
	@echo "Building fuzz harnesses..."
	@$(MAKE) -C server/fuzz

.PHONY: fuzz
fuzz: fuzz-build
	@echo "Running fuzzer (press Ctrl+C to stop)..."
	@$(MAKE) -C server/fuzz run-fuzz-rtp

.PHONY: fuzz-quick
fuzz-quick: fuzz-build
	@echo "Running 60-second fuzz session..."
	@$(MAKE) -C server/fuzz fuzz-quick
