# Builds deskstrate-host for macOS (Apple Silicon or Intel: whatever this Mac is).
#   make            build build/deskstrate-host
#   make install    build, copy to ~/Library/Application Support/Deskstrate and (re)start the LaunchAgent
#   make uninstall  stop and remove the LaunchAgent
#   make cert       once: a self-signed signing certificate, so rebuilds keep Accessibility

ROOT := ../..
BIN := build/deskstrate-host
APP_DIR := $(HOME)/Library/Application Support/Deskstrate
AGENT := $(HOME)/Library/LaunchAgents/com.deskstrate.host.plist
LABEL := gui/$(shell id -u)/com.deskstrate.host
SIGN_ID := Deskstrate Host Signing

$(BIN): main.swift Bridging.h ../common/host_policy.h $(ROOT)/components/kvm_proto/include/kvm_proto.h
	mkdir -p build
	swiftc -O -cxx-interoperability-mode=default -import-objc-header Bridging.h \
		-Xcc -I../common -Xcc -I$(ROOT)/components/kvm_proto/include \
		main.swift -o $@
	@# Signed with the same certificate every time (make cert), macOS keeps the Accessibility
	@# permission across rebuilds. Without it (CI, or before make cert), ad-hoc sign.
	@if security find-identity -v -p codesigning 2>/dev/null | grep -q '"$(SIGN_ID)"'; then \
		codesign -f -s "$(SIGN_ID)" -i com.deskstrate.host $@; \
	else \
		echo "note: no \"$(SIGN_ID)\" certificate (make cert), so Accessibility must be re-granted after each rebuild"; \
		codesign -f -s - -i com.deskstrate.host $@; \
	fi

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

cert:
	./signing-cert.sh
	rm -f $(BIN)  # so the next make signs with it

clean:
	rm -rf build

.PHONY: install uninstall cert clean
