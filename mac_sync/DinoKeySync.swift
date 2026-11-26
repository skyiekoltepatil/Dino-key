import Cocoa
import CoreBluetooth

final class DinoKeySyncApp: NSObject, NSApplicationDelegate, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let dinoSvcUUID = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private let dinoChrUUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")
    private let batSvcUUID  = CBUUID(string: "180F")
    private let batLvlUUID  = CBUUID(string: "2A19")
    private let hidSvcUUID  = CBUUID(string: "1812")

    private var statusItem: NSStatusItem!
    private var statusMenu: NSMenuItem!
    private var central: CBCentralManager!
    private var activePeripheral: CBPeripheral?
    private var cmdChar: CBCharacteristic?
    private var batChar: CBCharacteristic?

    private var triedAll = false, discovering = false, gotReply = false
    private var btBatPct = 100
    private var lastSynced = ""
    private var lastPush = Date.distantPast
    private var scanStopWork: DispatchWorkItem?
    private var logURL: URL?

    func applicationDidFinishLaunching(_ n: Notification) {
        if let dir = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first?.appendingPathComponent("Logs") {
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            logURL = dir.appendingPathComponent("DinoKeySync.log")
        }

        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem.button?.title = "🦖"
        statusItem.button?.toolTip = "DINO KEY Bluetooth Time & Battery Sync"

        let menu = NSMenu()
        statusMenu = NSMenuItem(title: "DINO KEY: Starting Bluetooth...", action: nil, keyEquivalent: "")
        statusMenu.isEnabled = false
        let syncItem = NSMenuItem(title: "Sync Time & Battery Now", action: #selector(syncNow), keyEquivalent: "s")
        syncItem.target = self
        menu.addItem(statusMenu)
        menu.addItem(.separator())
        menu.addItem(syncItem)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Quit DinoKeySync", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        statusItem.menu = menu

        central = CBCentralManager(delegate: self, queue: .main)
        NSWorkspace.shared.notificationCenter.addObserver(self, selector: #selector(onWake), name: NSWorkspace.didWakeNotification, object: nil)
        Timer.scheduledTimer(withTimeInterval: 3.0, repeats: true) { [weak self] _ in self?.checkConnection(scan: false) }
        Timer.scheduledTimer(withTimeInterval: 60.0, repeats: true) { [weak self] _ in
            if self?.activePeripheral?.state == .connected, self?.cmdChar != nil { self?.pushTime("periodic") }
        }
        log("DinoKeySync started")
    }

    @objc private func syncNow() {
        log("Manual Sync clicked")
        if activePeripheral?.state == .connected, cmdChar != nil {
            if let b = batChar { activePeripheral?.readValue(for: b) }
            pushTime("manual")
        } else {
            checkConnection(scan: true)
        }
    }

    @objc private func onWake() {
        log("Mac woke -> probing DINO KEY")
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in self?.checkConnection(scan: true) }
    }

    private func setStatus(_ text: String, badge: String = "🦖") {
        statusMenu.title = text
        statusItem.button?.title = badge
        statusItem.button?.toolTip = text
    }

    private func log(_ msg: String) {
        guard let url = logURL, let data = "\(ISO8601DateFormatter().string(from: Date())) \(msg)\n".data(using: .utf8) else { return }
        if let fh = try? FileHandle(forWritingTo: url) {
            _ = try? fh.seekToEnd(); try? fh.write(contentsOf: data); try? fh.close()
        } else {
            try? data.write(to: url)
        }
    }

    private func checkConnection(scan: Bool) {
        guard central.state == .poweredOn else {
            let msg = central.state == .unauthorized ? "Allow Bluetooth in System Settings -> Privacy"
                    : (central.state == .poweredOff ? "Turn On Mac Bluetooth" : "Waiting for Bluetooth...")
            setStatus(msg, badge: (central.state == .unauthorized || central.state == .poweredOff) ? "🦖!" : "🦖")
            return
        }
        if activePeripheral?.state == .connected, cmdChar != nil { return }

        let list = central.retrieveConnectedPeripherals(withServices: [dinoSvcUUID, batSvcUUID, hidSvcUUID])
        if let dino = list.first(where: { ($0.name ?? "").localizedCaseInsensitiveContains("DINO") })
                   ?? list.first(where: { $0.services?.contains(where: { $0.uuid == dinoSvcUUID }) == true }) {
            if central.isScanning { central.stopScan() }
            if dino.state == .connecting { setStatus("DINO KEY: Connecting Bluetooth sync..."); return }
            if activePeripheral?.identifier != dino.identifier || dino.state != .connected {
                log("Found system-connected: \(dino.name ?? dino.identifier.uuidString)")
                activePeripheral = dino; dino.delegate = self; triedAll = false; discovering = false
                if dino.state == .connected {
                    setStatus("DINO KEY: Discovering sync service...")
                    discovering = true
                    dino.discoverServices([dinoSvcUUID, batSvcUUID])
                } else {
                    setStatus("DINO KEY: Attaching Bluetooth sync...")
                    central.connect(dino, options: nil)
                }
            } else if cmdChar == nil && dino.state == .connected && !discovering {
                discovering = true
                dino.discoverServices([dinoSvcUUID, batSvcUUID])
            }
            return
        }

        if scan && !central.isScanning {
            setStatus("Scanning for DINO KEY Bluetooth...")
            central.scanForPeripherals(withServices: [dinoSvcUUID, hidSvcUUID], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
            scanStopWork?.cancel()
            let w = DispatchWorkItem { [weak self] in
                guard let self = self, self.central.isScanning else { return }
                self.central.stopScan()
                if self.activePeripheral?.state != .connected { self.setStatus("DINO KEY: Waiting for Bluetooth pair...") }
            }
            scanStopWork = w
            DispatchQueue.main.asyncAfter(deadline: .now() + 6.0, execute: w)
        } else if activePeripheral?.state != .connected {
            setStatus("DINO KEY: Waiting for Bluetooth pair...")
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log("Central state = \(central.state.rawValue)")
        checkConnection(scan: true)
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? ""
        guard name.localizedCaseInsensitiveContains("DINO") || p.identifier == activePeripheral?.identifier else { return }
        central.stopScan(); scanStopWork?.cancel()
        activePeripheral = p; p.delegate = self; triedAll = false
        setStatus("DINO KEY: Connecting...")
        central.connect(p, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        log("didConnect: \(p.name ?? p.identifier.uuidString)")
        activePeripheral = p; p.delegate = self; triedAll = false; discovering = true; cmdChar = nil; batChar = nil
        setStatus("DINO KEY: Discovering services...")
        p.discoverServices([dinoSvcUUID, batSvcUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        log("didFailToConnect: \(error?.localizedDescription ?? "unknown")")
        discovering = false
        setStatus("DINO KEY: Connect retry...")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("didDisconnect: \(error?.localizedDescription ?? "clean")")
        if p.identifier == activePeripheral?.identifier { cmdChar = nil; batChar = nil; discovering = false }
        setStatus("DINO KEY Disconnected — Waiting...")
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        var foundDino = false
        for s in p.services ?? [] {
            if s.uuid == dinoSvcUUID { foundDino = true; p.discoverCharacteristics([dinoChrUUID], for: s) }
            else if s.uuid == batSvcUUID { p.discoverCharacteristics([batLvlUUID], for: s) }
        }
        if !foundDino && !triedAll {
            triedAll = true; p.discoverServices(nil)
        } else if !foundDino {
            discovering = false; setStatus("DINO KEY: Sync service missing")
        }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        discovering = false
        if service.uuid == batSvcUUID, let b = service.characteristics?.first(where: { $0.uuid == batLvlUUID }) {
            batChar = b
            if b.properties.contains(.notify) { p.setNotifyValue(true, for: b) }
            p.readValue(for: b)
        }
        guard let ch = service.characteristics?.first(where: { $0.uuid == dinoChrUUID }) else { return }
        cmdChar = ch
        if ch.properties.contains(.notify) { p.setNotifyValue(true, for: ch) }
        pushTime("auto-connect")
    }

    func peripheral(_ p: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        log("didModifyServices (\(invalidatedServices.count)) -> re-discovering")
        cmdChar = nil; batChar = nil; discovering = true; triedAll = false
        p.discoverServices([dinoSvcUUID, batSvcUUID])
    }

    private func pushTime(_ reason: String) {
        guard let p = activePeripheral, p.state == .connected, let ch = cmdChar else { return }
        if reason == "auto-connect", Date().timeIntervalSince(lastPush) < 2.0 { return }
        lastPush = Date(); gotReply = false
        let cmd = "T:\(Int(Date().timeIntervalSince1970)):\(TimeZone.current.secondsFromGMT() / 60)"
        guard let data = cmd.data(using: .utf8) else { return }
        let wt: CBCharacteristicWriteType = ch.properties.contains(.write) ? .withResponse : .withoutResponse
        p.writeValue(data, for: ch, type: wt)
        log("Wrote \(cmd) (\(reason))")
        setStatus("DINO KEY: Syncing time & BT battery...")
        if wt == .withoutResponse {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
                if self?.gotReply == false { p.readValue(for: ch) }
            }
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if let err = error {
            log("didWrite err: \(err.localizedDescription) -> refreshing GATT")
            setStatus("DINO KEY: Refreshing GATT cache...")
            cmdChar = nil; batChar = nil; discovering = false; triedAll = false
            central.cancelPeripheralConnection(p)
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in self?.checkConnection(scan: true) }
            return
        }
        let f = DateFormatter(); f.dateFormat = "hh:mm a"
        lastSynced = "Synced: \(f.string(from: Date()))"
        log("didWrite OK -> \(lastSynced) • BT Bat \(btBatPct)%")
        if !gotReply { setStatus("\(lastSynced) • BT Bat \(btBatPct)% ✓", badge: "🦖✓") }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
            if self?.gotReply == false, p.state == .connected { p.readValue(for: ch) }
        }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard error == nil, let data = ch.value else { return }
        if ch.uuid == batLvlUUID, let first = data.first {
            btBatPct = Int(min(first, 100))
            log("BLE 0x2A19 BT Battery = \(btBatPct)%")
            let base = lastSynced.isEmpty ? "DINO KEY: Linked" : lastSynced
            setStatus("\(base) • BT Bat \(btBatPct)% ✓", badge: "🦖✓")
            return
        }
        gotReply = true
        if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let tm = json["time"] as? String, let dt = json["date"] as? String, let src = json["sync"] as? String {
            if let b = json["bat"] as? Int, b >= 0 { btBatPct = b }
            lastSynced = "Synced: \(dt) \(tm) [\(src)]"
            let msg = "\(lastSynced) • BT Bat \(btBatPct)%"
            log(msg)
            setStatus("\(msg) ✓", badge: "🦖✓")
        }
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let delegate = DinoKeySyncApp()
app.delegate = delegate
app.run()
