NATIVE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_DIR := $(abspath $(NATIVE_DIR)/../..)
BUILD_DIR ?= $(REPO_DIR)/build
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
ASKPASS = $(BUILD_DIR)/omarchy-ssh-askpass
ASKPASS_SOURCES = $(NATIVE_DIR)omarchy-ssh-askpass.c $(NATIVE_DIR)askpass-context.c $(NATIVE_DIR)askpass-json.c
ASKPASS_HEADERS = $(NATIVE_DIR)askpass-context.h $(NATIVE_DIR)askpass-json.h
ASKPASS_INSTALL_PATH ?= $(HOME)/.local/bin/omarchy-ssh-askpass
MONITOR = $(BUILD_DIR)/omarchy-monitor-control
MONITOR_SOURCES = $(NATIVE_DIR)omarchy-monitor-control.c $(NATIVE_DIR)monitor-model.c
MONITOR_FLAGS = $(shell pkg-config --cflags json-c)
MONITOR_LIBS = $(shell pkg-config --libs json-c) -lm

.PHONY: all install clean test test-bridge test-sanitize test-dialog
all: $(ASKPASS) $(MONITOR)

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

$(ASKPASS): $(ASKPASS_SOURCES) $(ASKPASS_HEADERS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ASKPASS_SOURCES) $(LDFLAGS) -o $@

# Installation is deliberately separate from building or testing. Back up the
# installed helper first; the README gives the corresponding rollback command.
install: $(ASKPASS)
	install -Dm755 "$(ASKPASS)" "$(ASKPASS_INSTALL_PATH)"

clean:
	rm -rf "$(BUILD_DIR)"

TEST_SOURCES = $(REPO_DIR)/tests/askpass-test.c $(NATIVE_DIR)askpass-context.c $(NATIVE_DIR)askpass-json.c

$(BUILD_DIR)/test-bridge: $(ASKPASS_SOURCES) $(ASKPASS_HEADERS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -DASKPASS_TIMEOUT_MS=500 $(ASKPASS_SOURCES) $(LDFLAGS) -o $@

$(BUILD_DIR)/test-fixture: $(TEST_SOURCES) $(ASKPASS_HEADERS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -I$(NATIVE_DIR) $(TEST_SOURCES) $(LDFLAGS) -o $@

$(BUILD_DIR)/test-bridge-fragmented: $(ASKPASS_SOURCES) $(ASKPASS_HEADERS) $(REPO_DIR)/tests/fragment-io.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -DASKPASS_TIMEOUT_MS=500 $(ASKPASS_SOURCES) $(REPO_DIR)/tests/fragment-io.c $(LDFLAGS) -Wl,--wrap=send,--wrap=recv -o $@

test: test-bridge

test-bridge: $(BUILD_DIR)/test-bridge $(BUILD_DIR)/test-bridge-fragmented $(BUILD_DIR)/test-fixture
	bash $(REPO_DIR)/tests/run-bridge.sh $(BUILD_DIR)/test-bridge $(BUILD_DIR)/test-fixture
	bash $(REPO_DIR)/tests/run-bridge.sh $(BUILD_DIR)/test-bridge-fragmented $(BUILD_DIR)/test-fixture

SANITIZER_FLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined

$(BUILD_DIR)/test-bridge-sanitize: $(ASKPASS_SOURCES) $(ASKPASS_HEADERS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(WARNINGS) $(SANITIZER_FLAGS) -DASKPASS_TIMEOUT_MS=500 $(ASKPASS_SOURCES) $(LDFLAGS) -o $@

$(BUILD_DIR)/test-bridge-fragmented-sanitize: $(ASKPASS_SOURCES) $(ASKPASS_HEADERS) $(REPO_DIR)/tests/fragment-io.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(WARNINGS) $(SANITIZER_FLAGS) -DASKPASS_TIMEOUT_MS=500 $(ASKPASS_SOURCES) $(REPO_DIR)/tests/fragment-io.c $(LDFLAGS) -Wl,--wrap=send,--wrap=recv -o $@

$(BUILD_DIR)/test-fixture-sanitize: $(TEST_SOURCES) $(ASKPASS_HEADERS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(WARNINGS) $(SANITIZER_FLAGS) -I$(NATIVE_DIR) $(TEST_SOURCES) $(LDFLAGS) -o $@

test-sanitize: $(BUILD_DIR)/test-bridge-sanitize $(BUILD_DIR)/test-bridge-fragmented-sanitize $(BUILD_DIR)/test-fixture-sanitize
	bash $(REPO_DIR)/tests/run-bridge.sh $(BUILD_DIR)/test-bridge-sanitize $(BUILD_DIR)/test-fixture-sanitize
	bash $(REPO_DIR)/tests/run-bridge.sh $(BUILD_DIR)/test-bridge-fragmented-sanitize $(BUILD_DIR)/test-fixture-sanitize

test-dialog: $(ASKPASS)
	bash $(REPO_DIR)/tests/run-dialog.sh "$(ASKPASS)"
