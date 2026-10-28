# Builds deskstrate-host for macOS (Apple Silicon or Intel: whatever this Mac is).
#   make            build build/deskstrate-host
#   make install    build, copy to ~/Library/Application Support/Deskstrate and (re)start the LaunchAgent
#   make uninstall  stop and remove the LaunchAgent

ROOT := ../..
BIN := build/deskstrate-host
APP_DIR := $(HOME)/Library/Application Support/Deskstrate
AGENT := $(HOME)/Library/LaunchAgents/com.deskstrate.host.plist
LABEL := gui/$(shell id -u)/com.deskstrate.host

$(BIN): main.swift Bridging.h ../common/host_policy.h $(ROOT)/components/kvm_proto/include/kvm_proto.h
	mkdir -p build
	swiftc -O -import-objc-header Bridging.h \
		-Xcc -I../common -Xcc -I$(ROOT)/components/kvm_proto/include \
		main.swift -o $@

install: $(BIN)
	-launchctl bootout $(LABEL) 2>/dev/null && sleep 1
	mkdir -p "$(APP_DIR)" "$(HOME)/Library/Logs/Deskstrate" "$(HOME)/Library/LaunchAgents"
	cp $(BIN) "$(APP_DIR)/deskstrate-host"
	sed "s|@HOME@|$(HOME)|g" com.deskstrate.host.plist > "$(AGENT)"
	launchctl bootstrap gui/$(shell id -u) "$(AGENT)"
	@echo "Running. Log: ~/Library/Logs/Deskstrate/host.log"

uninstall:
	-launchctl bootout $(LABEL)
	rm -f "$(AGENT)"

clean:
	rm -rf build

.PHONY: install uninstall clean
