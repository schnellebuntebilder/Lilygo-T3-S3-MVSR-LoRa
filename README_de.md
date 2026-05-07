<!--
 * @Description: Deutsche README
 * @License: GPL 3.0
-->
<h1 align = "center">T3-S3-MVSRBoard</h1>

## **[English](./README.md) | [中文](./README_CN.md) | Deutsch**

Das T3-S3-MVSRBoard ist eine Erweiterungsplatine für das T3-S3_V1.2-Mainboard. Es bietet einen onboard Lautsprecher, ein Mikrofon, einen Vibrationsmotor und eine RTC-Funktion bei sehr geringem Ruhestromverbrauch.

---

## Beispiele

### DMIC_ReadData
Liest Rohpegel-Daten vom digitalen MEMS-Mikrofon (MSM261 / MP34DT05TR) über I²S aus und gibt die Lautstärkewerte fortlaufend auf dem seriellen Monitor aus. Geeignet zum Überprüfen der Mikrofongrundlage.

### DMIC_SD
Nimmt 10 Sekunden Audio über das digitale Mikrofon auf (16-bit, 44,1 kHz, Stereo) und speichert die Aufnahme als WAV-Datei (`/sound.wav`) auf der SD-Karte.

### GFX
Demonstriert die Adafruit GFX-Bibliothek auf dem onboard SSD1306-OLED-Display (128×64). Zeigt Texte, geometrische Formen und eine Schneeflocken-Animation.

### IIC_Scan_2
Scannt den I²C-Bus und gibt eine tabellarische Übersicht aller gefundenen Geräteadressen auf dem seriellen Monitor aus. Nützlich zur Hardware-Diagnose.

### Original_Test
Werksseitiger Gesamttest, der alle Hardwarekomponenten prüft: OLED-Display, LoRa-Funk (SX1262/1276/1278/1280/LR1121), Audio, Mikrofon, Vibrationsmotor und RTC.

### PCF85063
Grundlegendes Beispiel für die RTC PCF85063: Initialisiert die Uhr und gibt die aktuelle Uhrzeit und das Datum fortlaufend auf dem seriellen Monitor aus.

### PCF85063_Scheduled_INT
Konfiguriert die PCF85063-RTC so, dass sie zu einem vordefinierten Zeitpunkt (geplanter Alarm) einen Interrupt auslöst. Der Interrupt-Pin weckt den Mikrocontroller oder löst eine Aktion aus.

### PCF85063_Timer_INT
Nutzt den eingebauten Countdown-Timer der PCF85063-RTC, um in festen Zeitabständen einen Interrupt zu generieren. Praktisch für periodische Aufgaben im Energiesparbetrieb.

### SD
Grundlegender SD-Karten-Test: Initialisiert die Karte über SPI, liest Kartentyp und -größe aus und führt einfache Datei-Lese-/Schreiboperationen durch.

### SD_Music
Spielt eine MP3-Datei (`/music.mp3`) von der SD-Karte über den MAX98357A-Lautsprecher ab. Die Lautstärke lässt sich per Taster umschalten.

### SX126x_PingPong
Einfaches bidirektionales LoRa-Kommunikationsbeispiel für SX1262-Module mit RadioLib. Ein Knoten sendet ein Paket, der andere antwortet – klassisches Ping-Pong-Protokoll.

### SX126x_PingPong_2
Erweitertes Ping-Pong für SX1262: Verwendet die geräteeigene MAC-Adresse zur Absenderidentifikation und zeigt Paketverlust und Laufzeiten auf dem seriellen Monitor an.

### SX126x_Walkie_Talkie
Implementiert ein LoRa-Walkie-Talkie mit SX1262: Sprachaufnahme via DMIC, Codec2-Komprimierung, LoRa-Übertragung und Wiedergabe über den Lautsprecher. Unterstützt private Kanäle per Schlüsselbyte.

### SX127x_PingPong
Analoges Ping-Pong-Beispiel für SX1276/SX1278-Module (Sub-GHz LoRa) mit der RadioLib-Bibliothek.

### SX127x_PingPong_2
Erweitertes Ping-Pong für SX1276/SX1278 mit MAC-basierter Absenderkennung und Live-Anzeige von Übertragungsstatus und RSSI auf dem SSD1306-OLED.

### SX127x_Walkie_Talkie
Walkie-Talkie-Funktion portiert auf SX1276/SX1278: Sprachkommunikation über Sub-GHz-LoRa mit Codec2-Komprimierung, privatem Kanalschlüssel und OLED-Statusanzeige.

### SX128x_PingPong_2
Erweitertes Ping-Pong für SX1280/SX1280PA-Module im 2,4-GHz-Band. Nutzt die ESP32-MAC-Adresse zur Knotenidentifikation und zeigt Übertragungsstatistiken seriell aus.

### Sleep_Wake_Up
Demonstriert den Light-Sleep-Modus des ESP32-S3: Das Board wechselt nach 10 Sekunden Inaktivität automatisch in den Energiesparmodus und wird durch einen Tastendruck (GPIO 0) wieder aufgeweckt.

### Vibration_Motor
Steuert den Vibrationsmotor per PWM (LEDC). Die Intensität wird von 0 auf 255 hoch- und anschließend wieder heruntergefahren, um einen Puls-Effekt zu erzeugen.

### Voice_Codec2_Speaker
Nimmt Sprache vom Mikrofon auf, komprimiert sie mit dem Codec2-Algorithmus und gibt das dekodierte Signal in Echtzeit über den Lautsprecher wieder aus.

### Voice_Speaker
Einfaches Audio-Loopback-Beispiel: Das Mikrofonsignal (44,1 kHz, 16-bit) wird direkt und ohne Komprimierung an den Lautsprecher weitergeleitet.

### Wifi_Music
Verbindet den ESP32-S3 mit einem WLAN und streamt Audio von einer URL über den MAX98357A-Lautsprecher. Demonstriert die Integration der `Audio`-Bibliothek mit Wi-Fi.

### sx126x_tx_continuous_wave
Sendet mit dem SX1262 ein kontinuierliches Trägersignal (Continuous Wave) auf 868 MHz. Nützlich für HF-Messungen, Antennentests und die Überprüfung der Ausgangsleistung.

---

## Debug-Beispiele (`debug/examples`)

### Chip_Scan
Liest die einzigartige 6-Byte-Chip-ID (eFuse MAC) des ESP32 aus und gibt sie formatiert auf dem seriellen Monitor aus. Hilfreich zur schnellen Geräteidentifikation.

### Codec2_Test
Testet den Codec2-Algorithmus isoliert: Enkodiert einen synthetischen 1-kHz-Sinus-Testton und dekodiert ihn wieder, um die korrekte Funktion der Sprachkomprimierungsbibliothek zu verifizieren.

### Lora_Receive_Interrupt
Empfängt LoRa-Pakete interruptgesteuert mit RadioLib. Sobald ein vollständiges Paket eintrifft, wird ein DIO-Interrupt ausgelöst und der Paketinhalt seriell ausgegeben.

### Lora_Transmit_Interrupt
Sendet LoRa-Pakete interruptgesteuert: Ein Paket mit der MAC-Adresse des Absenders wird übertragen; nach Abschluss signalisiert ein Interrupt, dass das nächste Paket gesendet werden kann.

### SX1262_Send_Contract
Einfaches SX1262-Sendebeispiel ohne Interrupt: Sendet zyklisch ein festes Datenpaket und protokolliert Erfolg oder Fehlercode auf dem seriellen Monitor. Geeignet als Kommunikations-Grundgerüst.

### SX126x_Channel_Activity_Detection_Blocking
Führt eine blockierende Kanalaktivitätserkennung (CAD) mit dem SX1262 durch: Das Programm wartet synchron auf das CAD-Ergebnis, bevor es fortfährt. Nicht für zeitkritische Anwendungen empfohlen.

### SX126x_Channel_Activity_Detection_Interrupt
Erkennt laufende LoRa-Übertragungen auf dem aktuellen Kanal per Interrupt-gesteuerter CAD. Effizienter als die blockierende Variante, da der Prozessor zwischenzeitlich andere Aufgaben erledigen kann.

### SX126x_FSK_Modem
Zeigt die Konfiguration und Nutzung des FSK-Modems im SX1262. Dient als Vorlage zum Umstellen anderer Beispiele auf FSK-Betrieb anstelle von LoRa.

### SX126x_Spectrum_Scan
Führt einen Spektrum-Leistungsscan mit dem SX1262 durch und gibt 33 Leistungs-Bins pro Zeile seriell aus. Die Ergebnisse lassen sich mit dem mitgelieferten Python-Skript visualisieren. (Experimentell – erfordert einen binären Patch auf dem SX126x.)

### SX126x_Spectrum_Scan_Frequency
Erweiterung des Spektrum-Scans mit automatischem Frequenz-Sweep über einen definierten Bereich. Gibt pro Frequenzschritt eine Scan-Zeile aus, die anschließend grafisch ausgewertet werden kann. (Experimentell.)

### i2s_loopback
I²S-Loopback-Test auf niedriger Ebene (ESP-IDF `driver/i2s.h`): Mikrofondaten werden direkt mit 8 kHz aufgenommen und über den Lautsprecher (MAX98357A) wiedergegeben – ohne höherstufige Abstraktionsschichten.
