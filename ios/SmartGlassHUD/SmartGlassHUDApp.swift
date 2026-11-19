import SwiftUI
import CoreBluetooth
import Security
import AVFoundation

private let serviceID = CBUUID(string: "AC410000-7A01-4B0A-9A9F-8C2A013A0001")
private let writeID   = CBUUID(string: "AC410001-7A01-4B0A-9A9F-8C2A013A0001")
private let statusID  = CBUUID(string: "AC410002-7A01-4B0A-9A9F-8C2A013A0001")

@MainActor final class HUDCentral: NSObject, ObservableObject {
    @Published var state = "Bluetoothを準備中"
    @Published var devices: [CBPeripheral] = []
    @Published var pairingCode = ""
    @Published var message = "HELLO HUD"
    @Published var authorized = false
    @Published var connectedName = ""
    @Published var mirrorOn = true   // v0.3.3 firmware default: prism (mirrored)
    @Published var rotation = 3      // v0.3.4 firmware default: left side up for left eye
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var tx: CBCharacteristic?
    private var rx: CBCharacteristic?
    private var rememberedID: UUID? {
        get { UserDefaults.standard.string(forKey: "hudUUID").flatMap(UUID.init(uuidString:)) }
        set { UserDefaults.standard.set(newValue?.uuidString, forKey: "hudUUID") }
    }
    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }
    func scan() {
        guard central.state == .poweredOn else { state = "Bluetoothをオンにしてください"; return }
        devices.removeAll()
        state = "近くのHUDを検索中"
        central.scanForPeripherals(withServices: [serviceID], options: [CBCentralManagerScanOptionAllowDuplicatesKey:false])
    }
    func connect(_ device: CBPeripheral) {
        central.stopScan()
        peripheral = device
        device.delegate = self
        state = "接続中..."
        central.connect(device, options: nil)
    }
    func forgetSavedPairing() {
        if let id = peripheral?.identifier ?? rememberedID {
            let query: [String:Any] = [kSecClass as String:kSecClassGenericPassword,
                                      kSecAttrService as String:"SmartGlassHUD",
                                      kSecAttrAccount as String:id.uuidString]
            SecItemDelete(query as CFDictionary)
        }
        rememberedID = nil
        authorized = false
        state = "保存済み認証情報を削除しました。HUD側もBOOTを5秒長押しして再ペアリングしてください"
    }
    func applyQRCode(_ raw: String) {
        // Payload from HUD firmware: sghud://pair?c=123456&n=SG-HUD-01
        guard let comps = URLComponents(string: raw), comps.scheme == "sghud",
              let code = comps.queryItems?.first(where: { $0.name == "c" })?.value,
              code.count == 6, code.allSatisfy(\.isNumber) else {
            state = "QRの形式が違います（SmartGlass HUD用ではありません）"; return
        }
        pairingCode = code
        state = "QRからコードを取得しました"
        // Already connected: pair immediately. Otherwise pair() runs after
        // characteristic discovery (see didDiscoverCharacteristicsFor).
        if peripheral != nil && tx != nil && !authorized { pair() }
    }
    func pair() {
        guard pairingCode.count == 6, pairingCode.allSatisfy(\.isNumber) else { state = "6桁のコードを入力"; return }
        send("PAIR:" + pairingCode)
    }
    func cycleRotation() {
        // v0.3.4: rotate 90 degrees per tap (panel-side steps; watch the HUD and stop at the readable orientation)
        rotation = (rotation + 1) % 4
        send("ROT:" + String(rotation))
    }
    func setMirror(_ on: Bool) {
        // v0.3.3: mirror can be changed before pairing/auth (firmware allows it),
        // so the user can fix the orientation before reading the pairing code.
        mirrorOn = on
        send(on ? "MIR:1" : "MIR:0")
    }
    func sendText() {
        guard authorized else { state = "認証してください"; return }
        // ASCII demo until Japanese font support is added on the ESP32.
        guard message.utf8.count <= 100 else { state = "100バイト以下にしてください"; return }
        send("TXT:" + message)
    }
    private func send(_ value: String) {
        guard let p = peripheral, let c = tx else { state = "接続がありません"; return }
        let data = Data(value.utf8)
        guard data.count <= p.maximumWriteValueLength(for: .withResponse) else {
            state = "BLE送信サイズを超過。短い文字列にしてください"; return
        }
        p.writeValue(data, for: c, type: .withResponse)
    }
    private func storedToken(for id: UUID) -> String? {
        let account = id.uuidString
        let query: [String:Any] = [kSecClass as String:kSecClassGenericPassword,
                                   kSecAttrService as String:"SmartGlassHUD",
                                   kSecAttrAccount as String:account,
                                   kSecReturnData as String:true,
                                   kSecMatchLimit as String:kSecMatchLimitOne]
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data:data,encoding:.utf8)
    }
    private func saveToken(_ token: String, for id: UUID) {
        let query: [String:Any] = [kSecClass as String:kSecClassGenericPassword,
                                   kSecAttrService as String:"SmartGlassHUD",
                                   kSecAttrAccount as String:id.uuidString]
        SecItemDelete(query as CFDictionary)
        var add = query
        add[kSecValueData as String] = Data(token.utf8)
        SecItemAdd(add as CFDictionary, nil)
    }
}
extension HUDCentral: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            if central.state == .poweredOn { self.scan() }
            else { self.state = "Bluetoothを利用できません" }
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                     advertisementData: [String:Any], rssi RSSI: NSNumber) {
        Task { @MainActor in
            if !self.devices.contains(where: {$0.identifier == peripheral.identifier}) { self.devices.append(peripheral) }
            if self.rememberedID == peripheral.identifier && self.peripheral == nil { self.connect(peripheral) }
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.connectedName = peripheral.name ?? "SG-HUD"
            self.state = "サービスを確認中"
            peripheral.discoverServices([serviceID])
        }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in self.state = "接続失敗"; self.peripheral = nil; self.scan() }
    }
    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            self.authorized = false; self.tx = nil; self.rx = nil; self.peripheral = nil
            self.state = "切断。再検索中"; self.scan()
        }
    }
}
extension HUDCentral: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            peripheral.services?.filter{$0.uuid == serviceID}.forEach {
                peripheral.discoverCharacteristics([writeID,statusID],for:$0)
            }
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            for c in service.characteristics ?? [] {
                if c.uuid == writeID { self.tx = c }
                if c.uuid == statusID { self.rx = c; peripheral.setNotifyValue(true,for:c) }
            }
            self.rememberedID = peripheral.identifier
            if let token = self.storedToken(for:peripheral.identifier) {
                self.send("AUTH:" + token); self.state = "自動認証中"
            } else if self.pairingCode.count == 6 {
                self.pair(); self.state = "QR/入力済みコードでペアリング中"
            } else { self.state = "HUDに表示された6桁コードを入力" }
        }
    }
    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            guard let data = characteristic.value, let value = String(data:data,encoding:.utf8) else { return }
            if value.hasPrefix("TOKEN:") {
                let token = String(value.dropFirst(6))
                self.saveToken(token,for:peripheral.identifier)
                self.authorized = true; self.state = "ペアリング完了"
            } else if value == "AUTH_OK" { self.authorized = true; self.state = "自動再接続完了" }
            else if value == "MIR_OK:1" { self.mirrorOn = true; self.state = "左右反転ON（プリズム用）" }
            else if value == "MIR_OK:0" { self.mirrorOn = false; self.state = "左右反転OFF（直接表示）" }
            else if value.hasPrefix("ROT_OK:") { self.rotation = Int(value.dropFirst(7)) ?? self.rotation; self.state = "回転を切り替えました（ROT:\(self.rotation)）" }
            else { self.state = value }
        }
    }
}
// MARK: - QR scanner (AVFoundation)
final class ScannerViewController: UIViewController, AVCaptureMetadataOutputObjectsDelegate {
    var onScan: ((String) -> Void)?
    private let session = AVCaptureSession()
    private var preview: AVCaptureVideoPreviewLayer?
    private var scanned = false
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        guard let device = AVCaptureDevice.default(for: .video),
              let input = try? AVCaptureDeviceInput(device: device),
              session.canAddInput(input) else { return }
        session.addInput(input)
        let output = AVCaptureMetadataOutput()
        guard session.canAddOutput(output) else { return }
        session.addOutput(output)
        output.setMetadataObjectsDelegate(self, queue: .main)
        output.metadataObjectTypes = [.qr]
        let layer = AVCaptureVideoPreviewLayer(session: session)
        layer.videoGravity = .resizeAspectFill
        layer.frame = view.layer.bounds
        view.layer.addSublayer(layer)
        preview = layer
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in self?.session.startRunning() }
    }
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        preview?.frame = view.layer.bounds
    }
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        if session.isRunning {
            DispatchQueue.global().async { [weak self] in self?.session.stopRunning() }
        }
    }
    func metadataOutput(_ output: AVCaptureMetadataOutput, didOutput metadataObjects: [AVMetadataObject], from connection: AVCaptureConnection) {
        guard !scanned,
              let obj = metadataObjects.first as? AVMetadataMachineReadableCodeObject,
              let value = obj.stringValue else { return }
        scanned = true
        onScan?(value)
    }
}
struct QRScannerView: UIViewControllerRepresentable {
    var onScan: (String) -> Void
    func makeUIViewController(context: Context) -> ScannerViewController {
        let vc = ScannerViewController()
        vc.onScan = onScan
        return vc
    }
    func updateUIViewController(_ vc: ScannerViewController, context: Context) {}
}

@main struct SmartGlassHUDApp: App {
    @StateObject private var hud = HUDCentral()
    @State private var showScanner = false
    var body: some Scene {
        WindowGroup {
            NavigationStack {
                Form {
                    Section("接続状態") {
                        Text(hud.state)
                        if hud.authorized { Label("HUD接続済み",systemImage:"checkmark.circle.fill") }
                        Button("近くのHUDを再検索") { hud.scan() }
                        Button("保存したペアリングを解除") { hud.forgetSavedPairing() }
                    }
                    if !hud.authorized {
                        Section("検出されたデバイス") {
                            ForEach(hud.devices,id:\.identifier) { device in
                                Button(device.name ?? "SG-HUD") { hud.connect(device) }
                            }
                        }
                        Section("初回ペアリング") {
                            Button("QRコードで読み取る") { showScanner = true }
                            TextField("HUDの6桁コード",text:$hud.pairingCode)
                                .keyboardType(.numberPad)
                            Button("コードで接続") { hud.pair() }
                        }
                    }
                    Section("画面の向き（プリズム）") {
                        Text(hud.mirrorOn ? "反転ON：プリズム越しに読む向き" : "反転OFF：画面を直接見る向き")
                        Button("プリズム用に左右反転する") { hud.setMirror(true) }
                        Button("直接表示に戻す") { hud.setMirror(false) }
                        Button("画面を90°回転させる（現在 ROT:\(hud.rotation)）") { hud.cycleRotation() }
                        Text("HUD本体のBOOTボタンを短く押しても切り替えできます。反転ONのときはQRは表示されず、6桁コードをプリズム越しに読んで入力してください。")
                            .font(.footnote)
                    }
                    Section("HUD表示テスト") {
                        TextField("表示テキスト（まず英数字）",text:$hud.message)
                        Button("HUDに送信") { hud.sendText() }.disabled(!hud.authorized)
                    }
                    Section {
                        Text("試作版：独自コード認証は暗号学的な安全性を保証しません。信頼できる環境でのみ使用してください。")
                            .font(.footnote)
                    }
                }
                .navigationTitle("SmartGlass HUD")
                .sheet(isPresented: $showScanner) {
                    QRScannerView { value in
                        showScanner = false
                        hud.applyQRCode(value)
                    }
                    .ignoresSafeArea()
                }
            }
        }
    }
}
