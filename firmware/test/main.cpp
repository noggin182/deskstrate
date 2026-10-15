/*
 * ddc_probe - poke a monitor over HDMI DDC/CI from a 5V ATmega32u4 (Leonardo / Pro Micro).
 *
 * Wiring (5V board, so no level shifter needed):
 *   SDA = D2  -> HDMI pin 16 (SDA pad)
 *   SCL = D3  -> HDMI pin 15 (SCL pad)
 *   GND       -> HDMI pin 17 / GND pad / socket shell
 *   Do NOT connect the board's 5V to HDMI pin 18.
 * The AVR's internal pull-ups are weak (~30-50k). If reads are flaky and the
 * switcher is unpowered, add 4.7k pull-ups from SDA and SCL to 5V.
 *
 * Serial 115200, single-character commands - press 'h' for the list.
 */
#include <Wire.h>

const uint8_t DDC_ADDR  = 0x37;  // DDC/CI (0x6E/0x6F on the wire)
const uint8_t EDID_ADDR = 0x50;
const uint8_t HOST_ADDR = 0x51;  // "source address" byte in host->display messages

// Dell P2723DE input values (VCP 0x60)
const uint8_t IN_DP = 0x0F, IN_HDMI = 0x11, IN_USBC = 0x1B;

bool verbose = false;
bool slowClock = false;

struct VcpReply {
  bool ok;          // got a well-formed reply
  bool supported;   // monitor says the code exists
  uint8_t type;     // 0 = set parameter, 1 = momentary
  uint16_t max;
  uint16_t cur;
};

// ---------------------------------------------------------------- low level

void dumpHex(const char *tag, const uint8_t *b, uint8_t n) {
  Serial.print(tag);
  for (uint8_t i = 0; i < n; i++) {
    Serial.print(' ');
    if (b[i] < 0x10) Serial.print('0');
    Serial.print(b[i], HEX);
  }
  Serial.println();
}

// Send a DDC/CI message: 0x51, 0x80|len, payload..., checksum
bool ddcSend(const uint8_t *payload, uint8_t len) {
  uint8_t msg[40];
  msg[0] = HOST_ADDR;
  msg[1] = 0x80 | len;
  uint8_t chk = (DDC_ADDR << 1) ^ msg[0] ^ msg[1];
  for (uint8_t i = 0; i < len; i++) {
    msg[2 + i] = payload[i];
    chk ^= payload[i];
  }
  msg[2 + len] = chk;

  if (verbose) dumpHex("  tx:", msg, len + 3);
  Wire.beginTransmission(DDC_ADDR);
  Wire.write(msg, len + 3);
  uint8_t err = Wire.endTransmission();
  if (err) {
    Serial.print(F("  ! I2C write error "));
    Serial.print(err);
    Serial.println(err == 2 ? F(" (no ACK on address - wiring/pull-ups/5V?)") : F(""));
    return false;
  }
  return true;
}

uint8_t ddcRecv(uint8_t *buf, uint8_t n) {
  uint8_t got = Wire.requestFrom(DDC_ADDR, n);
  for (uint8_t i = 0; i < got; i++) buf[i] = Wire.read();
  if (verbose) dumpHex("  rx:", buf, got);
  return got;
}

VcpReply getVcp(uint8_t code, bool quiet = false) {
  VcpReply r = {false, false, 0, 0, 0};
  for (uint8_t attempt = 0; attempt < 3 && !r.ok; attempt++) {
    if (attempt) delay(100);
    uint8_t req[] = {0x01, code};
    if (!ddcSend(req, sizeof req)) continue;
    delay(50);  // DDC/CI: display needs >= 40 ms before the reply is ready

    uint8_t b[11];
    uint8_t n = ddcRecv(b, sizeof b);
    if (n >= 3 && (b[1] & 0x7F) == 0) {
      if (!quiet) Serial.println(F("  (null reply - display busy, retrying)"));
      continue;
    }
    if (n < 11 || b[1] != 0x88 || b[2] != 0x02) {
      if (!quiet) Serial.println(F("  ! malformed reply"));
      continue;
    }
    uint8_t chk = 0x50;  // replies are checksummed against the virtual host address
    for (uint8_t i = 0; i < 10; i++) chk ^= b[i];
    if (chk != b[10]) {
      if (!quiet) Serial.println(F("  ! bad checksum"));
      continue;
    }
    r.ok = true;
    r.supported = (b[3] == 0);
    r.type = b[5];
    r.max = (uint16_t)b[6] << 8 | b[7];
    r.cur = (uint16_t)b[8] << 8 | b[9];
  }
  delay(50);  // keep commands >= 50 ms apart
  return r;
}

bool setVcp(uint8_t code, uint16_t value) {
  uint8_t req[] = {0x03, code, (uint8_t)(value >> 8), (uint8_t)value};
  bool ok = ddcSend(req, sizeof req);
  delay(50);
  return ok;
}

// ---------------------------------------------------------------- helpers

const __FlashStringHelper *inputName(uint8_t v) {
  switch (v) {
    case IN_DP:   return F("DisplayPort (PC)");
    case IN_HDMI: return F("HDMI (guest)");
    case IN_USBC: return F("USB-C (Mac)");
    default:      return F("?");
  }
}

void printVcp(const __FlashStringHelper *label, uint8_t code) {
  Serial.print(label);
  VcpReply r = getVcp(code, true);
  if (!r.ok)             Serial.println(F("no reply"));
  else if (!r.supported) Serial.println(F("unsupported"));
  else {
    Serial.print(r.cur);
    Serial.print(F(" / max "));
    Serial.print(r.max);
    Serial.print(F("  (0x"));
    Serial.print(r.cur, HEX);
    Serial.println(')');
  }
}

void readInput() {
  VcpReply r = getVcp(0x60);
  if (!r.ok) { Serial.println(F("input: no reply")); return; }
  uint8_t v = r.cur & 0xFF;  // Dell repeats the value in the high byte (0x0F0F)
  Serial.print(F("input: "));
  Serial.print(v);
  Serial.print(F(" = "));
  Serial.print(inputName(v));
  Serial.print(F("   raw 0x"));
  Serial.println(r.cur, HEX);
}

void switchInput(uint8_t v) {
  Serial.print(F("switch -> "));
  Serial.println(inputName(v));
  setVcp(0xD6, 1);  // make sure it's awake first
  if (!setVcp(0x60, v)) return;
  delay(2000);      // monitor gives garbage for ~1-2 s after switching
  readInput();
}

void readPower() {
  VcpReply r = getVcp(0xD6);
  if (!r.ok) { Serial.println(F("power: no reply")); return; }
  Serial.print(F("power: "));
  Serial.print(r.cur & 0xFF);
  switch (r.cur & 0xFF) {
    case 1:  Serial.println(F(" = on")); break;
    case 2:  Serial.println(F(" = standby (DPM)")); break;
    case 3:  Serial.println(F(" = suspend")); break;
    case 4:  Serial.println(F(" = standby/off (DPM off)")); break;
    case 5:  Serial.println(F(" = soft power off")); break;
    default: Serial.println();
  }
}

void nudgeBrightness(int delta) {
  VcpReply r = getVcp(0x10);
  if (!r.ok || !r.supported) { Serial.println(F("brightness: no reply")); return; }
  int v = constrain((int)r.cur + delta, 0, (int)r.max);
  setVcp(0x10, v);
  Serial.print(F("brightness "));
  Serial.print(r.cur);
  Serial.print(F(" -> "));
  Serial.println(v);
}

void status() {
  Serial.println(F("--- status"));
  readInput();
  readPower();
  printVcp(F("brightness  (0x10): "), 0x10);
  printVcp(F("contrast    (0x12): "), 0x12);
  printVcp(F("volume      (0x62): "), 0x62);
  printVcp(F("MCCS ver    (0xDF): "), 0xDF);
  printVcp(F("controller  (0xC8): "), 0xC8);
  printVcp(F("firmware    (0xC9): "), 0xC9);
  printVcp(F("OSD lock    (0xCA): "), 0xCA);
}

// Capabilities string, fetched in fragments. The AVR Wire buffer is 32 bytes, so we read
// 32 and advance the offset by however much data actually arrived.
void capabilities() {
  Serial.println(F("--- capabilities"));
  uint16_t off = 0;
  for (uint8_t frag = 0; frag < 60; frag++) {
    uint8_t req[] = {0xF3, (uint8_t)(off >> 8), (uint8_t)off};
    if (!ddcSend(req, sizeof req)) return;
    delay(50);
    uint8_t b[32];
    uint8_t n = ddcRecv(b, sizeof b);
    if (n < 5 || b[2] != 0xE3) {
      Serial.println(F("\n  ! bad fragment, retrying"));
      delay(200);
      continue;
    }
    uint8_t dataLen = (b[1] & 0x7F) - 3;
    if (dataLen == 0) break;  // empty fragment = end of string
    uint8_t avail = min((uint8_t)(n - 5), dataLen);
    for (uint8_t i = 0; i < avail; i++)
      if (b[5 + i]) Serial.write(b[5 + i]);
    off += avail;
    delay(50);
  }
  Serial.println();
}

// Walk every VCP code and print the ones the monitor supports. ~30 s; any key aborts.
void scanVcp() {
  Serial.println(F("--- VCP scan (any key aborts)"));
  for (uint16_t c = 0; c <= 0xFF; c++) {
    if (Serial.available()) { while (Serial.available()) Serial.read(); Serial.println(F("aborted")); return; }
    VcpReply r = getVcp(c, true);
    if (r.ok && r.supported) {
      Serial.print(F("  0x"));
      if (c < 0x10) Serial.print('0');
      Serial.print(c, HEX);
      Serial.print(r.type ? F(" momentary") : F(" set      "));
      Serial.print(F("  cur "));
      Serial.print(r.cur);
      Serial.print(F(" (0x"));
      Serial.print(r.cur, HEX);
      Serial.print(F(")  max "));
      Serial.println(r.max);
    }
  }
  Serial.println(F("done"));
}

// EDID (block 0) - lives at 0x50, often powered from HDMI pin 18.
void edid() {
  Serial.println(F("--- EDID block 0"));
  uint8_t e[128];
  for (uint8_t chunk = 0; chunk < 4; chunk++) {
    Wire.beginTransmission(EDID_ADDR);
    Wire.write(chunk * 32);
    uint8_t err = Wire.endTransmission();
    if (err) {
      Serial.print(F("  ! no ACK from 0x50 (EDID may need +5V on pin 18), err "));
      Serial.println(err);
      return;
    }
    uint8_t got = Wire.requestFrom(EDID_ADDR, (uint8_t)32);
    for (uint8_t i = 0; i < got; i++) e[chunk * 32 + i] = Wire.read();
    if (got != 32) { Serial.println(F("  ! short read")); return; }
  }
  for (uint8_t row = 0; row < 8; row++) dumpHex(" ", e + row * 16, 16);

  uint8_t sum = 0;
  for (uint8_t i = 0; i < 128; i++) sum += e[i];
  Serial.print(F("checksum ")); Serial.println(sum == 0 ? F("ok") : F("BAD"));

  uint16_t mfg = (uint16_t)e[8] << 8 | e[9];
  Serial.print(F("maker    "));
  Serial.write('A' - 1 + ((mfg >> 10) & 0x1F));
  Serial.write('A' - 1 + ((mfg >> 5) & 0x1F));
  Serial.write('A' - 1 + (mfg & 0x1F));
  Serial.print(F("  product 0x"));
  Serial.print((uint16_t)e[11] << 8 | e[10], HEX);
  Serial.print(F("  serial# "));
  Serial.println((uint32_t)e[15] << 24 | (uint32_t)e[14] << 16 | (uint32_t)e[13] << 8 | e[12]);
  Serial.print(F("made     week "));
  Serial.print(e[16]);
  Serial.print(F(" of "));
  Serial.println(1990 + e[17]);

  for (uint8_t d = 54; d <= 108; d += 18) {  // four 18-byte descriptors
    if (e[d] || e[d + 1] || e[d + 2]) continue;  // detailed timing, skip
    uint8_t tag = e[d + 3];
    if (tag != 0xFC && tag != 0xFF && tag != 0xFE) continue;
    Serial.print(tag == 0xFC ? F("name     ") : tag == 0xFF ? F("serial   ") : F("text     "));
    for (uint8_t i = 5; i < 18 && e[d + i] != 0x0A; i++) Serial.write(e[d + i]);
    Serial.println();
  }
  Serial.print(F("extension blocks: "));
  Serial.println(e[126]);
}

void scanBus() {
  Serial.println(F("--- I2C scan"));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  0x"));
      if (a < 0x10) Serial.print('0');
      Serial.print(a, HEX);
      switch (a) {
        case 0x30: Serial.println(F("  EDID segment pointer")); break;
        case 0x37: Serial.println(F("  DDC/CI (monitor control)")); break;
        case 0x3A: Serial.println(F("  HDCP receiver")); break;
        case 0x50: Serial.println(F("  EDID")); break;
        case 0x54: Serial.println(F("  SCDC (HDMI 2.0)")); break;
        default:   Serial.println();
      }
    }
  }
  Serial.println(F("done"));
}

void help() {
  Serial.println(F(
    "\nddc_probe - single-key commands\n"
    "  1 / 2 / 3   input -> DisplayPort / HDMI / USB-C\n"
    "  i           read input\n"
    "  p / o       power on / standby  (VCP D6 = 1 / 4)\n"
    "  P           read power mode\n"
    "  + / -       brightness up / down 10\n"
    "  x           status summary (input, power, brightness, versions...)\n"
    "  c           capabilities string\n"
    "  a           scan all VCP codes (~30 s)\n"
    "  e           read + decode EDID\n"
    "  s           I2C bus scan\n"
    "  v           toggle verbose (raw tx/rx bytes)\n"
    "  k           toggle bus speed 100 kHz / 20 kHz\n"
    "  h / ?       this help"));
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  while (!Serial) {}  // 32u4 native USB: wait for the port to open
  Wire.begin();       // also enables the AVR's internal pull-ups
  Wire.setClock(100000);
  Wire.setWireTimeout(25000, true);  // don't hang forever if the bus gets stuck low
  help();
}

void loop() {
  if (!Serial.available()) return;
  char ch = Serial.read();
  switch (ch) {
    case '1': switchInput(IN_DP); break;
    case '2': switchInput(IN_HDMI); break;
    case '3': switchInput(IN_USBC); break;
    case 'i': readInput(); break;
    case 'p': Serial.println(F("power on"));  setVcp(0xD6, 1); delay(1000); readPower(); break;
    case 'o': Serial.println(F("standby"));   setVcp(0xD6, 4); delay(1000); readPower(); break;
    case 'P': readPower(); break;
    case '+': nudgeBrightness(10); break;
    case '-': nudgeBrightness(-10); break;
    case 'x': status(); break;
    case 'c': capabilities(); break;
    case 'a': scanVcp(); break;
    case 'e': edid(); break;
    case 's': scanBus(); break;
    case 'v': verbose = !verbose; Serial.print(F("verbose ")); Serial.println(verbose ? F("on") : F("off")); break;
    case 'k':
      slowClock = !slowClock;
      Wire.setClock(slowClock ? 20000 : 100000);
      Serial.println(slowClock ? F("bus 20 kHz") : F("bus 100 kHz"));
      break;
    case 'h': case '?': help(); break;
    case '\r': case '\n': case ' ': break;
    default: Serial.print(F("? '")); Serial.print(ch); Serial.println(F("' - h for help"));
  }
}
