// deskstrate-host for macOS: the Mac's daemon, run as a LaunchAgent in the user's session
// (it changes the display layout and sleeps/wakes the displays, which need the GUI session).
//
// Listens for the core's STATE broadcasts on KVM_PORT and follows host/common/host_policy.h:
// extend or mirror the two Dells; in split mode without the keyboard, keep the displays
// awake, sleep them and wake them with the keyboard's host; sleep them for the deck's
// Screens off; wake them when a deck key asks for the Mac. Replies to each STATE with a
// STATUS, which carries the Mac's display state to the core (and so to the other host).
// To sleep the displays from a key: Ctrl+Shift+Power, or bind `pmset displaysleepnow`.
//
// Mirroring as in the VERIFIED test (PROJECT_KNOWLEDGE.md §3): CGConfigureDisplayMirrorOfDisplay
// mirrors the hidden Dell onto the visible one; it fails (CGError 1014) while the displays
// sleep, so they are woken first.
//
// Options: --core <ipv4>          send to the core directly instead of broadcasting
//          --no-topology          never mirror/unmirror
//          --left-serial <n>      EDID serial of the left Dell (default 810562882)
//          --right-serial <n>     EDID serial of the right Dell (default 810959170)
//          --list-displays        print the online displays and their serials, then exit

import AppKit
import CoreGraphics
import Foundation
import IOKit.pwr_mgt

// ---- options -----------------------------------------------------------------

var topologyEnabled = true
var coreFixed: in_addr_t = 0
var serials: [UInt8: UInt32] = [UInt8(KVM_MON_LEFT.rawValue): 810562882, UInt8(KVM_MON_RIGHT.rawValue): 810959170]

func log(_ message: String) {
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd HH:mm:ss.SSS"
    print("\(f.string(from: Date())) \(message)")
    fflush(stdout)
}

func onlineDisplays() -> [CGDirectDisplayID] {
    var ids = [CGDirectDisplayID](repeating: 0, count: 16)
    var count: UInt32 = 0
    guard CGGetOnlineDisplayList(UInt32(ids.count), &ids, &count) == .success else { return [] }
    return Array(ids.prefix(Int(count)))
}

do {
    var args = CommandLine.arguments.dropFirst().makeIterator()
    while let arg = args.next() {
        switch arg {
        case "--no-topology": topologyEnabled = false
        case "--core": if let v = args.next() { coreFixed = inet_addr(v) }
        case "--left-serial": if let v = args.next(), let n = UInt32(v) { serials[UInt8(KVM_MON_LEFT.rawValue)] = n }
        case "--right-serial": if let v = args.next(), let n = UInt32(v) { serials[UInt8(KVM_MON_RIGHT.rawValue)] = n }
        case "--list-displays":
            for d in onlineDisplays() {
                let b = CGDisplayBounds(d)
                print("display \(d): serial \(CGDisplaySerialNumber(d)), vendor \(CGDisplayVendorNumber(d)), " +
                      "model \(CGDisplayModelNumber(d)), builtin \(CGDisplayIsBuiltin(d) != 0), " +
                      "origin \(Int(b.minX)),\(Int(b.minY)), mirrors \(CGDisplayMirrorsDisplay(d))")
            }
            exit(0)
        default:
            FileHandle.standardError.write("unknown option \(arg)\n".data(using: .utf8)!)
            exit(2)
        }
    }
}

// ---- displays ------------------------------------------------------------------

var keepAwake: IOPMAssertionID = 0  // 0 = not held

/// While following the keyboard's host in split mode: no idle display sleep here.
func setKeepAwake(_ on: Bool) {
    if on == (keepAwake != 0) { return }
    if on {
        let r = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep as CFString,
                                            IOPMAssertionLevel(kIOPMAssertionLevelOn),
                                            "Deskstrate: following the host with the keyboard" as CFString, &keepAwake)
        if r != kIOReturnSuccess { keepAwake = 0 }
        log("displays: keep awake on\(r == kIOReturnSuccess ? "" : " (failed)")")
    } else {
        IOPMAssertionRelease(keepAwake)
        keepAwake = 0
        log("displays: keep awake off")
    }
}

func sleepDisplays() {
    log("displays: sleep")
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/pmset")
    p.arguments = ["displaysleepnow"]
    do { try p.run() } catch { log("pmset failed: \(error)") }
}

func wakeDisplays() {
    log("displays: wake")
    var id: IOPMAssertionID = 0
    IOPMAssertionDeclareUserActivity("Deskstrate" as CFString, kIOPMUserActiveLocal, &id)
}

func displaysAsleep() -> Bool {
    let ds = onlineDisplays()
    return !ds.isEmpty && ds.allSatisfy { CGDisplayIsAsleep($0) != 0 }
}

/// The Dells by kvm_mon_id, found by EDID serial (never by list order).
func dells() -> [UInt8: CGDirectDisplayID] {
    var found: [UInt8: CGDirectDisplayID] = [:]
    for d in onlineDisplays() {
        for (mon, serial) in serials where CGDisplaySerialNumber(d) == serial { found[mon] = d }
    }
    return found
}

var savedOrigins: [CGDirectDisplayID: CGPoint] = [:]  // the extended layout, put back on unmirror
var appliedTopology = UInt8(KVM_TOPO_NONE.rawValue)

func applyTopology(_ topo: UInt8, onto visibleMon: UInt8, hiding hiddenMon: UInt8, retry: Bool = true) {
    let found = dells()
    guard let visible = found[visibleMon], let hidden = found[hiddenMon] else {
        log("topology: both Dells not found (undocked?), leaving the layout alone")
        return
    }
    let mirror = topo == UInt8(KVM_TOPO_MIRROR.rawValue)
    if mirror ? CGDisplayMirrorsDisplay(hidden) == visible
              : CGDisplayMirrorsDisplay(visible) == kCGNullDirectDisplay && CGDisplayMirrorsDisplay(hidden) == kCGNullDirectDisplay {
        appliedTopology = topo
        return
    }
    if displaysAsleep() {
        // CGError 1014 while asleep: wake them, then try once more.
        wakeDisplays()
        if retry {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                applyTopology(topo, onto: visibleMon, hiding: hiddenMon, retry: false)
            }
            return
        }
    }
    let both = [visible, hidden]
    if both.allSatisfy({ CGDisplayMirrorsDisplay($0) == kCGNullDirectDisplay }) {
        savedOrigins = Dictionary(uniqueKeysWithValues: both.map { ($0, CGDisplayBounds($0).origin) })
    }
    var cfg: CGDisplayConfigRef?
    guard CGBeginDisplayConfiguration(&cfg) == .success, let cfg else { return }
    for d in both where CGDisplayMirrorsDisplay(d) != kCGNullDirectDisplay {
        CGConfigureDisplayMirrorOfDisplay(cfg, d, kCGNullDirectDisplay)
    }
    if mirror {
        CGConfigureDisplayMirrorOfDisplay(cfg, hidden, visible)
    } else {
        for (d, origin) in savedOrigins where both.contains(d) {
            CGConfigureDisplayOrigin(cfg, d, Int32(origin.x), Int32(origin.y))
        }
    }
    let err = CGCompleteDisplayConfiguration(cfg, .permanently)
    log("topology: \(mirror ? "mirror onto monitor \(visibleMon)" : "extend") -> \(err.rawValue)")
    if err == .success { appliedTopology = topo }
}

// ---- protocol ------------------------------------------------------------------

let sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
var policy = host_policy_t()
host_policy_init(&policy, UInt8(KVM_IN_USBC.rawValue))
var state = kvm_state_t()
var core: in_addr_t = 0
var coreCounter: UInt64 = 0
var lastState: Date?
var lastCounter: UInt64 = 0
var reqId: UInt32 = 0
var displaysOn = true

func nextCounter() -> UInt64 {
    // KVM_COUNTER for hosts: Unix time in ms, kept strictly increasing.
    var ms = UInt64(Date().timeIntervalSince1970 * 1000)
    if ms <= lastCounter { ms = lastCounter + 1 }
    lastCounter = ms
    return ms
}

func header(_ type: kvm_type, flags: UInt8 = 0) -> kvm_hdr_t {
    var h = kvm_hdr_t()
    h.magic = UInt32(KVM_MAGIC)
    h.version = UInt8(KVM_VERSION)
    h.type = UInt8(type.rawValue)
    h.sender = UInt8(KVM_NODE_MAC.rawValue)
    h.flags = flags
    h.counter = nextCounter()
    return h
}

func sendToCore<T>(_ message: T) {
    var addr = sockaddr_in()
    addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
    addr.sin_family = sa_family_t(AF_INET)
    addr.sin_port = in_port_t(KVM_PORT).bigEndian
    addr.sin_addr.s_addr = lastState != nil ? core : (coreFixed != 0 ? coreFixed : 0xFFFF_FFFF)
    var m = message
    withUnsafeBytes(of: &m) { bytes in
        withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                _ = sendto(sock, bytes.baseAddress, bytes.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
    }
}

func sendRequest(_ action: kvm_action, _ a: UInt8 = 0, _ b: UInt8 = 0) {
    var req = kvm_request_t()
    req.hdr = header(KVM_REQUEST)
    reqId += 1
    req.req_id = reqId
    req.action = UInt8(action.rawValue)
    req.a = a
    req.b = b
    sendToCore(req)
}

func sendStatus(goingDown: Bool = false) {
    var st = kvm_status_t()
    st.hdr = header(KVM_STATUS, flags: goingDown ? UInt8(KVM_F_GOING_DOWN.rawValue) : 0)
    st.ack_seq = state.seq
    st.awake = goingDown ? 0 : 1
    st.displays = UInt8((displaysOn ? KVM_DISP_ON : KVM_DISP_ASLEEP).rawValue)
    st.topology = appliedTopology
    st.mon_count = 0  // the core reads the monitors itself
    sendToCore(st)
}

func inputName(_ v: UInt8) -> String {
    switch UInt32(v) {
    case KVM_IN_KEEP.rawValue: return "--"
    case KVM_IN_DP.rawValue: return "PC"
    case KVM_IN_HDMI.rawValue: return "HDMI"
    case KVM_IN_USBC.rawValue: return "Mac"
    case KVM_IN_OFF.rawValue: return "off"
    default: return "?"
    }
}

func onState(_ st: kvm_state_t, from: in_addr_t) {
    let first = lastState == nil
    core = from
    lastState = Date()
    let changed = first || st.seq != state.seq || st.hdr.flags & UInt8(KVM_F_HEARTBEAT.rawValue) == 0
    state = st
    if changed {
        log("STATE seq \(st.seq) from \(String(cString: inet_ntoa(in_addr(s_addr: from)))): " +
            "left \(inputName(st.mon.0)), right \(inputName(st.mon.1)), keyboard \(st.usb.0), hosts 0x\(String(st.hosts, radix: 16))")
    }
    var s = st
    let act = host_policy_on_state(&policy, &s)
    if act.set_topology != 0 && topologyEnabled {
        applyTopology(act.topology, onto: act.mirror_mon, hiding: act.hidden_mon)
    }
    setKeepAwake(act.keep_awake != 0)  // first: released before sleeping
    if act.sleep_displays != 0 { sleepDisplays() }
    if act.wake_displays != 0 { wakeDisplays() }
    sendStatus()
}

func onDatagram(_ buf: UnsafeRawBufferPointer, from: in_addr_t) {
    guard buf.count >= MemoryLayout<kvm_hdr_t>.size else { return }
    var hdr = kvm_hdr_t()
    withUnsafeMutableBytes(of: &hdr) { $0.copyMemory(from: UnsafeRawBufferPointer(rebasing: buf.prefix($0.count))) }
    guard hdr.magic == UInt32(KVM_MAGIC), hdr.version == UInt8(KVM_VERSION),
          hdr.sender == UInt8(KVM_NODE_CORE.rawValue) else { return }
    guard hdr.counter > coreCounter else { return }  // replayed or reordered
    coreCounter = hdr.counter
    if hdr.type == UInt8(KVM_STATE.rawValue) && buf.count == MemoryLayout<kvm_state_t>.size {
        var st = kvm_state_t()
        withUnsafeMutableBytes(of: &st) { $0.copyMemory(from: buf) }  // decode by copy
        onState(st, from: from)
    } else if hdr.type == UInt8(KVM_RESULT.rawValue) && buf.count == MemoryLayout<kvm_result_t>.size {
        var res = kvm_result_t()
        withUnsafeMutableBytes(of: &res) { $0.copyMemory(from: buf) }
        if res.code != UInt8(KVM_RES_OK.rawValue) { log("RESULT \(res.req_id): code \(res.code)") }
    }
}

func onDisplaysChanged(on: Bool) {
    displaysOn = on
    log("displays: \(on ? "on" : "off")")
    // The core relays it in STATE.hosts: in split mode the other host follows.
    if lastState != nil { sendStatus() }
}

// ---- main ----------------------------------------------------------------------

guard sock >= 0 else { log("socket failed: \(errno)"); exit(1) }
var yes: Int32 = 1
setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, socklen_t(MemoryLayout<Int32>.size))
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))
do {
    var addr = sockaddr_in()
    addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
    addr.sin_family = sa_family_t(AF_INET)
    addr.sin_port = in_port_t(KVM_PORT).bigEndian
    let ok = withUnsafePointer(to: &addr) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(sock, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
    }
    guard ok == 0 else { log("bind UDP \(KVM_PORT) failed: \(errno)"); exit(1) }
}
_ = fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK)

log("deskstrate-host starting (topology \(topologyEnabled ? "on" : "off"))")
for (mon, d) in dells().sorted(by: { $0.key < $1.key }) {
    log("monitor \(mon) is display \(d) (serial \(CGDisplaySerialNumber(d)))")
}

let reader = DispatchSource.makeReadSource(fileDescriptor: sock, queue: .main)
reader.setEventHandler {
    var buf = [UInt8](repeating: 0, count: 256)
    var from = sockaddr_in()
    var fromLen = socklen_t(MemoryLayout<sockaddr_in>.size)
    while true {
        let n = buf.withUnsafeMutableBytes { b in
            withUnsafeMutablePointer(to: &from) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(sock, b.baseAddress, b.count, 0, $0, &fromLen) }
            }
        }
        if n <= 0 { break }  // EAGAIN: drained
        buf.withUnsafeBytes { onDatagram(UnsafeRawBufferPointer(rebasing: $0.prefix(n)), from: from.sin_addr.s_addr) }
    }
}
reader.resume()

let timer = DispatchSource.makeTimerSource(queue: .main)
timer.schedule(deadline: .now(), repeating: 1.0)
timer.setEventHandler {
    if let last = lastState, Date().timeIntervalSince(last) * 1000 > Double(KVM_HOST_TIMEOUT_MS) {
        log("core silent for \(KVM_HOST_TIMEOUT_MS) ms: looking for it again")
        lastState = nil
        coreCounter = 0  // the core may have rebooted with a fresh NVS
    }
    if lastState == nil { sendRequest(KVM_ACT_GET_STATE) }
    let on = !displaysAsleep()
    if on != displaysOn { onDisplaysChanged(on: on) }
}
timer.resume()

let ws = NSWorkspace.shared.notificationCenter
// Display sleep/wake straight away (the 1 s timer is the fallback).
for name in [NSWorkspace.screensDidSleepNotification, NSWorkspace.screensDidWakeNotification] {
    ws.addObserver(forName: name, object: nil, queue: .main) { n in
        let on = n.name == NSWorkspace.screensDidWakeNotification
        if on != displaysOn { onDisplaysChanged(on: on) }
    }
}
ws.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { _ in
    log("system: going to sleep")
    if lastState != nil { sendStatus(goingDown: true) }
}
ws.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
    log("system: woke")
    lastState = nil
    sendRequest(KVM_ACT_GET_STATE)
}

RunLoop.main.run()
