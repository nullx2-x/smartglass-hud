#include "lcd_gc9107.h"
#include <SPI.h>

// ---- ピン (メーカー公式デモ準拠) ----
#define PIN_MOSI 39
#define PIN_SCLK 40
#define PIN_CS   41
#define PIN_DC   42
#define PIN_RST  38
#define PIN_BL   14

#define OFFSET_X 2
#define OFFSET_Y 1

#define SPI_HZ 40000000

static void cmd(uint8_t c) {
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static void dat(uint8_t d) {
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, HIGH);
  SPI.transfer(d);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static void datN(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) dat(d[i]);
}

static uint8_t madctlVal = 0x08;  // BGR, no mirror (メーカー値)

void lcdSetMirror(bool mirror) {
  madctlVal = mirror ? 0x48 : 0x08;  // 0x48 = MX(左右反転) | BGR
  cmd(0x36);
  dat(madctlVal);
}

void lcdSetOrientation(uint8_t madctl) {
  madctlVal = madctl | 0x08;  // BGRは常時付与
  cmd(0x36);
  dat(madctlVal);
}

void lcdSetBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(PIN_BL, (uint32_t)percent * 1023 / 100);
}

// ウィンドウ設定して RAMWR まで発行 (CSはHIGHに戻す。画素送信は lcdDrawBlock 内で完結)
static void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  // MV (軸交換=90°回転系) の時はカラム/ローのオフセットも入れ替わる
  uint16_t ox = (madctlVal & 0x20) ? OFFSET_Y : OFFSET_X;
  uint16_t oy = (madctlVal & 0x20) ? OFFSET_X : OFFSET_Y;
  uint8_t buf[4];
  cmd(0x2A);
  buf[0] = (x0 + ox) >> 8; buf[1] = (x0 + ox) & 0xFF;
  buf[2] = (x1 + ox) >> 8; buf[3] = (x1 + ox) & 0xFF;
  datN(buf, 4);
  cmd(0x2B);
  buf[0] = (y0 + oy) >> 8; buf[1] = (y0 + oy) & 0xFF;
  buf[2] = (y1 + oy) >> 8; buf[3] = (y1 + oy) & 0xFF;
  datN(buf, 4);
  cmd(0x2C);
}

void lcdDrawBlock(int x, int y, int w, int h, const uint8_t *data, size_t len) {
  if (x >= LCD_W || y >= LCD_H || w <= 0 || h <= 0) return;
  setWindow(x, y, x + w - 1, y + h - 1);
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, HIGH);
  SPI.writeBytes(data, len);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

void lcdFill(uint16_t color565) {
  // ビッグエンディアンで1画素分を用意し、行バッファで流す
  uint8_t row[LCD_W * 2];
  for (int i = 0; i < LCD_W; i++) {
    row[i * 2] = color565 >> 8;
    row[i * 2 + 1] = color565 & 0xFF;
  }
  setWindow(0, 0, LCD_W - 1, LCD_H - 1);
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, HIGH);
  for (int y = 0; y < LCD_H; y++) SPI.writeBytes(row, sizeof(row));
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

void lcdInit() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_CS, HIGH);

  ledcAttach(PIN_BL, 1000, 10);
  lcdSetBacklight(100);

  SPI.begin(PIN_SCLK, -1, PIN_MOSI, -1);

  // リセット (メーカー手順: CSをLOWにしてからRSTパルス)
  digitalWrite(PIN_CS, LOW);
  delay(50);
  digitalWrite(PIN_RST, LOW);
  delay(50);
  digitalWrite(PIN_RST, HIGH);
  delay(50);
  digitalWrite(PIN_CS, HIGH);

  // --- 初期化シーケンス (メーカー公式デモ verbatim) ---
  cmd(0x11); delay(120);                       // Sleep out
  cmd(0x36); dat(madctlVal);                   // MADCTL: 既定 0x08 (BGR, no mirror)
  cmd(0x3A); dat(0x05);                        // COLMOD: RGB565
  cmd(0xB0); dat(0x00); dat(0xE8);
  cmd(0xB2); dat(0x0C); dat(0x0C); dat(0x00); dat(0x33); dat(0x33);
  cmd(0xB7); dat(0x35);
  cmd(0xBB); dat(0x35);
  cmd(0xC0); dat(0x2C);
  cmd(0xC2); dat(0x01);
  cmd(0xC3); dat(0x13);
  cmd(0xC4); dat(0x20);
  cmd(0xC6); dat(0x0F);
  cmd(0xD0); dat(0xA4); dat(0xA1);
  cmd(0xD6); dat(0xA1);
  cmd(0xE0);
  { const uint8_t g[] = {0xF0,0x00,0x04,0x04,0x04,0x05,0x29,0x33,0x3E,0x38,0x12,0x12,0x28,0x30}; datN(g, sizeof(g)); }
  cmd(0xE1);
  { const uint8_t g[] = {0xF0,0x07,0x0A,0x0D,0x0B,0x07,0x28,0x33,0x3E,0x36,0x14,0x14,0x29,0x32}; datN(g, sizeof(g)); }
  cmd(0x21);                                   // INVON (IPS)
  cmd(0x11); delay(120);
  cmd(0x29);                                   // Display on

  lcdFill(0x0000);
}
