NATIVE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_DIR := $(abspath $(NATIVE_DIR)/../..)
BUILD_DIR ?= $(REPO_DIR)/build
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
MONITOR = $(BUILD_DIR)/omarchy-monitor-control
MONITOR_SOURCES = $(NATIVE_DIR)omarchy-monitor-control.c $(NATIVE_DIR)monitor-model.c
MONITOR_FLAGS = $(shell pkg-config --cflags json-c)
MONITOR_LIBS = $(shell pkg-config --libs json-c) -lm

.PHONY: all install clean test
all: $(MONITOR)

$(MONITOR): $(MONITOR_SOURCES) $(NATIVE_DIR)monitor-model.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(MONITOR_FLAGS) $(MONITOR_SOURCES) $(LDFLAGS) $(MONITOR_LIBS) -o $@

.PHONY: install-monitor test-monitor
install-monitor: $(MONITOR)
	install -Dm755 "$(MONITOR)" "$(HOME)/.local/bin/omarchy-monitor-control.new"
	mv -f "$(HOME)/.local/bin/omarchy-monitor-control.new" "$(HOME)/.local/bin/omarchy-monitor-control"
	ln -sfn omarchy-monitor-control "$(HOME)/.local/bin/omarchy-monitor-apply"
	ln -sfn omarchy-monitor-control "$(HOME)/.local/bin/omarchy-monitor-layout"
	install -Dm644 $(REPO_DIR)/default/systemd/user/omarchy-monitor-control.service "$(HOME)/.config/systemd/user/omarchy-monitor-control.service"

$(BUILD_DIR)/monitor-model-test: $(REPO_DIR)/tests/monitor-model-test.c $(NATIVE_DIR)monitor-model.c $(NATIVE_DIR)monitor-model.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(MONITOR_FLAGS) -I$(NATIVE_DIR) $(REPO_DIR)/tests/monitor-model-test.c $(NATIVE_DIR)monitor-model.c $(LDFLAGS) $(MONITOR_LIBS) -o $@

$(BUILD_DIR)/monitor-control-test: $(MONITOR_SOURCES) $(NATIVE_DIR)monitor-model.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(MONITOR_FLAGS) -DMONITOR_CONFIRM_SECONDS=2 $(MONITOR_SOURCES) $(LDFLAGS) $(MONITOR_LIBS) -o $@

test-monitor: $(BUILD_DIR)/monitor-model-test $(BUILD_DIR)/monitor-control-test
	$(BUILD_DIR)/monitor-model-test
	python3 $(REPO_DIR)/tests/run-monitor-control.py $(BUILD_DIR)/monitor-control-test

.PHONY: test-panel-hints install-panel-hints
test-panel-hints:
	python3 $(REPO_DIR)/tests/run-panel-hints.py

install-panel-hints:
	install -Dm755 $(REPO_DIR)/bin/omarchy-panel-hint-click "$(HOME)/.local/bin/omarchy-panel-hint-click.new"
	mv -f "$(HOME)/.local/bin/omarchy-panel-hint-click.new" "$(HOME)/.local/bin/omarchy-panel-hint-click"

install: install-monitor install-panel-hints

test: test-monitor test-panel-hints

clean:
	rm -rf "$(BUILD_DIR)"
