# SmartGlass HUD v0.3.4 — GC9107+回転 (上下調整) 対応版

## v0.3.4 の変更点 (左眼用に上下調整 / 2026-11-20)
v0.3.3 でプリズム越しに文字が読めるようになった (ユーザー目視確認済み) を受け、
左眼で見る試作品に合わせて画面の上下 (回転) も調整できるようにした。

1. **回転 (ROT) を反転と独立に追加** — `ROT:0..3` (パネル側 0=なし/1=時計回り/
   2=180°/3=反時計回り)。反転と MADCTL 表で合成し NVS 保存。
2. **デフォルトは ROT:3** — 反転ONと組み合わせると MADCTL=0x28 (MVのみ) になり、
   プリズム越しの見た目では「左が上・右が下」の向きになる (鏡像合成で回転方向が
   逆転するため)。反対向きが正解だった場合は ROT:1 で即切替可能。
3. **切替経路**: BLE `ROT:n` (未認証可) / USBシリアル `ROT:n`・`ROT:CYCLE` /
   BOOTダブルタップ (短押し2回で90°進む) / iOSアプリの回転ボタン。
4. MV使用時は LCDドライバの表示オフセット (2,1) も軸に合わせて入れ替える。

検証 (2026-11-20, 実機 COM14):
- コンパイル成功 (991,557 bytes)、merged.bin SHA-256
  `7a19c6680f620ec1caa7b30f18a55be24eb30b612ce09ab3451043d41892dff5` を
  J3L経由で書き込み・ハッシュ検証OK。
- シリアルで起動ログ `v0.3.4 / MIRROR:ON / ROT:3 (madctl=0x28)` と
  ROT:1⇄ROT:3 の切替を確認。BLE (J3L/bleak) からも未認証 ROT 切替 → `ROT_OK` を確認。
- 最終状態: mirror=1 / rot=3 / 未ペアリング。プリズム越しの目視確認はユーザー待ち。

---

# SmartGlass HUD v0.3.3 — GC9107+プリズム反転デフォルト版

## v0.3.3 の変更点 (単眼プリズム試作品対応 / 2026-11-20)
試作品 (段ボールトンネル＋プリズム) で「プリズム越しに文字が鏡像で読めない」
問題への対応と、接続までの詰まり解消が目的。

1. **左右反転 (MIR) デフォルトON** — 起動直後からプリズム越しに読める向きで表示。
   NVSに保存済みの設定があればそちらを優先。
2. **BOOTボタン短押しで反転トグル** — 押して離す (80ms〜2秒) とその場で
   反転ON/OFFを切り替えNVS保存。BLE・認証不要。直接画面でQRを読みたい時は
   短押しでOFFに戻す。5秒長押しのペアリングリセットは従来通り。
3. **MIR:0 / MIR:1 を未認証でも受付** — ペアリングコードを読む「前」に
   向きを直せるようにした (従来は認証後必須で鶏卵問題だった)。
4. **USBシリアルコマンド追加** (115200): `MIR:1` / `MIR:0` / `MIR:TOGGLE` /
   `CODE` (現在のペアリングコード表示) / `STATUS` (状態表示)。
   J3L経由で表示が読めない状態でも操作・確認できる。
5. **ペアリング画面の出し分け** — 反転ON時はQRを出さずコード画面のみ
   (鏡像QRは直接スキャン不可のため)。コードをプリズム越しに読んで手入力する。
   反転OFF時は従来通りコード⇄QRを4秒交互表示。
6. **iOSアプリに「画面の向き（プリズム）」セクション追加** — 接続後だけでなく
   ペアリング前でも反転を切り替えられるボタンを配置。`MIR_OK` 通知で表示も同期。

検証 (2026-11-20, 実機 COM14 / MAC 68:ee:8f:db:e6:f8):
- コンパイル成功 (990,553 bytes, Core 3.0.3 / profile hud303)、
  merged.bin SHA-256 `2c44a99da3a7a57a7c610d844982c73f95f905e1e79ea2b61fa0a4f7d1b08c00`
  を J3L経由で書き込み・ハッシュ検証OK。
- シリアルで起動ログ `v0.3.3 / MIRROR:ON (prism)`、STATUS / MIR 切替 / CODE を確認。
- J3L (Windows) の BLE (bleak) から実接続テスト: 未認証 MIR:0/MIR:1 → `MIR_OK`,
  PING → `ERR:AUTH` (認証ゲート維持), PAIR → TOKEN受信, TXT → `TXT_OK`,
  PING → `PONG`, 最後に UNPAIR で未ペアリング状態へ復旧済み。
- 残る実機確認: プリズム越しにコード/テキストが読めるかのユーザー目視、
  iPhone実機からのBLE接続 (AltStoreのApp ID制限の解消待ち)。

---

# SmartGlass HUD v0.3.2 — GC9107+QR版 (v0.3スターターキットのファーム修正版)

## v0.3.2 の追加点 (QRペアリング表示)
- ペアリング待ち画面で、6桁コード画面と **QRコード画面を4秒ごとに交互表示**。
- QRペイロード: `sghud://pair?c=<6桁コード>&n=SG-HUD-01`（Version 4 / ECC LOW、
  33×33モジュールを3倍描画）。生成は ricmoo/QRCode（MIT）を同梱。
- iPhone標準カメラでも内容を読める（動作確認用）。アプリ側のQRスキャナ
  （読んだら自動で PAIR 送信）は IPA フェーズで iOS アプリに追加する。
- 注意: QRは「コード入力の置き換え」であって、アプリ無しでBLE接続する手段では
  ない（iOS標準では任意BLEデバイスへのQR接続は不可）。

ESP32ファームウェアだけを修正した版。iOSアプリ / GATT仕様 / `project.yml` /
IPAワークフローは v0.3 から変更なし（互換維持）。

**Status: コンパイル検証は本README末尾の記録参照。実機（表示・BLE）は未検証。**

## v0.3 → v0.3.1 の変更点

1. **LCD: ST7789 → GC9107（メーカー実績ドライバ）**
   - v0.3 は Arduino_GFX の `Arduino_ST7789` を使用。対象基板 SpotPear
     ESP32S3-0.85-SD のメーカー公式デモ（github.com/HaNA17c/ESP32-S3-0.85-SD）
     の初期化シーケンスを verbatim で使う自前ドライバ `lcd_gc9107.cpp/h` に差し替え。
   - ピン: MOSI=39 / SCLK=40 / CS=41 / DC=42 / RST=38 / BL=14、オフセット (2,1)、
     MADCTL 既定 0x08。いずれもメーカー実績値。
   - 補足: Arduino_GFX にも `Arduino_GC9107` はあるが、その初期化列は汎用品で
     メーカー実績列とは別物のため採用しなかった。
   - 描画は 128×128 RGB565 フレームバッファ＋5×7フォント（`font5x7.h`、
     Adafruit GFX の glcdfont 由来）に置き換え。**Arduino_GFX_Library は不要**に
     なった（外部ライブラリ依存ゼロ。BLE/Preferences は Core 同梱）。

2. **ESP32 Arduino Core を 3.0.3 に固定**
   - メーカー推奨が 3.0.3 であること、BLEコールバック署名が 3.x の途中で
     変わることから、「3.x 最新」指定をやめて 3.0.3 に固定。
   - 固定方法: `esp32/SmartGlassHUD/sketch.yaml`（arduino-cli profile）、
     `.github/workflows/esp32-compile.yml`（CIコンパイルチェック）、
     Arduino IDE の場合は Boards Manager で **esp32 by Espressif Systems の
     バージョン 3.0.3 を明示的に選ぶ**。
   - 合わせてBLEコールバックを単一引数オーバーロードに統一した。3.0.3 では
     サーバが `onConnect(this)` と `onConnect(this,param)` の両方を呼び、
     Characteristic の param付き `onWrite` は単一引数版へ委譲される
     （3.0.3 の BLEServer.cpp / BLECharacteristic.cpp で確認済み）。

3. **MIR コマンド追加（反射表示用）**
   - 認証後に `MIR:1` を送ると MADCTL に MX を立てて左右反転し、NVS に保存
     （次回起動時も適用）。プリズム／反射板越しに文字が正しく読めるようにする用。
     直接画面を見る時は `MIR:0`。応答は `MIR_OK:1` / `MIR_OK:0`。
   - iOSアプリ側のUIはまだ無いので、送信は今のところ手動／次フェーズで追加。

4. **再描画は変更時のみ**（v0.3 の 350ms 全画面再描画によるちらつきを軽減）。
   BOOT 5秒長押しのリセットは、ボタンを離すまで再発しないよう挙動を整理。

## Windows 11 — ESP32 firmware (Arduino IDE)

1. Arduino IDE 2.x を入れる。
2. Board Manager URL に `https://espressif.github.io/arduino-esp32/package_esp32_index.json` を追加。
3. **esp32 by Espressif Systems の 3.0.3** を選んでインストール（最新版にしない）。
4. 外部ライブラリは不要（v0.3.1から）。
5. ボード設定: `ESP32S3 Dev Module`、USB CDC On Boot: Enabled、USB Mode: Hardware CDC and JTAG、
   **Flash Size: 4MB、PSRAM: QSPI PSRAM、Partition Scheme: Huge APP (3MB)**、
   COM ポートは実機のもの。
   ※実機チップは ESP32-S3FH4R2（Flash 4MB内蔵 / PSRAM 2MB内蔵・QSPI）だった
     （2026-11-19 J3L・esptool実測、MAC 68:ee:8f:db:e6:f8）。16MB/OPI の想定は誤り。
6. **書き込み前に工場ファームのバックアップを取る**（下記）。その後 Upload。
7. 起動すると `PAIR READY` と6桁コードが出る。Serial Monitor は 115200 baud。

arduino-cli の場合:
```
arduino-cli core install esp32:esp32@3.0.3 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli compile --profile hud303 esp32/SmartGlassHUD
```

### 工場ファームのバックアップ（推奨・書き込み前に必須）
```
python -m pip install esptool
python -m esptool --port COMx flash_id        # フラッシュ容量を確認してから
python -m esptool --port COMx read_flash 0 <flash_idで判明した容量> factory-backup.bin
```
容量を決め打ちしないこと。バックアップには認証情報が含まれ得るので取り扱い注意。

## GATT specification (v0.3から不変)
Service: `AC410000-7A01-4B0A-9A9F-8C2A013A0001`
Write:   `AC410001-7A01-4B0A-9A9F-8C2A013A0001`
Notify:  `AC410002-7A01-4B0A-9A9F-8C2A013A0001`
Commands: `PAIR:123456`, `AUTH:<token>`, `TXT:HELLO`, `PING`, `MIR:0`, `MIR:1`, `UNPAIR` (authorized)
Notifications: `TOKEN:<token>`, `AUTH_OK`, `TXT_OK`, `PONG`, `MIR_OK:*`, `ERR:*`

## 引き続きの制限 (v0.3から不変)
- 6桁コードはアプリ層の簡易認証で、BLE Secure Connections bonding ではない。
  コードとトークンはアプリ層暗号化なしで流れる。信頼できる環境でのみ使用。
- `TXT:` は ASCII のみ。日本語グリフ描画は次フェーズ。
- BLEペイロードの分割送信なし（長いテキストは次フェーズ）。
- 自動再接続は iOS アプリがフォアグラウンドで動いている時だけ。

## コンパイル検証記録
- 2026-11-19: arduino-cli 1.6.0 + esp32:esp32 **3.0.3**、`--profile hud303` で
  **コンパイル成功**。Sketch 982,433 bytes（Huge APP 3MB の31%）、
  グローバル変数 78,184 bytes（23%）。merged.bin SHA-256:
  `2abfedffd2dffcb44b91d1a2f40d8645c5fce199f85c2206db6f4f22385b9e65`
- 2026-11-19 実機書き込み: J3L (COM14) 経由で esptool write_flash 0x0 (merged.bin)、
  ハッシュ検証OK。書き込み前に工場ファーム全量バックアップ取得済み
  （J3L `C:\Users\jacklow\muse\smartglass-hud\factory-backup-20261119.bin`、
  4,194,304 bytes、SHA-256 `7663d9656b17298c2924e7c08478d87956e0794270e30b9633ee4b884d1c6886`）。
- 実機の表示確認（PAIR READY＋6桁コード）とBLE接続確認はユーザー目視待ち。
- 2026-11-19 v0.3.2 (QR追加版): コンパイル成功（987,789 bytes）、merged.bin SHA-256
  `fe60d2775e599372d848989ad6af0032be5a9c8405f6254fb48655f22ef5b75c` を
  J3L経由で書き込み・ハッシュ検証OK・リセット済み。
- 2026-11-19 **実機表示確認OK（ユーザーの写真で検証）**: コード画面は
  `PAIR READY`（シアン）／`118232`（白・大）／`SCAN QR ->`（緑）／青ラインで
  色・向き・レイアウトとも設計通り。QR画面の写真を OpenCV でデコードし
  `sghud://pair?c=118232&n=SG-HUD-01` が得られ、コード画面の数字と一致。
  GC9107ドライバ・フレームバッファ描画・QR生成の実機動作が確定。
  残るは BLE接続と iPhone アプリ（IPA/AltStoreフェーズ）。
