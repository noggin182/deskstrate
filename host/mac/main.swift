// deskstrate-host for macOS: the Mac's daemon, run as a LaunchAgent in the user's session
// (it changes the display layout and wakes the displays, which need the GUI session).
//
// The Mac is on a work VPN that won't let it send to the LAN, so it never opens a socket:
// it talks kvm_proto through the serial bridge on its dock (firmware/bridge), one message
// per line, and the bridge passes the lines to and from UDP KVM_PORT. The bridge's port
// being there is how this daemon knows the Mac is docked. Undocked it does nothing at all:
// no port, no timers, no audio listeners. The bridge is found by its USB serial number
// (--bridge-serial, set by make install); any other ESP plugged in is ignored.
//
// Docked, it follows the core's STATE through host/common/host_policy.h: extend or mirror
// the two Dells, wake the displays when a monitor comes back to the Mac or the keyboard
// moves to it, and DDC standby / on for the monitors showing the Mac while the desk is
// asleep / awake (standby in STATE). A KV VOLUME message for the Mac changes its volume
// (the keyboard's Fn+knob while the keyboard is on the PC). It reports the Mac's own
// volume and mute to the core ("KR ---- --- 42%", host/common/level_report.h) for the
// deck's VOLUME card, and otherwise only sends an empty REQUEST, to get a STATE.
// Lines from the bridge that start with "# " are its own logs, copied into this log.
//
// Mirroring as in the VERIFIED test (PROJECT_KNOWLEDGE.md §3): CGConfigureDisplayMirrorOfDisplay
// mirrors the hidden Dell onto the visible one; it fails (CGError 1014) while the displays
// sleep, so they are woken first.
//
// Options: --bridge-serial <sn>   the bridge's USB serial number (its MAC, "D4:05:92:4B:67:D0")
//          --list-bridges         print each Espressif USB serial port's serial number and path, then exit
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
import IOKit.serial

// ---- DDC/CI (Apple Silicon) ------------------------------------------------------
// While the desk is asleep (standby in STATE) the monitors showing the Mac go to DDC
// standby (VCP 0xD6 = 4), and back on (0xD6 = 1) when it wakes. DDC only: the signal stays
// on, so there's no "No signal", and macOS doesn't see a display sleep (which would lock
// the Mac). Apple Silicon has no public DDC API; this uses the IOAVService calls that
// MonitorControl, m1ddc and BetterDisplay use.

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


// ---- USB serial ports ----------------------------------------------------------

/// An Espressif USB Serial/JTAG port (USB ID 303A:1001), which is what the bridge is.
struct EspPort {
    let path: String    // /dev/cu.usbmodem...
    let serial: String  // USB serial number: the board's MAC
}

/// A property of the USB device above a serial port's node in the IORegistry.
func usbProperty(_ service: io_object_t, _ key: String) -> CFTypeRef? {
    IORegistryEntrySearchCFProperty(service, kIOServicePlane, key as CFString, kCFAllocatorDefault,
                                    IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents))
}

/// The port behind an IOSerialBSDClient, if it's an Espressif USB Serial/JTAG.
func espPort(_ service: io_object_t) -> EspPort? {
    guard let path = IORegistryEntryCreateCFProperty(service, kIOCalloutDeviceKey as CFString,
                                                     kCFAllocatorDefault, 0)?.takeRetainedValue() as? String,
          (usbProperty(service, "idVendor") as? Int) == 0x303A,
          (usbProperty(service, "idProduct") as? Int) == 0x1001,
          let serial = usbProperty(service, "USB Serial Number") as? String else { return nil }
    return EspPort(path: path, serial: serial)
}

func espPorts() -> [EspPort] {
    var out: [EspPort] = []
    var iter: io_iterator_t = 0
    guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOSerialBSDServiceValue), &iter) == KERN_SUCCESS else { return out }
    defer { IOObjectRelease(iter) }
    var s = IOIteratorNext(iter)
    while s != 0 {
        if let p = espPort(s) { out.append(p) }
        IOObjectRelease(s)
        s = IOIteratorNext(iter)
    }
    return out
}

// ---- options -----------------------------------------------------------------

var topologyEnabled = true
var bridgeSerial: String?
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
        case "--bridge-serial": bridgeSerial = args.next()
        case "--list-bridges":
            for p in espPorts() { print("\(p.serial) \(p.path)") }
            exit(0)
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

var policy = host_policy_t()
host_policy_init(&policy, UInt8(KVM_HOST_MAC.rawValue))
var state = [UInt8](repeating: UInt8(KVM_UNKNOWN), count: Int(KVM_SLOTS.rawValue))
var lastState: Date?
var displaysOn = true

/// Sends one message through the bridge (the bridge broadcasts it on the LAN). Dropped
/// while undocked, or if the bridge isn't keeping up.
func sendToCore(_ message: kvm_msg_t) {
    guard bridgeFd >= 0 else { return }
    var m = message
    var raw = [UInt8](repeating: 0, count: 16)  // >= KVM_MAX_LEN
    let len = Int(kvm_encode(&m, &raw))
    raw[len] = UInt8(ascii: "\n")
    _ = raw.withUnsafeBytes { write(bridgeFd, $0.baseAddress, len + 1) }
}

/// An empty REQUEST ("KR ---- --- ---") changes nothing: the core just answers with its STATE.
func askForState() {
    let none = [UInt8](repeating: UInt8(KVM_UNKNOWN), count: Int(KVM_SLOTS.rawValue))
    sendToCore(kvm_msg(UInt8(KVM_REQUEST.rawValue), none))
}

func monName(_ b: UInt8) -> String {
    if kvm_mon_connected(b) == 0 { return "--" }
    if kvm_mon_standby(b) != 0 { return "asleep" }
    switch UInt32(kvm_mon_host(b)) {
    case KVM_HOST_PC.rawValue: return "PC"
    case KVM_HOST_MAC.rawValue: return "Mac"
    case KVM_HOST_GUEST.rawValue: return "HDMI"
    default: return "?"
    }
}

func onState(_ b: [UInt8]) {
    let first = lastState == nil
    lastState = Date()
    if first || b != state {
        let text = String(decoding: b, as: UTF8.self)  // kvm_decode let only ASCII through
        log("STATE \(text): " +
            "left \(monName(b[Int(KVM_MON_LEFT.rawValue)])), right \(monName(b[Int(KVM_MON_RIGHT.rawValue)]))")
    }
    state = b
    let act = host_policy_on_state(&policy, b)
    if act.set_topology != 0 && topologyEnabled {
        applyTopology(act.topology, onto: act.mirror_mon, hiding: act.hidden_mon)
    }
    if act.wake_displays != 0 { wakeDisplays() }
    if act.power != 0 {
        let on = act.power == UInt8(HOST_POWER_ON.rawValue)
        ddcQueue.async { ddcPower(on: on) }
    }
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

// ---- the Mac's own volume, reported to the core (level_report.h) -------------------

// CoreAudio listeners on the default output (and on which device that is), only while
// docked. They run on the main queue, like everything else here.
var level = level_report_t()
level_report_init(&level)
var audioDevice = AudioObjectID(0)
var defaultOutputListener: AudioObjectPropertyListenerBlock?
var deviceListener: AudioObjectPropertyListenerBlock?
var defaultOutputAddr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice,
                                                   mScope: kAudioObjectPropertyScopeGlobal,
                                                   mElement: kAudioObjectPropertyElementMain)
var volumeAddr = AudioObjectPropertyAddress(mSelector: kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
                                            mScope: kAudioDevicePropertyScopeOutput,
                                            mElement: kAudioObjectPropertyElementMain)
var muteAddr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyMute,
                                          mScope: kAudioDevicePropertyScopeOutput,
                                          mElement: kAudioObjectPropertyElementMain)

func nowMs() -> UInt64 { DispatchTime.now().uptimeNanoseconds / 1_000_000 + 1 }  // never 0

/// Sends a report if level_report.h says one is due. state: the STATE just received, if any.
func checkLevel(_ state: UnsafePointer<kvm_msg_t>?) {
    guard bridgeFd >= 0 else { return }
    let now = nowMs()
    guard mac_level_due(&level, now, state) != 0 else { return }
    sendToCore(level_report_msg(&level, Int32(KVM_LEVEL_MAC.rawValue)))
    level_report_sent(&level, now)
}

/// Reads the default output's volume and mute. An output without a software volume (a
/// display's own speakers, some interfaces) reads as unknown, so nothing is reported.
func readAudio() {
    var lv: Int32 = -1
    var muted: Int32 = 0
    if audioDevice != 0 {
        var vol: Float32 = 0
        var size = UInt32(MemoryLayout<Float32>.size)
        if AudioObjectGetPropertyData(audioDevice, &volumeAddr, 0, nil, &size, &vol) == noErr {
            lv = kvm_level_from_scalar(vol)
        }
        var m: UInt32 = 0
        size = UInt32(MemoryLayout<UInt32>.size)
        if AudioObjectGetPropertyData(audioDevice, &muteAddr, 0, nil, &size, &m) == noErr { muted = m != 0 ? 1 : 0 }
    }
    let before = level.mine
    level_report_set(&level, lv, muted)
    if level.mine != before {
        let m = level.mine
        log("volume \(String(decoding: [m.0, m.1, m.2], as: UTF8.self))")
    }
    checkLevel(nil)
}

func unbindAudioDevice() {
    if let l = deviceListener, audioDevice != 0 {
        AudioObjectRemovePropertyListenerBlock(audioDevice, &volumeAddr, .main, l)
        AudioObjectRemovePropertyListenerBlock(audioDevice, &muteAddr, .main, l)
    }
    deviceListener = nil
    audioDevice = 0
}

/// Follows the default output: listens to the new one's volume and mute.
func bindAudioDevice() {
    unbindAudioDevice()
    if let dev = defaultOutput() {
        let l: AudioObjectPropertyListenerBlock = { _, _ in readAudio() }
        // Either may be missing on this output; a listener for it then just never fires.
        AudioObjectAddPropertyListenerBlock(dev, &volumeAddr, .main, l)
        AudioObjectAddPropertyListenerBlock(dev, &muteAddr, .main, l)
        audioDevice = dev
        deviceListener = l
    }
    readAudio()
}

func startAudio() {
    let l: AudioObjectPropertyListenerBlock = { _, _ in bindAudioDevice() }
    AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &defaultOutputAddr, .main, l)
    defaultOutputListener = l
    bindAudioDevice()
}

func stopAudio() {
    unbindAudioDevice()
    if let l = defaultOutputListener {
        AudioObjectRemovePropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &defaultOutputAddr, .main, l)
    }
    defaultOutputListener = nil
    level_report_init(&level)  // report straight away on the next dock
}

// KV VOLUME ("KV M+"), broadcast by the keyboard's ESP.
func onVolume(host: UInt8, action: UInt8) {
    guard host == UInt8(KVM_HOST_MAC.rawValue) else { return }
    log("volume key \(Character(UnicodeScalar(action)))")
    if !pressVolumeKey(action) { setVolumeDirectly(action) }
}

/// One line from the bridge: one of its logs ("# ..."), or a datagram it heard.
func onLine(_ line: [UInt8]) {
    if line.count >= 2 && line[0] == UInt8(ascii: "#") && line[1] == UInt8(ascii: " ") {
        log("bridge: \(String(decoding: line[2...], as: UTF8.self))")
        return
    }
    var msg = kvm_msg_t()
    if line.withUnsafeBytes({ kvm_decode($0.baseAddress, Int32($0.count), UInt8(KVM_STATE.rawValue), &msg) }) != 0 {
        onState([msg.b.0, msg.b.1, msg.b.2, msg.b.3])
        checkLevel(&msg)
    } else if line.withUnsafeBytes({ kvm_decode($0.baseAddress, Int32($0.count), UInt8(KVM_VOLUME.rawValue), &msg) }) != 0 {
        onVolume(host: msg.host, action: msg.action)
    }
    // Anything else (the bridge's ROM and bootloader at power-up, other kvm_proto
    // messages on the LAN) is ignored.
}

func onDisplaysChanged(on: Bool) {
    displaysOn = on
    log("displays: \(on ? "on" : "off")")
}

// ---- the bridge: docked / undocked ------------------------------------------------

var bridgeFd: Int32 = -1
var bridgeReader: DispatchSourceRead?
var lineBuf: [UInt8] = []
var dockedTimer: DispatchSourceTimer?
var ticks = 0

/// Opens the bridge's port raw. macOS raises DTR and RTS together on open and drops them
/// together on close, which the S3's USB Serial/JTAG ignores (only RTS without DTR resets
/// it), so opening the port doesn't reset the bridge. They are left alone after that.
func openBridge(_ port: EspPort, retry: Bool = true) {
    guard bridgeFd < 0 else { return }
    let fd = open(port.path, O_RDWR | O_NOCTTY | O_NONBLOCK)
    guard fd >= 0 else {
        log("bridge: can't open \(port.path): \(String(cString: strerror(errno)))")
        // The node can appear a moment before it can be opened.
        if retry { DispatchQueue.main.asyncAfter(deadline: .now() + 1) { openBridge(port, retry: false) } }
        return
    }
    var t = termios()
    tcgetattr(fd, &t)
    cfmakeraw(&t)
    t.c_cflag |= tcflag_t(CLOCAL | CREAD)
    cfsetspeed(&t, speed_t(B115200))  // ignored by USB, but set something sane
    tcsetattr(fd, TCSANOW, &t)
    tcflush(fd, TCIOFLUSH)

    bridgeFd = fd
    lineBuf = []
    let reader = DispatchSource.makeReadSource(fileDescriptor: fd, queue: .main)
    reader.setEventHandler {
        var buf = [UInt8](repeating: 0, count: 1024)
        while true {
            let n = buf.withUnsafeMutableBytes { read(fd, $0.baseAddress, $0.count) }
            if n > 0 { feed(buf[0..<n]); continue }
            if n < 0 && (errno == EAGAIN || errno == EINTR) { return }
            closeBridge("port gone (\(n < 0 ? String(cString: strerror(errno)) : "EOF"))")  // unplugged
            return
        }
    }
    reader.setCancelHandler { close(fd) }
    reader.resume()
    bridgeReader = reader
    log("docked: bridge \(port.serial) on \(port.path)")
    onDocked()
}

func closeBridge(_ why: String) {
    guard bridgeFd >= 0 else { return }
    bridgeReader?.cancel()  // closes the port
    bridgeReader = nil
    bridgeFd = -1
    log("undocked: \(why); idle until the bridge is back")
    onUndocked()
}

func feed(_ bytes: ArraySlice<UInt8>) {
    for c in bytes {
        if c == UInt8(ascii: "\n") || c == UInt8(ascii: "\r") {
            if !lineBuf.isEmpty { onLine(lineBuf) }
            lineBuf.removeAll(keepingCapacity: true)
        } else if lineBuf.count < 512 {
            lineBuf.append(c)  // a longer line is cut short, and so never decodes
        }
    }
}

func onDocked() {
    lastState = nil
    askForState()
    startAudio()
    ticks = 0
    let timer = DispatchSource.makeTimerSource(queue: .main)
    timer.schedule(deadline: .now() + .milliseconds(Int(LEVEL_REPORT_TICK_MS)),
                   repeating: .milliseconds(Int(LEVEL_REPORT_TICK_MS)))
    timer.setEventHandler(handler: tick)
    timer.resume()
    dockedTimer = timer
}

func onUndocked() {
    dockedTimer?.cancel()
    dockedTimer = nil
    stopAudio()
    lastState = nil
    lineBuf = []
}

/// Every LEVEL_REPORT_TICK_MS while docked.
func tick() {
    checkLevel(nil)
    ticks += 1
    guard ticks % 10 == 0 else { return }  // the rest once a second
    if let last = lastState, Date().timeIntervalSince(last) * 1000 > Double(KVM_TIMEOUT_MS) {
        log("core silent for \(KVM_TIMEOUT_MS) ms: looking for it again")
        lastState = nil
    }
    if lastState == nil { askForState() }
    let on = !displaysAsleep()
    if on != displaysOn { onDisplaysChanged(on: on) }
}

/// New serial ports: open the one that is our bridge.
func portsAdded(_ iter: io_iterator_t) {
    var s = IOIteratorNext(iter)
    while s != 0 {
        if let p = espPort(s) {
            if p.serial.caseInsensitiveCompare(bridgeSerial ?? "") == .orderedSame {
                openBridge(p)
            } else {
                log("ignoring ESP \(p.serial) on \(p.path): not the bridge (\(bridgeSerial ?? "none set"))")
            }
        }
        IOObjectRelease(s)
        s = IOIteratorNext(iter)
    }
}

// ---- main ----------------------------------------------------------------------

log("deskstrate-host starting (topology \(topologyEnabled ? "on" : "off"), bridge \(bridgeSerial ?? "not set"))")
if bridgeSerial == nil {
    log("no --bridge-serial: run make install with the bridge plugged in. Staying idle.")
}
for (mon, d) in dells().sorted(by: { $0.key < $1.key }) {
    log("monitor \(mon) is display \(d) (serial \(CGDisplaySerialNumber(d)))")
}

// The bridge's port coming and going is the dock: IOKit says when a port appears, and a
// read error says it has gone.
let notifyPort = IONotificationPortCreate(kIOMainPortDefault)
IONotificationPortSetDispatchQueue(notifyPort, .main)
var portsIter: io_iterator_t = 0
if bridgeSerial != nil {
    let kr = IOServiceAddMatchingNotification(notifyPort, kIOFirstMatchNotification,
                                              IOServiceMatching(kIOSerialBSDServiceValue),
                                              { _, iter in portsAdded(iter) }, nil, &portsIter)
    if kr == KERN_SUCCESS {
        portsAdded(portsIter)  // the ports already there; this also arms the notification
    } else {
        log("IOKit notification failed: \(kr)")
    }
}

let ws = NSWorkspace.shared.notificationCenter
ws.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { _ in
    log("system: going to sleep")
}
ws.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
    log("system: woke")
    guard bridgeFd >= 0 else { return }
    lastState = nil
    askForState()
}

RunLoop.main.run()
