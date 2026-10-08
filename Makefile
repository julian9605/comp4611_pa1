CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
HAZARDS ?= src/hazards.c
TARGET ?= sim

.PHONY: all score test test-unit test-pipeline clean
all: $(TARGET)
$(TARGET): framework/main.c framework/parser.c framework/bridge.c framework/internal.h src/hazards.h $(HAZARDS)
	$(CC) $(CFLAGS) -Isrc -Iframework framework/main.c framework/parser.c framework/bridge.c $(HAZARDS) -lm -o $@
test-unit:
	python3 tools/check_unit.py --source $(HAZARDS)
score:
	python3 tools/score_stalls.py --source $(HAZARDS)
test-pipeline: $(TARGET)
	python3 tools/check.py ./$(TARGET)
test: test-unit test-pipeline
clean:
	rm -f sim sim-ref sim-sanitize
