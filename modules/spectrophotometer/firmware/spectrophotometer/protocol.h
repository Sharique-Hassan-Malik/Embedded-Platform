#pragma once

// ── Host → Firmware commands (single ASCII character) ─────────────────────────
//
// 'R'  Read — take a single averaged ADC reading and reply with a DATA packet.
//             Returns both signal and reference channel values.
//
// 'B'  Blank — store the current signal reading as I0 (blank/reference level).
//              Reply: ACK packet with stored I0 value.
//
// 'D'  Dark  — measure with LED off to capture dark current / offset.
//              Reply: ACK packet with dark ADC value.
//
// 'L'  LED duty — set LED PWM intensity. Followed immediately by a 3-digit
//              decimal string (000–255) then newline, e.g. "L180\n".
//              Reply: ACK packet.
//
// 'S'  Status — return instrument status (cuvette present, LED duty, I0).
//              Reply: STATUS packet.
//
// 'I'  Identify — return firmware version string.
//
// ── Firmware → Host response packets (newline-terminated ASCII) ───────────────
//
// DATA packet:
//   "DATA,<signal>,<reference>,<absorbance>\n"
//   signal     — averaged ADC counts (0–1023, or 0–8191 with oversampling)
//   reference  — averaged ADC counts from reference channel (0 if not fitted)
//   absorbance — computed A = -log10(signal / I0); "nan" if no blank stored
//
// ACK packet:
//   "ACK,<key>,<value>\n"
//   key/value pairs depend on command: "I0,<counts>", "DARK,<counts>", "DUTY,<n>"
//
// STATUS packet:
//   "STATUS,cuvette=<0|1>,duty=<n>,I0=<counts>,dark=<counts>\n"
//
// ERROR packet:
//   "ERR,<reason>\n"

static constexpr char CMD_READ    = 'R';
static constexpr char CMD_BLANK   = 'B';
static constexpr char CMD_DARK    = 'D';
static constexpr char CMD_LED     = 'L';
static constexpr char CMD_STATUS  = 'S';
static constexpr char CMD_IDENT   = 'I';

static constexpr const char *FIRMWARE_VERSION = "spectrophotometer/1.0";
