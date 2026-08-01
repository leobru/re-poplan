SHELL := /bin/sh

BUILD_DIR := build
IMAGE := $(BUILD_DIR)/poplan.bin
TRACE := $(BUILD_DIR)/trace.quine
OUTPUT := $(BUILD_DIR)/quine.out
COVERAGE := $(BUILD_DIR)/quine.cov
LISTING := $(BUILD_DIR)/poplan.lst
CALLS := $(BUILD_DIR)/calls.md
DICTIONARY := $(BUILD_DIR)/dictionary.md
CPP_BUILD_DIR := $(BUILD_DIR)/cpp
DISPAK_RUNNER ?= ./tools/run-dispak.sh
ZONE_INPUTS := $(wildcard zone*.pop2)
ZONE_COVERAGES := $(patsubst %.pop2,$(BUILD_DIR)/%.cov,$(ZONE_INPUTS))
COVERAGE_CORPUS := $(BUILD_DIR)/coverage-corpus.md

.PHONY: all image trace listing calls dictionary coverage-corpus cpp cpp-test test clean

all: listing calls dictionary

image: $(IMAGE)

trace: $(TRACE)

listing: $(LISTING)

calls: $(CALLS)

dictionary: $(DICTIONARY)

coverage-corpus: $(COVERAGE_CORPUS)

cpp:
	cmake -S . -B $(CPP_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(CPP_BUILD_DIR)

cpp-test: cpp
	ctest --test-dir $(CPP_BUILD_DIR) --output-on-failure

test:
	./tests/run.sh

$(IMAGE): tools/extract-image.sh
	./tools/extract-image.sh $@

$(TRACE): tools/run-trace.sh poplan.b6 quine.pop2 | $(BUILD_DIR)
	./tools/run-trace.sh quine.pop2 $(OUTPUT) $@ $(COVERAGE)

$(LISTING): $(IMAGE) $(TRACE) poplan.sym tools/disassemble.sh
	./tools/disassemble.sh $(IMAGE) $(TRACE) > $@

$(CALLS): $(TRACE) tools/analyze-trace.py
	./tools/analyze-trace.py $(TRACE) > $@

$(DICTIONARY): $(LISTING) $(IMAGE) tools/extract-dictionary.py
	./tools/extract-dictionary.py --image $(IMAGE) $(LISTING) > $@

$(BUILD_DIR)/zone%.cov: zone%.pop2 poplan.b6 | $(BUILD_DIR)
	timeout 10 $(DISPAK_RUNNER) --bootstrap --coverage=$@ poplan.b6 \
		< $< > $(BUILD_DIR)/zone$*.out

$(COVERAGE_CORPUS): $(COVERAGE) $(ZONE_COVERAGES) tools/summarize-coverage.py
	./tools/summarize-coverage.py $(COVERAGE) $(ZONE_COVERAGES) > $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
