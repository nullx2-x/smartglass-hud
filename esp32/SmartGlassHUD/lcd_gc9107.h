#pragma once
#include <Arduino.h>

// ============================================================
// SpotPear ESP32-S3-0.85-SD (Mini) 内蔵 LCD ドライバ
// パネル: 0.85" 128x128, コントローラ GC9107, 4線SPI
// ピン割り当てと初期化シーケンスはメーカー公式デモ
// (github.com/HaNA17c/ESP32-S3-0.85-SD) の実績値を使用:
//   MOSI=39  SCLK=40  CS=41  DC=42  RST=38  BL=14
//   オフセット X=2, Y=1 / MADCTL=0x08
// ============================================================

#define LCD_W 128
#define LCD_H 128

void lcdInit();
void lcdSetMirror(bool mirror);                   // HUD反射(プリズム)用: trueでMADCTLにMXを立てて左右反転
void lcdSetOrientation(uint8_t madctl);           // v0.3.4: MADCTL値を直接設定 (MX/MY/MVの組み合わせで反転+回転)。BGRビットはドライバ側で付与
void lcdSetBacklight(uint8_t percent);            // 0..100
void lcdFill(uint16_t color565);                  // 全画面単色
// ブロック描画 (RGB565ビッグエンディアンのバイト列をそのまま流す)
void lcdDrawBlock(int x, int y, int w, int h, const uint8_t *data, size_t len);
