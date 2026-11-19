// ============================================================
// SmartGlass HUD v0.3.3 (GC9107版) — BLE MVP prototype
// v0.3.3: 単眼プリズム試作品向けに「文字が読める」ことを最優先に改良。
//  6. 左右反転 (MIR) のデフォルトを ON に変更。プリズム/反射板越しで
//     そのまま文字が読める状態で起動する (NVSに保存済みの設定があればそれを優先)。
//  7. BOOTボタンの短押し (離した時に 80ms〜2秒) で反転をその場でトグル。
//     BLEも認証も不要。直接画面でQRを読みたい時は短押しで反転OFFに戻せる。
//     5秒長押しのペアリングリセットは従来通り。
//  8. MIR:0 / MIR:1 を未認証・未ペアリングでも受け付けるように変更
//     (表示の向きは機密ではない。ペアリングコードを読む前に反転できる必要があるため)。
//  9. USBシリアル (115200) からも MIR:0 / MIR:1 / MIR:TOGGLE / CODE / STATUS を
//    受け付ける。J3L経由で表示が読めなくてもコード確認と反転設定ができる。
// 10. ペアリング画面: 反転ON (プリズム使用中) の時はQRを出さずコード画面のみ表示
//     (鏡像QRは直接スキャンできず、トンネル越しスキャンも実用的でないため)。
//     コードはプリズム越しに読んでスマホへ手入力する。反転OFF時は従来通り
//     コード画面とQR画面を4秒交互表示。
//
// v0.3.2: ペアリング画面にQRコード表示を追加 (コード画面と4秒交互)。
//   QRペイロード: sghud://pair?c=<6桁コード>&n=SG-HUD-01
//   iPhone標準カメラでも読める (アプリ側スキャナはIPAフェーズで追加)。
//   QR生成は ricmoo/QRCode (MIT, qrcode.c/h同梱)。Version 4 / ECC LOW。
//
// v0.3スターターキットからの変更点:
//  1. LCDドライバを Arduino_GFX の ST7789 から、SpotPear公式デモの
//     初期化シーケンスをそのまま使う自前 GC9107 ドライバ (lcd_gc9107.cpp)
//     に差し替え。ピンは MOSI39/SCLK40/CS41/DC42/RST38/BL14、offset(2,1)。
//     ※ Arduino_GFX にも GC9107 クラスはあるが初期化列が汎用品で
//       メーカー実績列と異なるため、実績列 verbatim のこちらを採用。
//  2. 描画は 128x128 RGB565 フレームバッファ + 5x7フォント (font5x7.h)。
//     Arduino_GFX_Library への依存を廃止。
//  3. BLEコールバックを単一引数オーバーロードに統一。
//     ESP32 Arduino Core 3.0.3 ではサーバが両形式を呼び、Characteristic
//     の (param付き) onWrite は単一引数版へ委譲されるため、これで正しく動く。
//     Core は 3.0.3 に固定すること (README参照)。
//  4. MIR:0 / MIR:1 コマンド追加 (要認証)。プリズム/反射板越しに読む時は
//     MIR:1 で左右反転を NVS に保存し、次回起動時から適用する。
//  5. 画面再描画は内容が変わった時だけ (ちらつき軽減)。
//
// GATT / コマンド体系は v0.3 の iOS アプリと互換 (UUID・コマンド不変)。
// セキュリティ上の注意は v0.3 から不変: 6桁コードはアプリ層の簡易認証で
// BLE Secure Connections ではない。信頼できる環境でのみ使うこと。
// ============================================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <esp_system.h>
#include "lcd_gc9107.h"
#include "font5x7.h"
#include "qrcode.h"

static const char* SERVICE_UUID = "ac410000-7a01-4b0a-9a9f-8c2a013a0001";
static const char* WRITE_UUID   = "ac410001-7a01-4b0a-9a9f-8c2a013a0001";
static const char* STATUS_UUID  = "ac410002-7a01-4b0a-9a9f-8c2a013a0001";

// ---- 色 (RGB565) ----
static constexpr uint16_t COL_BLACK = 0x0000;
static constexpr uint16_t COL_WHITE = 0xFFFF;
static constexpr uint16_t COL_CYAN  = 0x07FF;
static constexpr uint16_t COL_BLUE  = 0x001F;
static constexpr uint16_t COL_GREEN = 0x07E0;

// ---- フレームバッファ (RGB565 ビッグエンディアンで保持し、そのまま流す) ----
static uint8_t fb[LCD_W * LCD_H * 2];
static bool fbDirty = true;

static void fbPixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
  size_t i = ((size_t)y * LCD_W + x) * 2;
  fb[i] = color >> 8;
  fb[i + 1] = color & 0xFF;
}
static void fbClear(uint16_t color) {
  for (int i = 0; i < LCD_W * LCD_H; i++) {
    fb[i * 2] = color >> 8;
    fb[i * 2 + 1] = color & 0xFF;
  }
}
static void fbHLine(int x, int y, int w, uint16_t color) {
  for (int i = 0; i < w; i++) fbPixel(x + i, y, color);
}
// 5x7フォント描画。scale倍、文字セルは 6*scale × 8*scale。
static void fbChar(int x, int y, char c, uint16_t color, int scale) {
  unsigned char uc = (unsigned char)c;
  for (int col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&FONT5X7[uc * 5 + col]);
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        for (int dx = 0; dx < scale; dx++)
          for (int dy = 0; dy < scale; dy++)
            fbPixel(x + col * scale + dx, y + row * scale + dy, color);
      }
    }
  }
}
static void fbText(int x, int y, const String& s, uint16_t color, int scale) {
  int cx = x;
  for (size_t i = 0; i < s.length(); i++) {
    fbChar(cx, y, s[i], color, scale);
    cx += 6 * scale;
    if (cx + 6 * scale > LCD_W) break;
  }
}
static void fbFlush() {
  lcdDrawBlock(0, 0, LCD_W, LCD_H, fb, sizeof(fb));
  fbDirty = false;
}

// ---- 状態 ----
Preferences prefs;
BLEServer* server = nullptr;
BLECharacteristic* statusChar = nullptr;
bool connected = false, authorized = false, paired = false;
uint32_t pairingCode = 0;
String savedToken, currentText = "READY";
String pendingText = "";
bool pending = false;
bool mirrorMode = false;

void showLines(const String& top, const String& middle, const String& bottom) {
  fbClear(COL_BLACK);
  fbText(6, 8, top, COL_CYAN, 1);
  fbHLine(6, 23, 116, COL_BLUE);
  fbText(6, 43, middle.substring(0, 9), COL_WHITE, 2);
  fbText(6, 107, bottom.substring(0, 19), COL_GREEN, 1);
  fbDirty = true;
}
// ---- ペアリング用QR (Version 4 = 33x33モジュール, ECC LOW) ----
static QRCode qrObj;
static uint8_t qrBuf[256];
static bool qrReady = false;
void genQR() {
  char payload[48];
  snprintf(payload, sizeof(payload), "sghud://pair?c=%06lu&n=SG-HUD-01", (unsigned long)pairingCode);
  qrReady = (qrcode_initText(&qrObj, qrBuf, 4, ECC_LOW, payload) == 0);
}
void showQR() {
  fbClear(COL_WHITE);
  if (qrReady) {
    const int scale = 3;
    int off = (LCD_W - qrObj.size * scale) / 2;  // 白マージン込みで中央配置
    for (int y = 0; y < qrObj.size; y++)
      for (int x = 0; x < qrObj.size; x++)
        if (qrcode_getModule(&qrObj, x, y))
          for (int dx = 0; dx < scale; dx++)
            for (int dy = 0; dy < scale; dy++)
              fbPixel(off + x * scale + dx, off + y * scale + dy, COL_BLACK);
  }
  fbDirty = true;
}

void status(const String& s) {
  if (statusChar) { statusChar->setValue(s.c_str()); if (connected) statusChar->notify(); }
  Serial.println(s);
}
// 左右反転の適用 (NVS保存つき)。BLE / シリアル / BOOTボタン共通。
void applyMirror(bool on, bool save) {
  mirrorMode = on;
  if (save) prefs.putBool("mirror", on);
  lcdSetMirror(on);
  fbDirty = true;
  Serial.println(on ? "MIRROR:ON (prism)" : "MIRROR:OFF (direct)");
}
String makeToken() {
  char b[33]; for (int i = 0; i < 32; i++) b[i] = "0123456789abcdef"[esp_random() % 16]; b[32] = 0;
  return String(b);
}
void resetPairing() {
  prefs.remove("token"); savedToken = ""; paired = false; authorized = false;
  pairingCode = 100000 + (esp_random() % 900000);
  genQR();
  status("PAIR_RESET");
}

class ServerCallbacks : public BLEServerCallbacks {
  // Core 3.0.3: サーバは onConnect(this) と onConnect(this,param) の両方を呼ぶ。
  // 単一引数版だけを override すれば十分 (param付き既定実装は何もしない)。
  void onConnect(BLEServer* s) override { connected = true; authorized = false; fbDirty = true; }
  void onDisconnect(BLEServer* s) override {
    connected = false; authorized = false; fbDirty = true;
    BLEDevice::startAdvertising();
  }
};
class WriteCallbacks : public BLECharacteristicCallbacks {
  // Core 3.0.3: (param付き) onWrite の既定実装が単一引数版へ委譲する。
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v == "MIR:0" || v == "MIR:1") {
      // v0.3.3: 反転は未認証でも可 (コードを読む前に向きを直す必要があるため)
      applyMirror(v == "MIR:1", true);
      status(mirrorMode ? "MIR_OK:1" : "MIR_OK:0");
    } else if (v.startsWith("PAIR:") && !paired) {
      if (v.substring(5).toInt() == (int)pairingCode) {
        savedToken = makeToken(); prefs.putString("token", savedToken);
        paired = true; authorized = true; status("TOKEN:" + savedToken);
      } else status("ERR:CODE");
    } else if (v.startsWith("AUTH:") && paired) {
      authorized = (v.substring(5) == savedToken);
      status(authorized ? "AUTH_OK" : "ERR:AUTH");
    } else if (v == "PING") {
      status(authorized ? "PONG" : "ERR:AUTH");
    } else if (v.startsWith("TXT:") && authorized) {
      // ASCII MVP。日本語グリフ描画は次フェーズ (v0.3から不変)。
      pendingText = v.substring(4); pending = true; status("TXT_OK");
    } else if (v == "UNPAIR" && authorized) {
      resetPairing();
    } else status("ERR:UNAUTHORIZED");
    fbDirty = true;
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  prefs.begin("smartglass", false);
  savedToken = prefs.getString("token", "");
  paired = savedToken.length() == 32;
  mirrorMode = prefs.getBool("mirror", true);  // v0.3.3: 既定ON (プリズム越しで読める向き)
  pairingCode = 100000 + (esp_random() % 900000);
  genQR();

  lcdInit();                       // GC9107 + メーカー初期化列、BL 100%
  lcdSetBacklight(80);             // HUD用途に少し落とす
  if (mirrorMode) lcdSetMirror(true);

  BLEDevice::init("SG-HUD-01");
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);
  BLECharacteristic* rx = service->createCharacteristic(WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new WriteCallbacks());
  statusChar = service->createCharacteristic(STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());
  statusChar->setValue("READY");
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("SmartGlass HUD v0.3.3 (GC9107+MIR default) advertising as SG-HUD-01");
  Serial.println(mirrorMode ? "MIRROR:ON (prism)" : "MIRROR:OFF (direct)");
  Serial.println("Serial cmds: MIR:1 / MIR:0 / MIR:TOGGLE / CODE / STATUS");
}

// ---- USBシリアルコマンド (J3Lから表示なしで操作・確認するため) ----
void handleSerial() {
  static String line = "";
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      line.trim();
      if (line == "MIR:1") { applyMirror(true, true); Serial.println("MIR_OK:1"); }
      else if (line == "MIR:0") { applyMirror(false, true); Serial.println("MIR_OK:0"); }
      else if (line == "MIR:TOGGLE") { applyMirror(!mirrorMode, true); Serial.println(mirrorMode ? "MIR_OK:1" : "MIR_OK:0"); }
      else if (line == "CODE") { Serial.printf("CODE:%06lu\n", (unsigned long)pairingCode); }
      else if (line == "STATUS") {
        Serial.printf("STATUS v0.3.3 paired=%d connected=%d authorized=%d mirror=%d code=%06lu\n",
                      paired ? 1 : 0, connected ? 1 : 0, authorized ? 1 : 0, mirrorMode ? 1 : 0,
                      (unsigned long)pairingCode);
      }
      line = "";
    } else if (line.length() < 64) {
      line += ch;
    }
  }
}

void loop() {
  handleSerial();

  // BOOTボタン: 短押し (離した時 80ms〜2秒) = 左右反転トグル / 5秒長押し = ペアリング消去
  static unsigned long pressStart = 0;
  static bool pressing = false;
  static bool resetDone = false;
  bool btnLow = (digitalRead(0) == LOW);
  unsigned long nowMs = millis();
  if (btnLow && !pressing) {
    pressing = true; pressStart = nowMs; resetDone = false;
  } else if (btnLow && pressing && !resetDone && nowMs - pressStart > 5000) {
    resetPairing(); resetDone = true; fbDirty = true;
  } else if (!btnLow && pressing) {
    unsigned long dur = nowMs - pressStart;
    pressing = false;
    if (!resetDone && dur >= 80 && dur < 2000) {
      applyMirror(!mirrorMode, true);
      status(mirrorMode ? "MIR_OK:1" : "MIR_OK:0");
    }
    resetDone = false;
  }

  if (pending) { currentText = pendingText; pending = false; fbDirty = true; }

  if (!paired) {
    if (mirrorMode) {
      // プリズム使用中: コード画面のみ (プリズム越しに読んで手入力)。QRは出さない。
      if (fbDirty) {
        showLines("PAIR READY", String(pairingCode), "PRISM MODE TAP=QR");
        fbFlush();
      }
    } else {
      // 直接表示: コード画面とQR画面を4秒ごとに交互表示
      static int lastPhase = -1;
      int phase = (millis() / 4000) % 2;
      if (phase != lastPhase || fbDirty) {
        if (phase == 0) showLines("PAIR READY", String(pairingCode), "SCAN QR ->");
        else showQR();
        fbFlush();
        lastPhase = phase;
      }
    }
  } else if (fbDirty) {
    if (!connected) showLines("SMARTGLASS", "WAIT", "BLE advertising");
    else if (!authorized) showLines("SMARTGLASS", "LOCKED", "Open iPhone app");
    else showLines("SMARTGLASS", currentText, mirrorMode ? "BLE CONN MIR" : "BLE CONNECTED");
    fbFlush();
  }
  delay(20);
}
