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
CPP_ZONE1224_OUTPUT := $(BUILD_DIR)/cpp-zone1224.out
CPP_ZONE1224_INTERPRETED := $(BUILD_DIR)/cpp-zone1224-interpreted.out
CPP_ZONE1224_NORMALIZED := $(BUILD_DIR)/cpp-zone1224.normalized
CPP_ZONE1224_INTERPRETED_NORMALIZED := $(BUILD_DIR)/cpp-zone1224-interpreted.normalized
DISPAK_RUNNER ?= ./tools/run-dispak.sh
ZONE_INPUTS := $(wildcard zone*.pop2)
ZONE_COVERAGES := $(patsubst %.pop2,$(BUILD_DIR)/%.cov,$(ZONE_INPUTS))
COVERAGE_CORPUS := $(BUILD_DIR)/coverage-corpus.md

.PHONY: all image trace listing calls dictionary coverage-corpus cpp cpp-test cpp-run cpp-trivial cpp-quine cpp-zone1224 test clean

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

cpp-test: cpp image
	ctest --test-dir $(CPP_BUILD_DIR) --output-on-failure
	echo '2+2=>' | $(CPP_BUILD_DIR)/poplan > $(BUILD_DIR)/cpp-trivial.out
	./tools/normalize-output.py $(BUILD_DIR)/cpp-trivial.out \
		> $(BUILD_DIR)/cpp-trivial.normalized
	diff -u tests/expected/trivial.out $(BUILD_DIR)/cpp-trivial.normalized

cpp-run: cpp image
	$(CPP_BUILD_DIR)/poplan

cpp-trivial: cpp image
	echo '2+2=>' | $(CPP_BUILD_DIR)/poplan > $(BUILD_DIR)/cpp-trivial.out
	./tools/normalize-output.py $(BUILD_DIR)/cpp-trivial.out \
		> $(BUILD_DIR)/cpp-trivial.normalized
	diff -u tests/expected/trivial.out $(BUILD_DIR)/cpp-trivial.normalized

cpp-quine: cpp image
	$(CPP_BUILD_DIR)/poplan --image $(IMAGE) < quine.pop2 \
		> $(BUILD_DIR)/cpp-quine.out
	./tools/normalize-output.py $(BUILD_DIR)/cpp-quine.out \
		> $(BUILD_DIR)/cpp-quine.normalized
	diff -u tests/expected/quine.out $(BUILD_DIR)/cpp-quine.normalized

cpp-zone1224: cpp image
	$(CPP_BUILD_DIR)/poplan --image $(IMAGE) < zone1224.pop2 \
		> $(CPP_ZONE1224_OUTPUT)
	POPLAN_INTERPRET_ONLY=1 $(CPP_BUILD_DIR)/poplan --image $(IMAGE) \
		< zone1224.pop2 > $(CPP_ZONE1224_INTERPRETED)
	./tools/normalize-output.py $(CPP_ZONE1224_OUTPUT) \
		> $(CPP_ZONE1224_NORMALIZED)
	./tools/normalize-output.py $(CPP_ZONE1224_INTERPRETED) \
		> $(CPP_ZONE1224_INTERPRETED_NORMALIZED)
	diff -u $(CPP_ZONE1224_INTERPRETED_NORMALIZED) \
		$(CPP_ZONE1224_NORMALIZED)

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
