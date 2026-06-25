#include <LiquidCrystal.h>

int pin_led       = 9;
int Toggle_screen = 8;
int RS = 10;
int EN = 2;
int D4 = 3;
int D5 = 4;
int D6 = 5;
int D7 = 6;

LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

byte off_msg[7] = {          // lsb to left  — power-off frame
  0b00101000  ,
  0b11000110  ,
  0b00000000  ,
  0b00001000  ,
  0b00001000  ,
  0b01000000  ,
  0b10111111
  // bytes 7-15 zero-initialised
};

byte message[16] = {          // lsb to left  — power-on frame template
  0b00101000,
  0b11000110,
  0b00000000,
  0b00001000,
  0b00001000,
  0b01111111,
  0b10010000,
  0b00001100,
  0b00000000,  // byte  8: temp  (injected by send)
  0b00000000,  // byte  9: mode  (injected by send)
  0b00000000,  // byte 10: fan   (injected by send)
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000100,
  0b00000000   // byte 15: checksum (filled by checksum())
};

bool ac_state = false;   // last transmitted power state

// ── Helpers ────────────────────────────────────────────────────────

// Bit-reverse a full byte (protocol stores bytes LSB-first / reversed)
byte rev_byte(byte b) {
  b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
  b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
  b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
  return b;
}

// Bit-reverse the lower 4 bits only (temperature field encoding)
byte rev4(byte b) {
  return ((b & 0x01) << 3) | ((b & 0x02) << 1) |
         ((b & 0x04) >> 1) | ((b & 0x08) >> 3);
}

// ── Functions ──────────────────────────────────────────────────────

void checksum(byte frame[16]) {
  // add checksum to byte 15
  // algorithm: sum of bit-reversed bytes 0-14, mod 256,
  // subtracted from 0x9E, then bit-reversed  (from bit_calculator.c)
  byte sum = 0;
  for (int i = 0; i < 15; i++)
    sum = (sum + rev_byte(frame[i])) & 0xFF;
  frame[15] = rev_byte((0x9E - sum) & 0xFF);
}

void mark(int time) {
  unsigned long start = micros();
  while (micros() - start < time) {
    digitalWrite(pin_led, HIGH);
    delayMicroseconds(5);
    digitalWrite(pin_led, LOW);
    delayMicroseconds(13);
  }
}

void space(int time) {
  digitalWrite(pin_led, LOW);
  delayMicroseconds(time);
}

void send(String s_input, byte frame[16]) {
  // decode serial string  →  "temp-mode-fan-power"  e.g. "22-C-H-True"
  int d1 = s_input.indexOf('-');
  int d2 = s_input.indexOf('-', d1 + 1);
  int d3 = s_input.indexOf('-', d2 + 1);
  if (d1 < 0 || d2 < 0 || d3 < 0) return;   // malformed, skip

  int  temp   = s_input.substring(0, d1).toInt();
  char mode   = s_input.charAt(d1 + 1);
  char fan    = s_input.charAt(d2 + 1);
  bool pow_on = s_input.charAt(d3 + 1) == 'T';

  // if requested state is off, send off message
  if (!pow_on) {
    byte cur;
    mark(3300); space(1600);
    for (int i = 0; i < 7; i++) {
      cur = off_msg[i];
      for (int j = 0; j < 8; j++) {
        if (cur & 0x80) { mark(420); space(1200); }
        else            { mark(420); space(420);  }
        cur <<= 1;
      }
    }
    mark(420); space(0);
    ac_state = false;
    lcdPrint(temp, "OFF", "---");
    return;
  }

  // inject input to message
  // byte 8: temperature  →  rev4(temp - 16), lsb set only on first power-on
  frame[8] = rev4((byte)(temp - 16));
  if (!ac_state) frame[8] |= 0x80;

  // byte 9: mode
  switch (mode) {
    case 'C': frame[9] = 0x80; break;  // COOL
    case 'A': frame[9] = 0x00; break;  // AUTO
    case 'D': frame[9] = 0x40; break;  // DRY
    case 'F': frame[9] = 0xC0; break;  // FAN
  }

  // byte 10: fan
  switch (fan) {
    case 'H': frame[10] = 0x80; break;  // HIGH
    case 'M': frame[10] = 0x40; break;  // MED
    case 'L': frame[10] = 0xC0; break;  // LOW
    case 'Q': frame[10] = 0x20; break;  // QUITE
  }

  // add checksum
  checksum(frame);

  // transmit
  byte cur;
  mark(3300); space(1600);
  for (int i = 0; i < 16; i++) {
    cur = frame[i];
    for (int j = 0; j < 8; j++) {
      if (cur & 0x80) { mark(420); space(1200); }
      else            { mark(420); space(420);  }
      cur <<= 1;
    }
  }
  mark(420); space(0);

  ac_state = true;   // mark AC as on; cleared only when off_msg is sent

  // build display strings
  String fanStr, modeStr;
  switch (fan) {
    case 'H': fanStr = "HIGH";  break;
    case 'M': fanStr = "MED";   break;
    case 'L': fanStr = "LOW";   break;
    case 'Q': fanStr = "QUITE"; break;
    default:  fanStr = "?";
  }
  switch (mode) {
    case 'C': modeStr = "COOL"; break;
    case 'A': modeStr = "AUTO"; break;
    case 'D': modeStr = "DRY";  break;
    case 'F': modeStr = "FAN";  break;
    default:  modeStr = "?";
  }
  lcdPrint(temp, fanStr, modeStr);
}

void lcdPrint(int temp, String statusFan, String statusMode) {
  lcd.setCursor(0, 0);
  lcd.clear();
  lcd.print("TEMP: ");
  lcd.print(temp);
  lcd.setCursor(0, 1);
  lcd.print(statusFan);
  lcd.print("  ");
  lcd.print(statusMode);
}

void tog() {
  digitalWrite(Toggle_screen, HIGH);
  delay(3000);
  digitalWrite(Toggle_screen, LOW);
}

// ── Arduino entry points ───────────────────────────────────────────

void setup() {
  pinMode(pin_led, OUTPUT);
  pinMode(Toggle_screen, OUTPUT);
  lcd.begin(16, 2);
  Serial.begin(9600);
  lcd.print("AC READY");
}

void loop() {
  while (Serial.available() == 0) { }

  String msg = Serial.readStringUntil('\n');
  msg.trim();
  delay(100);

  send(msg, message);

  tog();
  delay(5000);
  lcd.clear();
  while (Serial.available() > 0) Serial.read();
}
