# Java-owned utilities. Generated files stay under root out/java/.

ROOT := $(abspath ..)
OUT  := $(ROOT)/out/java

CC     ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror

ifeq ($(origin JAVA_HOME),undefined)
  JAVA_HOME := $(shell \
    for j in $(ROOT)/java/jdk8-macos-arm64/Contents/Home \
             /usr/lib/jvm/java-8-openjdk-amd64 \
             /usr/lib/jvm/temurin-8-jdk-amd64 \
             /Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home; do \
      [ -d "$$j" ] && echo "$$j" && break; \
    done)
endif

JAVAC := $(JAVA_HOME)/bin/javac
JAVA  := $(JAVA_HOME)/bin/java

FA_SRC   := fetch_mc_assets/src
FA_TEST  := fetch_mc_assets/test
FA_CLS   := $(OUT)/fetch_mc_assets/classes
FA_TCLS  := $(OUT)/fetch_mc_assets/test-classes
FA_MAIN  := netherite.fetchassets.FetchMcAssets
FA_TMAIN := netherite.fetchassets.FetchMcAssetsTest

BO_SRC   := bootstrap_oracle/src
BO_TEST  := bootstrap_oracle/test
BO_CLS   := $(OUT)/bootstrap_oracle/classes
BO_TCLS  := $(OUT)/bootstrap_oracle/test-classes
BO_MAIN  := netherite.bootstraporacle.BootstrapOracle
BO_TMAIN := netherite.bootstraporacle.BootstrapOracleTest

JAVAC_FLAGS := -source 1.8 -target 1.8 -encoding UTF-8 -Xlint:none

.PHONY: all test patch-class-paths test-patch-class-paths fetch-assets \
	test-fetch-assets require-java8 bootstrap-oracle-classes bootstrap-oracle \
	test-bootstrap-oracle clean

all: patch-class-paths fetch-assets bootstrap-oracle-classes

test: test-patch-class-paths test-fetch-assets test-bootstrap-oracle

$(OUT):
	mkdir -p $@

$(OUT)/patch_class_paths: patch_class_paths.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $<

$(OUT)/patch_class_paths_test: patch_class_paths.c | $(OUT)
	$(CC) $(CFLAGS) -DPATCH_CLASS_PATHS_SELFTEST -o $@ $<

patch-class-paths: $(OUT)/patch_class_paths

test-patch-class-paths: $(OUT)/patch_class_paths_test
	$(OUT)/patch_class_paths_test

require-java8:
	@test -n "$(JAVA_HOME)" || { echo "ERROR: JAVA_HOME not set and no JDK 8 found"; exit 2; }
	@test -x "$(JAVAC)" || { echo "ERROR: javac not found at $(JAVAC)"; exit 2; }
	@"$(JAVA)" -version 2>&1 | grep -q '1\.8\.' \
		|| { echo "ERROR: $(JAVA_HOME) is not a JDK 8"; exit 2; }

fetch-assets: require-java8
	@mkdir -p "$(FA_CLS)" "$(OUT)/fetch_mc_assets"
	find "$(FA_SRC)" -name '*.java' | sort > "$(OUT)/fetch_mc_assets/sources.list"
	"$(JAVAC)" $(JAVAC_FLAGS) -d "$(FA_CLS)" @"$(OUT)/fetch_mc_assets/sources.list"
	@echo "fetch-assets classes -> $(FA_CLS)"

test-fetch-assets: fetch-assets
	@mkdir -p "$(FA_TCLS)" "$(OUT)/fetch_mc_assets"
	find "$(FA_TEST)" -name '*.java' | sort > "$(OUT)/fetch_mc_assets/test-sources.list"
	"$(JAVAC)" $(JAVAC_FLAGS) -cp "$(FA_CLS)" -d "$(FA_TCLS)" \
		@"$(OUT)/fetch_mc_assets/test-sources.list"
	"$(JAVA)" -cp "$(FA_CLS):$(FA_TCLS)" $(FA_TMAIN)

bootstrap-oracle-classes: fetch-assets
	@mkdir -p "$(BO_CLS)" "$(OUT)/bootstrap_oracle"
	find "$(BO_SRC)" -name '*.java' | sort > "$(OUT)/bootstrap_oracle/sources.list"
	"$(JAVAC)" $(JAVAC_FLAGS) -cp "$(FA_CLS)" -d "$(BO_CLS)" \
		@"$(OUT)/bootstrap_oracle/sources.list"
	@echo "bootstrap-oracle classes -> $(BO_CLS)"

# Full oracle decompile + sync. Network-heavy when decomp sources are absent.
bootstrap-oracle: bootstrap-oracle-classes
	"$(JAVA)" -cp "$(BO_CLS):$(FA_CLS)" $(BO_MAIN) "$(ROOT)"

test-bootstrap-oracle: bootstrap-oracle-classes
	@mkdir -p "$(BO_TCLS)" "$(OUT)/bootstrap_oracle"
	find "$(BO_TEST)" -name '*.java' | sort > "$(OUT)/bootstrap_oracle/test-sources.list"
	"$(JAVAC)" $(JAVAC_FLAGS) -cp "$(BO_CLS):$(FA_CLS)" -d "$(BO_TCLS)" \
		@"$(OUT)/bootstrap_oracle/test-sources.list"
	"$(JAVA)" -cp "$(BO_CLS):$(FA_CLS):$(BO_TCLS)" $(BO_TMAIN)

clean:
	rm -rf "$(OUT)"
