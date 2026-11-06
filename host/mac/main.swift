// deskstrate-host for macOS: the Mac's daemon, run as a LaunchAgent in the user's session
// (it changes the display layout and wakes the displays, which need the GUI session).
//
// Listens for the core's STATE broadcasts on KVM_PORT and follows host/common/host_policy.h:
// extend or mirror the two Dells, and wake the displays when a monitor comes back to the
// Mac or the keyboard moves to it. A KV VOLUME message for the Mac changes its volume
// (the keyboard's Fn+knob while the keyboard is on the PC), and a KD DISPLAYS (the keyboard's
// Sleep key, then its next key press) sleeps or wakes the displays. It only ever sends the core
// an empty REQUEST, to get a STATE.
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
import AudioToolbox  // kAudioHardwareServiceDeviceProperty_VirtualMainVolume
import CoreAudio
import CoreGraphics
import Foundation
import IOKit
import IOKit.pwr_mgt

// ---- DDC/CI (Apple Silicon) ------------------------------------------------------
// The Sleep key puts the monitors showing the Mac into DDC standby (VCP 0xD6 = 4) before
// sleeping the displays, so they go dark at once instead of showing "No signal", and
// switches them back on (0xD6 = 1) on wake: a monitor in DDC standby doesn't wake on its
// own when the signal comes back. Apple Silicon has no public DDC API; this uses the
// IOAVService calls that MonitorControl, m1ddc and BetterDisplay use.

@_silgen_name("IOAVServiceCreateWithService")
func IOAVServiceCreateWithService(_ allocator: CFAllocator?, _ service: io_service_t) -> Unmanaged<CFTypeRef>?
@_silgen_name("IOAVServiceReadI2C")
func IOAVServiceReadI2C(_ service: CFTypeRef, _ chip: UInt32, _ offset: UInt32, _ buf: UnsafeMutableRawPointer, _ size: UInt32) -> IOReturn
@_silgen_name("IOAVServiceWriteI2C")
func IOAVServiceWriteI2C(_ service: CFTypeRef, _ chip: UInt32, _ offset: UInt32, _ buf: UnsafeMutableRawPointer, _ size: UInt32) -> IOReturn

let vcpInput: UInt8 = 0x60
let vcpPower: UInt8 = 0xD6  // 1 on, 4 standby
let macInputs: Set<UInt16> = [0x1B]  // the Dells' USB-C input
let ddcQueue = DispatchQueue(label: "ddc")

/// The external displays' AV services (one per display on a DCP).
func avServices() -> [CFTypeRef] {
    var out: [CFTypeRef] = []
    var iter: io_iterator_t = 0
    guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("DCPAVServiceProxy"), &iter) == KERN_SUCCESS else { return out }
    defer { IOObjectRelease(iter) }
    var s = IOIteratorNext(iter)
    while s != 0 {
        let loc = IORegistryEntryCreateCFProperty(s, "Location" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() as? String
        if loc == "External", let av = IOAVServiceCreateWithService(kCFAllocatorDefault, s) {
            out.append(av.takeRetainedValue())
        }
        IOObjectRelease(s)
        s = IOIteratorNext(iter)
    }
    return out
}

func ddcWrite(_ av: CFTypeRef, _ body: [UInt8]) -> Bool {
    var packet = body
    packet.append(body.reduce(UInt8(0x6E ^ 0x51), ^))  // checksum: dest 0x6E, source 0x51
    var ok = false
    for _ in 0..<2 {  // as MonitorControl does: a single write is sometimes dropped
        usleep(10_000)
        if IOAVServiceWriteI2C(av, 0x37, 0x51, &packet, UInt32(packet.count)) == kIOReturnSuccess { ok = true }
    }
    return ok
}

/// The current value of a VCP feature, or nil if the monitor didn't answer.
func ddcGet(_ av: CFTypeRef, _ vcp: UInt8) -> UInt16? {
    for _ in 0..<3 {
        guard ddcWrite(av, [0x82, 0x01, vcp]) else { continue }
        usleep(40_000)
        var reply = [UInt8](repeating: 0, count: 12)
        guard IOAVServiceReadI2C(av, 0x37, 0x51, &reply, UInt32(reply.count)) == kIOReturnSuccess else { continue }
        // [0x6E, 0x88, 0x02 (VCP reply), result, vcp, type, max hi, max lo, cur hi, cur lo, checksum]
        for i in 0...(reply.count - 8) where reply[i] == 0x02 && reply[i + 1] == 0x00 && reply[i + 2] == vcp {
            return UInt16(reply[i + 6]) << 8 | UInt16(reply[i + 7])
        }
    }
    return nil
}

func ddcSet(_ av: CFTypeRef, _ vcp: UInt8, _ value: UInt16) -> Bool {
    ddcWrite(av, [0x84, 0x03, vcp, UInt8(value >> 8), UInt8(value & 0xFF)])
}

/// DDC power on every monitor whose input is the Mac's. Monitors showing another host
/// are left alone (a Dell ignores standby from an input it isn't showing anyway).
func ddcPower(on: Bool) {
    let want: UInt16 = on ? 1 : 4
    for (i, av) in avServices().enumerated() {
        guard let input = ddcGet(av, vcpInput) else { log("ddc: display \(i): no reply"); continue }
        guard macInputs.contains(input & 0xFF) else { continue }
        if let p = ddcGet(av, vcpPower), p & 0xFF == want { continue }
        let ok = ddcSet(av, vcpPower, want)
        log("ddc: display \(i) \(on ? "on" : "standby")\(ok ? "" : " failed")")
    }
}


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
        case "--ddc-list":  // each external display's input and power, over DDC
            for (i, av) in avServices().enumerated() {
                let hex = { (v: UInt16?) in v.map { String(format: "%02X", $0) } ?? "no reply" }
                print("display \(i): input \(hex(ddcGet(av, vcpInput))), power \(hex(ddcGet(av, vcpPower)))")
            }
            exit(0)
        case "--ddc":  // --ddc standby | on: the monitors showing the Mac
            let v = args.next()
            guard v == "standby" || v == "on" else { FileHandle.standardError.write("--ddc standby|on\n".data(using: .utf8)!); exit(2) }
            ddcPower(on: v == "on")
            exit(0)
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

func wakeDisplays() {
    log("displays: wake")
    var id: IOPMAssertionID = 0
    IOPMAssertionDeclareUserActivity("Deskstrate" as CFString, kIOPMUserActiveLocal, &id)
    // A monitor in DDC standby stays dark until told otherwise.
    ddcQueue.async { ddcPower(on: true) }
}

func displaysAsleep() -> Bool {
    let ds = onlineDisplays()
    return !ds.isEmpty && ds.allSatisfy { CGDisplayIsAsleep($0) != 0 }
}

/// The Dells by kvm_slot, found by EDID serial (never by list order).
func dells() -> [UInt8: CGDirectDisplayID] {
    var found: [UInt8: CGDirectDisplayID] = [:]
    for d in onlineDisplays() {
        for (mon, serial) in serials where CGDisplaySerialNumber(d) == serial { found[mon] = d }
    }
    return found
}

var savedOrigins: [CGDirectDisplayID: CGPoint] = [:]  // the extended layout, put back on unmirror
var appliedTopology = UInt8(HOST_TOPO_NONE.rawValue)

func applyTopology(_ topo: UInt8, onto visibleMon: UInt8, hiding hiddenMon: UInt8, retry: Bool = true) {
    let found = dells()
    guard let visible = found[visibleMon], let hidden = found[hiddenMon] else {
        log("topology: both Dells not found (undocked?), leaving the layout alone")
        return
    }
    let mirror = topo == UInt8(HOST_TOPO_MIRROR.rawValue)
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
host_policy_init(&policy, UInt8(KVM_HOST_MAC.rawValue))
var state = [UInt8](repeating: UInt8(KVM_UNKNOWN), count: Int(KVM_SLOTS.rawValue))
var core: in_addr_t = 0
var lastState: Date?
var displaysOn = true

func sendToCore(_ message: kvm_msg_t) {
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

/// An empty REQUEST (all 0xFF) changes nothing: the core just answers with its STATE.
func askForState() {
    let none = [UInt8](repeating: UInt8(KVM_UNKNOWN), count: Int(KVM_SLOTS.rawValue))
    sendToCore(kvm_msg(UInt8(KVM_REQUEST.rawValue), none))
}

func monName(_ b: UInt8) -> String {
    if b == UInt8(KVM_UNKNOWN) { return "--" }
    if kvm_mon_standby(b) != 0 { return "standby" }
    switch UInt32(kvm_mon_host(b)) {
    case KVM_HOST_PC.rawValue: return "PC"
    case KVM_HOST_MAC.rawValue: return "Mac"
    case KVM_HOST_GUEST.rawValue: return "HDMI"
    default: return "?"
    }
}

func onState(_ b: [UInt8], from: in_addr_t) {
    let first = lastState == nil
    core = from
    lastState = Date()
    if first || b != state {
        let hex = b.map { String(format: "%02X", $0) }.joined(separator: " ")
        log("STATE \(hex) from \(String(cString: inet_ntoa(in_addr(s_addr: from)))): " +
            "left \(monName(b[Int(KVM_MON_LEFT.rawValue)])), right \(monName(b[Int(KVM_MON_RIGHT.rawValue)]))")
    }
    state = b
    let act = host_policy_on_state(&policy, b)
    if act.set_topology != 0 && topologyEnabled {
        applyTopology(act.topology, onto: act.mirror_mon, hiding: act.hidden_mon)
    }
    if act.wake_displays != 0 { wakeDisplays() }
}

// ---- volume (the keyboard's Fn+knob while the keyboard is on the PC) -----------------

// With Accessibility permission (System Settings > Privacy & Security > Accessibility),
// press the media key itself: macOS's own step and volume HUD. Without it, set the
// default output's volume through CoreAudio: no HUD, 1/16 steps.
func pressVolumeKey(_ action: UInt8) -> Bool {
    guard AXIsProcessTrusted() else { return false }
    let key: Int
    switch action {
    case UInt8(KVM_VOL_UP.rawValue): key = 0    // NX_KEYTYPE_SOUND_UP
    case UInt8(KVM_VOL_DOWN.rawValue): key = 1  // NX_KEYTYPE_SOUND_DOWN
    case UInt8(KVM_VOL_MUTE.rawValue): key = 7  // NX_KEYTYPE_MUTE
    default: return true
    }
    for down in [true, false] {
        let state = down ? 0xA : 0xB
        guard let ev = NSEvent.otherEvent(with: .systemDefined, location: .zero,
                                          modifierFlags: NSEvent.ModifierFlags(rawValue: UInt(state << 8)),
                                          timestamp: 0, windowNumber: 0, context: nil, subtype: 8,
                                          data1: (key << 16) | (state << 8), data2: -1) else { return false }
        ev.cgEvent?.post(tap: .cghidEventTap)
    }
    return true
}

func defaultOutput() -> AudioObjectID? {
    var id = AudioObjectID(0)
    var size = UInt32(MemoryLayout<AudioObjectID>.size)
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice,
                                          mScope: kAudioObjectPropertyScopeGlobal,
                                          mElement: kAudioObjectPropertyElementMain)
    let err = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &id)
    return err == noErr && id != 0 ? id : nil
}

func setVolumeDirectly(_ action: UInt8) {
    guard let dev = defaultOutput() else { log("volume: no default output"); return }
    if action == UInt8(KVM_VOL_MUTE.rawValue) {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyMute,
                                              mScope: kAudioDevicePropertyScopeOutput,
                                              mElement: kAudioObjectPropertyElementMain)
        var muted: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &muted) == noErr else {
            log("volume: this output has no mute"); return
        }
        muted = muted == 0 ? 1 : 0
        let err = AudioObjectSetPropertyData(dev, &addr, 0, nil, size, &muted)
        if err != noErr { log("volume: mute failed (\(err))") }
        return
    }
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
                                          mScope: kAudioDevicePropertyScopeOutput,
                                          mElement: kAudioObjectPropertyElementMain)
    var vol: Float32 = 0
    var size = UInt32(MemoryLayout<Float32>.size)
    guard AudioObjectGetPropertyData(dev, &addr, 0, nil, &size, &vol) == noErr else {
        log("volume: this output has no software volume"); return
    }
    let step: Float32 = action == UInt8(KVM_VOL_UP.rawValue) ? 1.0 / 16 : -1.0 / 16
    vol = min(1, max(0, vol + step))
    let err = AudioObjectSetPropertyData(dev, &addr, 0, nil, size, &vol)
    if err != noErr { log("volume: set failed (\(err))") }
}

// KV VOLUME: [host, enum kvm_volume, FF, FF], broadcast by the keyboard's ESP.
func onVolume(_ b: [UInt8]) {
    guard b[0] == UInt8(KVM_HOST_MAC.rawValue) else { return }
    log("volume \(b[1])")
    if !pressVolumeKey(b[1]) { setVolumeDirectly(b[1]) }
}

// KD DISPLAYS [enum kvm_displays, seq, FF, FF]: the keyboard's Sleep key sleeps the displays
// (not the Mac), and the key press after it wakes them. The ESP sends each command three
// times with the same seq in case one is lost: act on a seq once.
var lastDisplaysSeq: UInt8?
func onDisplays(_ b: [UInt8]) {
    let wake = b[0] == UInt8(KVM_DISPLAYS_WAKE.rawValue)
    guard wake || b[0] == UInt8(KVM_DISPLAYS_SLEEP.rawValue), b[1] != lastDisplaysSeq else { return }
    lastDisplaysSeq = b[1]
    if wake { wakeDisplays(); return }
    log("displays: sleep (keyboard)")
    // DDC standby only: the signal stays on, so the monitors go dark at once with no
    // "No signal", and macOS doesn't see a display sleep (which would lock the Mac). The
    // keyboard wakes them (KD wake -> wakeDisplays()).
    ddcQueue.async { ddcPower(on: false) }
}

func onDatagram(_ buf: UnsafeRawBufferPointer, from: in_addr_t) {
    var msg = kvm_msg_t()
    guard let base = buf.baseAddress else { return }
    if kvm_decode(base, Int32(buf.count), UInt8(KVM_STATE.rawValue), &msg) != 0 {
        onState([msg.b.0, msg.b.1, msg.b.2, msg.b.3], from: from)
    } else if kvm_decode(base, Int32(buf.count), UInt8(KVM_VOLUME.rawValue), &msg) != 0 {
        onVolume([msg.b.0, msg.b.1, msg.b.2, msg.b.3])
    } else if kvm_decode(base, Int32(buf.count), UInt8(KVM_DISPLAYS.rawValue), &msg) != 0 {
        onDisplays([msg.b.0, msg.b.1, msg.b.2, msg.b.3])
    }
}

func onDisplaysChanged(on: Bool) {
    displaysOn = on
    log("displays: \(on ? "on" : "off")")
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
    if let last = lastState, Date().timeIntervalSince(last) * 1000 > Double(KVM_TIMEOUT_MS) {
        log("core silent for \(KVM_TIMEOUT_MS) ms: looking for it again")
        lastState = nil
    }
    if lastState == nil { askForState() }
    let on = !displaysAsleep()
    if on != displaysOn { onDisplaysChanged(on: on) }
}
timer.resume()

let ws = NSWorkspace.shared.notificationCenter
ws.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { _ in
    log("system: going to sleep")
}
ws.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
    log("system: woke")
    lastState = nil
    askForState()
}

RunLoop.main.run()
