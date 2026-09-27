# CAM_CE / CAM_RESET (Rev.5): Analyse und Plan

**Umsetzung:** B0, AP1 und AP2 sind umgesetzt, Stand und Abweichungen stehen in Abschnitt 17.

Stand 3 vom 2026-09-27. Stand 3 zieht die Isolation durch (neuer Abschnitt 16) und arbeitet deine Antworten auf F13–F18 ein; alles Übrige ist Stand 2. Basis ist `main` @ `35e815a`. Der Vorab-Fix (AP0) liegt als `9156d3e` auf dem Zweig `fix/esp-timer-units`. Für CAM_CE und CAM_RESET gibt es bis zur Freigabe keinen Code; alle Schnipsel sind Skizzen.

Quellen:
- Code im Repo.
- ESP-IDF v5.4.2 (lokale Installation, Pfade relativ zu `esp-idf/`).
- OV3660-Datenblatt v1.3 im Repo-Root (`OV3660_CSP3_DS_1.3_sida.pdf`); Seitenangaben sind PDF-Seiten.
- Deine Netzlisten- und BOM-Angaben aus der Antwort auf Stand 1. Sie gelten als fest.

Kennzeichnung: ⚠ = Widerspruch oder Abweichung von deinem Vorschlag, *Vermutung* = nicht belegt.

---

## 0. Änderungen

### Stand 3 gegenüber Stand 2

| Thema | Stand 2 | Stand 3 | Grund |
|---|---|---|---|
| Isolation | nicht betrachtet | Kategorie A über Feature-Symbole, Kategorie B getrennt, Verfahren für Bitgleichheit (Abschnitt 16) | dein Nachtrag |
| SCCB-Abbau | ⚠ Änderung in `SCCB_Deinit` vorgesehen | **zurückgenommen**: kein erreichbarer Fehler, keine Änderung am Treiber und auch keine außerhalb (Abschnitt 7) | IDF-Statuslogik genau nachgelesen |
| I2C-Bus-Clear vor dem Reinit | vorgeschlagen | gestrichen: das IDF räumt den Bus vor der nächsten Transaktion selbst frei (`i2c_master.c:616-618`) | – |
| `boot_failure` | Frage F13 | eingebaut, einmalig, innerhalb `setupCamera()` | F13 |
| ESP-Neustart als letzte Stufe | Frage F15 | Kconfig-Option, Default aus, getrennt gezählt und geloggt | F15 |
| Zähler | Frage F18 | kein NVS; Zusammenfassung nur auf Kommando als eine WARN-Zeile im persistenten Log | F18 |
| DMM-Blick 1V5_Cx | Frage F16 | ein Kommando: `camera_power_cycle {"off_ms": 5000}`; `off_ms` bis 30 s | F16 |
| Sense-Pins | Frage F17 | festgelegt: D0 = GPIO15, D6 = GPIO11; Netze haben genau zwei Knoten | F17 |
| `get_camera_status` | überall geplant | nur mit Feature; die angebotene Ausnahme beanspruche ich nicht (16.2 d) | Bitgleichheit |
| 32-kHz-Quarz | – | geprüft: `CONFIG_RTC_CLK_SRC_INT_RC=y`, kein Board setzt einen externen Quarz, kein Code nutzt ihn. Zusätzlich Build-Schutz (16.2 c) | dein Hinweis |

### Stand 2 gegenüber Stand 1

| Thema | Stand 1 | Stand 2 | Grund |
|---|---|---|---|
| Zweck | Recovery, später Stromsparen | ESD-Tests. Power-Cycle ist die Kernstufe, der automatische Auslöser gehört in den ersten Wurf | deine Antwort, Abschnitt 4 |
| Nachweis „stromlos" | Scope und DMM (M-1…M-4) | Softwarenachweis für 2V8_Cx in jedem Zyklus (Abschnitt 5). 1V5_Cx ist per Software nicht beobachtbar | deine Antwort, Abschnitt 5 |
| Begründung Pin-Parken | pauschal mit DS Tab. 8-1 | Tab. 8-1 trägt nur für XCLK. Bei SCCB und RESET ist der Grund die Rückspeisung. Ergebnis unverändert | Tab. 1-3 (S. 19) genauer gelesen |
| Fähigkeitsprobe | digital mit Pull-down | ADC; Pull-down nur am RESET-Knoten, nie an CE | dein V_IH-Hinweis; TP132-EN-Schwelle unbekannt |
| Bench-Kommando | `camera_power {"state":"off","hold_s":N}` | ein synchroner Zyklus `camera_power_cycle`, nur solange kein Streaming-Backend läuft | ein reines Zustandsflag schützt im UVC-Modus nicht (Abschnitt 9) |
| Budget | 10 pro Boot, Cooldown 5 s | ⚠ Cooldown 5 s, Sperre nach 3 Fehlschlägen in Folge, Ratenfenster 10 pro 10 min, kein hartes Boot-Limit | eine ESD-Prüfung kann mehr als 10 Erholungen pro Boot brauchen |
| Aus-Zeit | Platzhalter 100 ms | mindestens 500 ms, danach aus, bis der Rail-Check „zusammengebrochen" meldet, höchstens 3 s | großzügig und selbstprüfend statt gemessen |
| t5 | „nicht steuerbar" | strukturell null: ein LDO speist AVDD und DOVDD | deine Netzlistenangabe |
| SCCB-Abbau | nicht betrachtet | kann bei gestörtem I2C jeden späteren Init blockieren; Absicherung vorgesehen | I2C-Unabhängigkeit durchgeprüft (Abschnitt 7) |
| Teil A | offen | abgeschlossen, Bericht in Anhang A | deine Antwort, Abschnitt 1 |

**Teil A:** Beim Durchgang für Stand 1 habe ich im Lüfterpfad keinen echten Defekt gefunden, nur unerfüllte Vorgaben. Nichts Neues zu melden.

---

## 1. Vorab-Commit (deine Antwort, Abschnitt 7)

| | Befund | Stand |
|---|---|---|
| (a) `OpenIrisTasks::ScheduleRestart` | bestätigt | behoben in `9156d3e`: `esp_timer_start_once(timer, ms * 1000)` |
| (b) `startStreamingCommand` | bestätigt | behoben in `9156d3e`: 150 ms statt 150 µs |
| (c) `CONFIG_LED_DEBUG_GPIO` = 8 | ⚠ **anders eingeschätzt, nicht umgesetzt** | wandert als erster Punkt in AP1 |

Warum (c) nicht jetzt:
- **Heute ist es kein Defekt.** Auf allen FFVR-Konfigurationen ist `LED_DEBUG_ENABLE` aus. Nichts treibt GPIO8, und Code für CAM_RESET gibt es noch nicht.
- **Den Default zu ändern wäre eine Regression.** `CONFIG_LED_DEBUG_GPIO=8` steht explizit in `boards/sdkconfig.base_defaults:600`. `esp_eye`, `wrooms3`, `wrooms3QIO` und `wrover` setzen keinen eigenen Wert und erben ihn; deren Status-LED würde wandern. Nur den Kconfig-Default zu ändern, wirkte gar nicht, weil `base_defaults` ihn überschreibt.
- **Der `static_assert` braucht das Symbol für den Reset-Pin.** Das entsteht erst in AP1.

Abnahme des Commits:
- Build `facefocusvr_eye_l` (aktuelle `sdkconfig`): erfolgreich, ohne neue Warnungen. Die zwei Warnungen in `OpenIrisTasks.cpp:11` (fehlende Initialisierer in `esp_timer_create_args_t`) gab es schon vorher.
- Auf Hardware, von dir:
  1. `restart_device`: Die Antwort `success` kommt an. Das Gerät verschwindet etwa 2 s später und meldet sich neu an. Im Setup-Tool zeigt „Restart device" ✅ statt ❌.
  2. `start_streaming` im Setup-Modus: Die Antwort kommt an, danach startet UVC. ⚠ Korrektur: `start_streaming` startet den *gespeicherten* Modus (`launch_streaming`). Steht er auf `setup`, bleibt das Gerät im Setup-Modus; das ist bestehendes Verhalten. Richtig geprüft wird mit `switch_mode uvc` (ohne Neustart), dann `start_streaming`.
  3. Bestehende Tests mit `restart_device`: Der Neustart kommt jetzt 2 s später. `SWITCH_MODE_REBOOT_TIME` in `tests/.env` muss Neustart plus Boot abdecken (siehe unten).

**Kategorie B: was sich für die anderen Boards ändert.** AP0 wirkt auf allen Konfigurationen gleich:
- `restart_device`: Erst geht die Antwort raus, der Neustart folgt 2 s später statt nach etwa 2 ms. In diesen 2 s läuft das Gerät normal weiter; Kommandos, die in dem Fenster ankommen, werden noch ausgeführt.
- `start_streaming`: Das Streaming wird 150 ms nach dem Kommando aktiviert statt nach etwa 150 µs.
- Sonst nichts: keine Schnittstelle geändert, kein anderes Timing.

**`SWITCH_MODE_REBOOT_TIME`, Mindestwert:** der bisher funktionierende Wert **plus 2 s**. Die Tests schlafen nach `restart_device` genau einmal (z. B. `conftest.py:197-203`, `test_commands.py:82-84`). War der Wert bisher knapp, reicht er jetzt nicht mehr.
- Aus dem Code allein summieren sich die festen Wartezeiten zwischen `restart_device` und der neuen CDC-Schnittstelle im UVC-Modus auf etwa 4,1 s:
  - 2 s Verzögerung (AP0),
  - 0,3 s Pause vor der USB-Übergabe (`openiris_main.cpp:202`),
  - bis 1,5 s MAC-abhängiger Versatz (`openiris_main.cpp:211`),
  - ≥ 0,22 s feste Pausen im Kamera-Init,
  - auf eye_R etwa 35 ms Lüftererkennung.
- Dazu kommen Bootloader, Registerverkehr zur Kamera und die Enumeration am Host. Die habe ich nicht gemessen.
- **Empfehlung: nicht unter 8 s.**

**Was sonst auf die kurze Verzögerung gebaut haben könnte:** geprüft, nichts gefunden.
- Tests: Alle schlafen nach `restart_device`, sind also mit dem obigen Wert abgedeckt. `test_reboot_command` vergleicht die Portliste vor und nach dem Schlaf (`test_commands.py:100-110`); ist der Schlaf kürzer als 2 s, hat das Gerät noch gar nicht neu gestartet.
- `tools/setup_openiris.py:479-487` meldet nur und verbindet nicht neu.
- Firmware: Einziger Aufrufer von `ScheduleRestart` ist `restartDeviceCommand` (`device_commands.cpp:130`); `ProjectConfig.cpp:49` ist auskommentiert.
- `start_streaming`: Weder das Setup-Tool noch die Tests verlassen sich auf einen sofortigen Abbau.
- Nicht prüfbar: Host-Anwendungen außerhalb des Repos, die `restart_device` senden.

---

## 2. Feste Hardware-Fakten

GPIO-Belegung auf Rev.5 laut deiner Netzliste:

| ESP | Rolle | Pin 12 (GPIO7) | Pin 13 (GPIO8) |
|---|---|---|---|
| U28 | eye_L | CAM_CE_C1 | IO08/CAM_RESET |
| U31 | face | CAM_CE_C2 | IO08/CAM_RESETB |
| U34 | eye_R | CAM_CE_C3 | IO08/CAM_RESETC |

- **Rev.4/4.5:** Die Pads 12 und 13 hängen bei allen drei ESPs an keinem Netz.
- **Versorgung:** DOVDD (Pin 11) hängt direkt an 2V8_Cx, AVDD (Pin 4) über L17 (MPZ1608S121ATDH5) an derselben Netzgruppe. Es gibt einen 2,8-V-LDO pro Kamera.
- **LDOs:** TP132LC28D4 (C48586710) und TP132LC15D4 (C48586704), TECH PUBLIC. Es gibt kein Datenblatt. EN-Schwelle und Ausgangsentladung sind unbekannt.
- **Kein WLAN** (LNA_IN unbeschaltet): keine REST-Route, und ADC2 ist frei nutzbar.

Aus DS Tab. 1-3 (S. 19): Welche Kamera-Pads haben einen Pfad in die DOVDD-Schiene?

| Pad | Struktur laut Ersatzschaltbild | Pfad nach DOVDD |
|---|---|---|
| XVCLK | Eingang, Klemme nur nach DOGND | **nein** |
| SIOD, SIOC | Open-Drain bzw. Eingang, Klemme nur nach DOGND | **nein** |
| RESETB | Serienwiderstand, interner Pull-up-PMOS von DOVDD (Gate an DOGND) | **ja**, über den Pull-up |
| D9…D0, VSYNC, STROBE, FSIN | Push-Pull-Ausgang mit PMOS an DOVDD | **ja**, über die Drain-Wannen-Diode des Ausgangs-PMOS |
| PCLK, HREF | in Tab. 1-3 nicht aufgeführt | unbekannt |

---

## 3. Ist-Analyse Kamerapfad (aus Stand 1, weiterhin gültig)

**Start:**
- `setupCamera()` läuft einmalig in `app_main` (`main/openiris_main.cpp:328`).
- `setupCameraPinout()` baut `config` bei jedem Aufruf neu mit 23 MHz auf (`CameraManager.cpp:142-176`).
- `esp_camera_init` → danach Live-Switch auf 20 MHz (`CameraManager.cpp:332-351`) → `setupCameraSensor()`.
- `pin_reset` und `pin_pwdn` sind −1.
- Im UVC-Modus startet UVC nur, wenn die Kamera hochkam (`openiris_main.cpp:336-347`).

**Fehlschlag:**
- Kein Wiederholversuch. Der Init räumt sich per `esp_camera_deinit` selbst auf.
- Zur Laufzeit gibt es keine Erkennung. `esp_camera_fb_get` blockiert 4 s, auf dem S3 nach einem GDMA-Reset nochmals 4 s (`esp_camera.c:365`, `cam_hal.c:482-491`).
- UVC loggt dann `ESP_LOGE` und versucht es alle 30 ms erneut (`usb_device_uvc.c:190-199`).
- Der einzige Reset heute ist der SCCB-Softreset `0x3008 = 0x82` im Init (`ov3660.c:224-245`).

**Pins nach `esp_camera_deinit()`:**

| Gruppe | GPIO | Zustand nach Deinit | Beleg |
|---|---|---|---|
| XCLK | 10 | Ausgang, Matrix weiter auf `CAM_CLK`. Der LCD_CAM-Takt wird nicht abgeschaltet (*vermutlich läuft XCLK weiter*) | `ll_cam.c:177-197`, `esp_camera.c:93-95` |
| SCCB | SDA 40, SCL 39 | Ausgang aus, **interne Pull-ups an** | IDF `esp_driver_i2c/i2c_common.c:417-427` |
| DVP | D0…D7 = 15, 17, 18, 16, 14, 12, 11, 48; PCLK 13; VSYNC 38; HREF 47 | Eingang floating | `ll_cam.c:370-393` |

Der Treiber reserviert keine Pins. Zwischen Deinit und dem nächsten Init sind sie frei, und der nächste Init stellt alles wieder her, was er braucht. Die Regel lautet deshalb: **Parken nur im Zustand Aus, kein Entparken nötig.**

**Konsumenten:**
- UVC holt Frames direkt und überträgt zero-copy aus dem Kamerapuffer (`UVCStream.cpp:161`, `usb_device_uvc.c:207-239`).
- `CameraManager::camera_sensor` zeigt nach einem Deinit ins Leere (`CameraManager.hpp:24`).
- Ein Deinit bei laufendem Konsumenten wäre deshalb ein Use-after-free. Heute ruft niemand zur Laufzeit `esp_camera_deinit` auf, die Gefahr ist also latent.

---

## 4. Datenblatt-Abgleich (aktualisiert)

Es gilt DS §2.4.2 „power up with external DVDD source" (S. 27).

| Anforderung | Datenblatt | Plan |
|---|---|---|
| Einschaltreihenfolge DOVDD → AVDD → DVDD, Anstieg < 5 ms | §2.4.2, Abb. 2-6 | nicht durchsetzbar. Elektrisch wie der heutige Kaltstart auf allen Revisionen. Der Anstieg von 2V8_Cx wird per Software mitgeschrieben (Abschnitt 5) |
| t2 ≥ 5 ms, t3 ≥ 1 ms bis RESETB high | Abb. 2-6 | RESET bleibt ab dem Abschalten low. Freigabe **20 ms** nach der CE-Freigabe (5 + 5 + 1 ms, etwa doppelt) |
| t4 ≥ 20 ms bis zum ersten SCCB-Zugriff | Abb. 2-6 | **25 ms** (F4 bestätigt) |
| XVCLK ≥ 2 ms vor dem ersten Registerzugriff | §2.4.2 Pkt. 8 | erfüllt der Treiber selbst (`esp_camera.c:213`) |
| t6: RESETB low vor DVDD aus; t7: XVCLK aus vor DVDD aus | Abb. 2-6 | erfüllt |
| **t5: AVDD aus vor DOVDD aus** | Abb. 2-6 | **strukturell null.** AVDD und DOVDD kommen aus demselben LDO und gehen immer gleichzeitig. So ist es seit Rev.2 |
| Alle I/O ≤ VDD-IO + 1 V | Tab. 8-1, S. 153 | siehe die korrigierte Begründung unten |
| Hardware-Reset setzt **alle** Register zurück, ca. 2 ms Settling | §2.5, S. 28 | wichtig für Abschnitt 5 |
| Softreset `0x3008[7]`, Software-Power-Down `0x3008[6]` | S. 36 | Stufe „reinit" bzw. Testhaken |

**⚠ Korrektur zur Begründung des Pin-Parkens.** In Stand 1 hatte ich das Parken pauschal mit Tab. 8-1 begründet. Mit Tab. 1-3 stimmt das nur für XCLK. Das Ergebnis, dass Parken Pflicht ist, bleibt:

| Leitung | Pfad in die abgeschaltete Kamera | Grund fürs Parken |
|---|---|---|
| XCLK (GPIO10) | kein Pfad nach DOVDD, das Pad sähe volle 3,3 V | **Tab. 8-1**: 3,3 V > VDD-IO + 1 V = 1 V |
| SIOD/SIOC (40/39) | in der Kamera keiner, aber vom ESP-Pull-up (≈ 45 kΩ) über den Board-Pull-up 4,7 kΩ nach 2V8_Cx | **Rückspeisung** ≈ 66 µA je Leitung. Am Pad liegen ≈ 0,3 V, also innerhalb Tab. 8-1 |
| RESETB (GPIO8) | interner Pull-up-PMOS nach DOVDD | **Rückspeisung** ≈ 260 µA (deine Rechnung). Das Pad klemmt unterhalb VDD-IO + 1 V |
| D0…D7, VSYNC | Ausgangs-PMOS nach DOVDD | nur, wenn die ESP-Seite hochzieht. Die Pull-downs verhindern das |

---

## 5. Softwarenachweis: war die Kamera stromlos?

### Kurzantwort

- **2V8_Cx (DOVDD und AVDD): ja, tragfähig, in jedem einzelnen Power-Cycle.** Der Nachweis läuft über einen DVP-Datenpin und ADC2. Er liefert eine obere Schranke für die Schienenspannung am Ende der Aus-Zeit und kann nie fälschlich „stromlos" melden.
- **1V5_Cx (DVDD): per Software nicht beobachtbar.** Kein ESP-Pin hat einen Pfad in die DVDD-Domäne. Für den ESD-Zweck schließt ein Argument die Lücke weitgehend (siehe „Was offen bleibt"). Belegen lässt es sich nur mit einem einmaligen DMM-Blick (F16).
- **Der 260-µA-Pfad am Reset-Pin** ist durch das gehaltene RESET geschlossen. Wäre er trotzdem offen, würde der Check „nicht zusammengebrochen" melden.

### Verworfene Wege

- **Register mit POR-Wert:** RESETB setzt alle Register zurück (DS §2.5). Solange RESET während der Aus-Zeit gehalten wird, lässt sich „stromlos" nicht von „nur im Reset" unterscheiden. Und gehalten werden muss es, sonst ist der 260-µA-Pfad offen.

  Ein Diagnosezyklus ohne gehaltenes RESET wäre möglich: Marker schreiben, CE-Zyklus, Marker mit eigenem I2C-Zugriff vor dem Treiber-Init lesen. Er öffnet aber absichtlich den Rückspeisepfad, braucht I2C neben dem Treiber und ist nur einseitig aussagekräftig. Ich empfehle ihn nicht.
- **XCLK** (GPIO10 ist ADC1_CH9): XVCLK hat keinen Pfad nach DOVDD (Tab. 1-3). Die Messung wäre blind: fail-safe, aber nutzlos.
- **SCCB-Leitungen:** Sie hängen über 4,7 kΩ direkt an 2V8_Cx und wären ideal. GPIO39/40 haben aber keinen ADC.
  - Digital gelesen beweist eine 1 nur „nicht zusammengebrochen" (V > V_IL,max = 0,25 × 3,3 V).
  - Eine 0 beweist nicht „stromlos".
  - Sie bleiben als störungsfreie Gegenprobe im Ablauf.
- **Reset-Knoten** (GPIO8 ist ADC1_CH7): funktioniert über den Pull-up-PMOS. Dazu müsste RESET am Ende der Aus-Zeit kurz losgelassen werden, also genau der 260-µA-Pfad geöffnet. Außerdem klemmt der Knoten höher, die Schranke ist schwächer. Zweite Wahl.

### Gewählt: DVP-Datenpad mit ESP-Pull-up über ADC2

```
ESP32-S3                                        OV3660
GPIO15 = D0 (ADC2_CH4) ─────────────────────── D0-Pad ──┬── Ausgangs-PMOS ── DOVDD = 2V8_Cx
  interner Pull-up, nur während der Messung              │     Wannendiode Pad → DOVDD
                                                         └── Klemme ── DOGND
```

**Prinzip:** Mit eingeschaltetem Pull-up fließt nur dann Strom vom Pad in die Schiene, wenn V_Pad > V(2V8_Cx) ist. Das Pad liegt also entweder nahe 3,3 V (Schiene oben, Diode sperrt) oder es wird auf Schiene plus Durchlassspannung geklemmt. Liegt der Messwert deutlich unter der Positivkontrolle desselben Zyklus, ist belegt: **V(2V8_Cx) < Messwert.**

**Positivkontrolle:** Vor CE low ist die Kamera versorgt und im Reset, ihre Ausgänge sind hochohmig (DS Tab. 1-2, S. 17). Mit Pull-up zeigt der Pin dann den „Schiene oben"-Wert dieses Boards. Erwartet sind knapp über 3 V; das ist *nicht gemessen* und wird deshalb in jedem Zyklus neu bestimmt.

**Zwei Pins:** D0 (GPIO15, ADC2_CH4) und D6 (GPIO11, ADC2_CH0), beide aus der Board-Konfiguration (`Y2`, `Y8`). Beide müssen dasselbe melden.

**Klassifikation.** Die Schwellen sind Platzhalter und werden wie beim Lüfter aus den ersten Läufen mit großem Abstand festgelegt:

| Ergebnis | Bedingung (Vorschlag) | Aussage |
|---|---|---|
| `collapsed` | Kontrolle ≥ 2,6 V und Endwert ≤ 1,0 V an beiden Pins | V(2V8_Cx) < Endwert. Typisch ist das die Durchlassspannung, einige 100 mV |
| `partial` | dazwischen | Endwert wird als Obergrenze gemeldet |
| `not_collapsed` | Endwert ≥ Kontrolle − 0,3 V | Schiene nicht gefallen. Auf Rev.4/4.5 ist genau das die erwartete Antwort |
| `inconclusive` | Kontrolle < 2,6 V oder Pins uneinig | Verfahren auf diesem Board bzw. Pin nicht anwendbar |

**Warum fail-safe:** `collapsed` verlangt, dass der Pad-Knoten heruntergezogen wird. Bei eingeschaltetem Pull-up kann das nur ein Stromfluss in eine niedrigere DOVDD-Schiene. Eine fehlende oder andere Pad-Struktur kann den Messwert nur hoch halten, also `not_collapsed` oder `inconclusive` ergeben, nie ein falsches `collapsed`. Ein unbekannter Pfad nach Masse auf dem Netz würde schon die Positivkontrolle senken, das ergibt `inconclusive`.

**Störung durch die Messung:** etwa 60 µA für ≈ 0,2 ms pro Messung, also wenige nC. Vernachlässigbar.

**Nebennutzen:**
- **Abfallkurve** (im Bench-Modus alle 20 ms ein Punkt): Zeit bis 2V8_Cx < 1 V. Daraus wird die Aus-Zeit bemessen. Die Kurve zeigt auch, ob der TP132 aktiv entlädt (schnell) oder die Schiene nur über Leckströme sinkt (langsam).
- **Anstiegskurve** nach der CE-Freigabe (alle 0,5 ms): Anstiegszeit von 2V8_Cx gegen die DS-Forderung < 5 ms.
- **Fähigkeit nach Wirkung:** Ein erzwungener Bench-Zyklus auf Rev.4/4.5 muss `not_collapsed` liefern. Das bestätigt F2 in Hardware.
- **Adaptive Aus-Zeit:** Die Recovery hält CE mindestens 500 ms low und danach so lange, bis der Check `collapsed` meldet, höchstens 3 s.

**Was offen bleibt:**
- **1V5_Cx.** Das Argument für den ESD-Zweck:
  - (a) Logische Störungen: Beim Wiedereinschalten ist RESETB low, während DOVDD ansteigt. Der Kern bekommt also einen Hardware-Reset, egal was von DVDD übrig ist.
  - (b) Latch-up in der DVDD-Domäne: Ohne Versorgung entlädt der Latch-Pfad seine eigene Schiene. Sein Haltestrom liegt weit über allem, was ohne LDO nachfließen kann, und die Rückspeisepfade sind geparkt.
  - (c) *Vermutung:* gleiche LDO-Familie (TP132LC15 und LC28). Zeigt 2V8 eine aktive Entladung, hat LC15 sie wahrscheinlich auch.

  Für den Normalbetrieb hast du das Argument akzeptiert (F16). Den einmaligen DMM-Blick gibt es trotzdem, und zwar mit **einem Kommando** im Setup-Modus: `camera_power_cycle {"off_ms": 5000}`. Während der 5 s liest du 1V5_Cx ab; die Antwort mit dem vollständigen Report kommt nach dem Wiederanlauf. `off_ms` ist dafür bis 30 000 zugelassen.
- **Die absolute Spannung.** Es gibt nur eine Obergrenze: Die Durchlassspannung ist unbekannt, dazu kommt die ADC-Toleranz. Weil Kontrolle und Messung am selben Pin im selben Zyklus liegen, kürzen sich Kalibrierfehler weitgehend heraus.
- **Die Netze D0/D6** sind geklärt (F17): Laut Rev.5-Netzliste haben sie bei allen drei ESPs genau zwei Knoten, den ESP-Pin und den Kamerapin (U28/U31/U34-21 ↔ P1/P2/P3-19 für D0, U28/U31/U34-16 ↔ P1/P2/P3-14 für D6). Auch alle übrigen DVP-Netze inklusive PCLK, HREF und VSYNC sind reine Zwei-Knoten-Netze.

**Umsetzungshinweise:**
- ADC2 per `adc_oneshot` direkt in der Kamerakomponente. `AdcSampler` kann nur ADC1 (`AdcSampler_esp32s3.cpp:24-37`) und dient dem abgeschlossenen Lüfterpfad; er bleibt unberührt.
- Reihenfolge: zuerst Kanal konfigurieren (das schaltet die Pulls ab, IDF `adc_oneshot.c:322-326`), dann den Pull-up im RTC-Registersatz setzen (`rtc_gpio_pullup_en`). Der S3 hat getrennte Pull-Register für digital und RTC (`SOC_GPIO_SUPPORT_RTC_INDEPENDENT = 1`); das bestätige ich in AP2 am Chip.
- Danach die Pads **per `gpio_config` zurück ins Digitale**, bevor `esp_camera_init` läuft. Sonst konfiguriert `ll_cam_set_pin` die Matrix, das Pad bleibt aber im RTC-Mux, und der DVP-Eingang ist tot. Das ist dieselbe Lehre wie `releasePin()` in `FanRevision.cpp:68-89`.
- ADC2 kollidiert mit WLAN. Die Isolationsgrenze ist aber das Feature-Symbol `CAMERA_RAIL_SENSE`. Dass es zusätzlich `!GENERAL_ENABLE_WIRELESS` voraussetzt, ist nur eine Absicherung (Abschnitt 16).
- GPIO15 und GPIO16 (D0 und D3) sind die 32-kHz-Quarzpins des S3. Geprüft: Alle Konfigurationen nutzen den internen RC-Oszillator (`CONFIG_RTC_CLK_SRC_INT_RC=y`, kein Board setzt etwas anderes), und kein Code greift auf den Quarz zu. Damit das so bleibt, gibt es einen Build-Schutz (16.2 c).

---

## 6. Abstraktion (aktualisiert)

```
CameraRecovery   policy     command, auto trigger, budget, counters, report     (new)
recovery worker  task       runs every recovery, one at a time                  (new)
CameraManager    lifecycle  state, frame gate, stop/start, parking, rail check  (extended)
CamLines         mechanism  CE and RESET as open drain, capability probe        (new)
esp32-camera     driver     unchanged
```

**Kconfig:** Struktur, Defaults und Ort stehen in Abschnitt 16.1. Die Werte für FFVR:
- CE 7 und RESET 8 (F1),
- Sense-Pins 15 und 11 (F17),
- Recovery und Automatik an, ESP-Neustart aus.

`CONFIG_RESET_GPIO_NUM` bleibt −1.

**Build-Schutz, zugleich Punkt (c):** Die `static_assert`s stehen in 16.2 (c). Ohne Feature werden sie nie ausgewertet.

**Open Drain** (unverändert aus Stand 1): Losgelassen heißt Eingang ohne Pulls. Ziehen heißt: erst Pegel 0, dann `GPIO_MODE_INPUT_OUTPUT_OD`. Nie Push-Pull, nie `gpio_reset_pin`, nie `gpio_hold_en`.

**Fähigkeitsprobe per ADC** (Boot, vor `setupCamera()`; die Kamera ist dann noch nicht initialisiert):

```cpp
// RESET node: Rev.5 has 10k to 3V3 behind 1k, so against the internal pull-down it stays well
// above 1 V (about 2.2-2.7 V for a 20-45k pull-down). Unconnected pads on older boards read ~0 V.
// Never pull CE down: the TP132 enable threshold is unknown.
LinePresence CamLines::probeReset()
{
    AdcSampler adc;                                   // ADC1, shared unit, used as is
    if (!adc.init(kHwResetGpio)) return LinePresence::Unknown;
    rtc_gpio_pulldown_en(kHwResetGpio);
    vTaskDelay(pdMS_TO_TICKS(3));                     // node tau ~0.8 ms
    adc.sampleOnce();
    const int mv = adc.getFilteredMilliVolts();
    rtc_gpio_pulldown_dis(kHwResetGpio);
    releaseToDigitalInput(kHwResetGpio);             // gpio_config, leaves the RTC mux
    if (mv >= 1200) return LinePresence::Present;     // thresholds: placeholders
    if (mv <= 400) return LinePresence::Absent;
    return LinePresence::Unknown;
}
```

- **CE** wird ohne Pull-down nur auf Plausibilität gelesen (Rev.5 ≈ 3,3 V). Die Board-Klasse entscheidet der RESET-Knoten.
- Der Pegel am RESET-Knoten (≥ 2,2 V) liegt über dem extrapolierten V_IH von RESETB (≈ 1,96 V). Die Probe setzt die Kamera also nicht zurück, und selbst wenn, wäre sie noch nicht initialisiert.

**Ergebnis-Typen** (erweitert um den Rail-Check):

```cpp
enum class LineOutcome : uint8_t { Done, NotAvailable, Error };
enum class RailVerdict : uint8_t { NotChecked, Collapsed, Partial, NotCollapsed, Inconclusive };
enum class RecoveryLevel : uint8_t { Reinit, HwReset, PowerCycle };
enum class RecoveryTrigger : uint8_t { Command, FrameTimeout, BootFailure };

struct RecoveryReport
{
    RecoveryTrigger trigger;
    RecoveryLevel requested, performed;
    bool degraded;                     // performed < requested, only with "auto"
    LineOutcome power, reset;
    RailVerdict rail;
    int16_t rail_control_mv[2], rail_end_mv[2];
    uint16_t off_ms, rise_ms;          // rise_ms: 2V8 back at control level, 0 if not traced
    esp_err_t reinit;                  // setupCamera()
    uint16_t pid_before, pid_after;
    bool first_frame;                  // DVP path, no SCCB involved
    uint32_t duration_ms;
};
```

**Wie ein Aufrufer die Fälle unterscheidet:**

| Aufrufer sieht | Bedeutung |
|---|---|
| `success`, `performed == requested`, `rail: collapsed` | hat funktioniert, **und die Kamera war nachweislich ohne DOVDD** |
| `success`, `performed: power_cycle`, `rail: partial` oder `not_collapsed` | Kamera läuft wieder, aber die Schiene fiel nicht (genug). Wird eigens gezählt |
| `error: not_supported` | explizite Stufe, die das Board nicht hat. Nichts angefasst |
| `success`, `degraded: true` | `auto` konnte nur eine niedrigere Stufe, und die hat funktioniert |
| `error: failed`, `failed_step` | fehlgeschlagen. `camera_state` sagt, wo die Kamera jetzt steht |
| `error: busy`, `cooldown`, `suspended`, `drain_timeout` | abgelehnt, nichts passiert |

---

## 7. Sequenzen (aktualisiert)

### Power-Cycle (Rev.5)

| # | Schritt | Zeit | Zweck bzw. Beleg |
|---|---|---|---|
| 0 | Gate schließen, Konsumenten und Frame in flight abwarten | ≤ 9 s | Abschnitt 3 |
| 1 | `esp_camera_deinit()`, `camera_sensor = nullptr` | – | – |
| 2 | Parken: XCLK Eingang + Pull-down, SCCB ohne Pulls, DVP Pull-down | – | Abschnitt 4 |
| 3 | RESET low | 2 ms | DS t6; Knoten-τ ≈ 90 µs |
| 4 | Rail-Check: Positivkontrolle an D0/D6 | < 1 ms | Kamera versorgt, Ausgänge hochohmig |
| 5 | CE low | – | – |
| 6 | Aus-Zeit: 500 ms, dann alle 100 ms Rail-Check bis `collapsed`, höchstens 3 s | 0,5–3 s | adaptiv |
| 7 | CE loslassen, RESET weiter low; im Bench-Modus Anstiegsspur | – | – |
| 8 | warten | 20 ms | Anstieg < 5 ms + t2 + t3, etwa doppelt |
| 9 | RESET loslassen | – | – |
| 10 | warten | 25 ms | t4 (F4) |
| 11 | ADC-Pads per `gpio_config` zurück ins Digitale | – | sonst toter DVP-Eingang |
| 12 | `setupCamera()` | einige 100 ms | 23 → 20 MHz wie beim Boot |
| 13 | Erfolg: PID gleich, erster Frame ≤ 2 s | ≤ 2 s | ohne SCCB |

- Endet Schritt 6 nach 3 s ohne `collapsed`, läuft der Zyklus trotzdem weiter. Der Report sagt `rail: not_collapsed`.
- **Hardware-Reset:** Schritte 0–3, dann 9–13, ohne CE und ohne Rail-Check.
- **Reinit (alle Boards):** Schritte 0–2 und 11–13. Einen eigenen I2C-Bus-Clear braucht es nicht. Das IDF setzt vor jeder Transaktion den Controller zurück und räumt den Bus frei (9 SCL-Takte), wenn die vorige mit Timeout endete oder der Bus belegt ist (`i2c_master.c:616-618`). Ein festgehaltenes SDA wird damit schon beim Probe-Durchlauf des Treibers behandelt.

### Hängt irgendetwas an I2C? (dein Punkt 2)

| Schritt | braucht I2C | Bemerkung |
|---|---|---|
| Fehlererkennung (Frame-Timeout) | nein | DVP-/DMA-Pfad |
| Gate, Leerlauf | nein | – |
| `esp_camera_deinit` | nein | keine Bustransaktion; der Abbau hängt nicht vom Buszustand ab (siehe unten) |
| Parken, CE, RESET | nein | GPIO |
| Probe, Rail-Check | nein | ADC. Die SCCB-Leitungen werden nur digital *gelesen* |
| Wiederanlauf `setupCamera()` | **ja, zwingend** | Geht I2C nach einem echten Power-Cycle nicht, ist die Kamera tatsächlich defekt: `failed` mit `failed_step: sccb_probe` |
| Erfolgsprüfung | nein | erster Frame über DVP; die PID kommt aus dem Init |

**⚠ Korrektur zu Stand 2: Der SCCB-Abbau ist kein Fehler.** In Stand 2 hatte ich eine Änderung an `SCCB_Deinit` vorgeschlagen. Nach genauem Nachlesen der IDF-Statuslogik nehme ich das zurück:
- Richtig bleibt: `SCCB_Deinit` bricht beim ersten Fehler ab (`sccb-ng.c:163-170`), und `i2c_master_bus_rm_device` verweigert bei Busstatus READ, WRITE oder START (IDF `i2c_master.c:1134`).
- Diese Zustände überleben aber keinen API-Aufruf:
  - Die synchrone Transaktion endet in jedem Ausstiegspfad mit DONE, ACK_ERROR oder TIMEOUT (`i2c_master.c:486-552`, ISR `720-725`).
  - `i2c_master_probe` setzt am Ende immer DONE (`i2c_master.c:1268` ff.).
- READ, WRITE oder START blieben nur stehen, wenn ein Task mitten in einer Transaktion gelöscht würde. Das tut im Code niemand, und die Recovery serialisiert alle Kamerazugriffe.
- Folge: **keine Änderung am vendorten Treiber und auch keine Umgehung außerhalb.** Scheitert der Wiederanlauf dennoch an „sccb init err", meldet der Report `failed_step: sccb_init`. Die letzte Stufe ist dann der optionale ESP-Neustart (Abschnitt 8).

---

## 8. Recovery (aktualisiert)

**Auslöser:**
- Kommando `recover_camera {"level": "auto"|"reinit"|"reset"|"power_cycle"}`, nur über Serial/CDC (F10).
- **Automatisch (`frame_timeout`):** Das Gate zählt `NULL` aus `esp_camera_fb_get`, solange ein Konsument Frames anfordert. Ein `NULL` bedeutet ≥ 4 s ohne Frame, auf dem S3 bis 8 s (`esp_camera.c:365`, `cam_hal.c:482-491`). Schwelle: 1 (Kconfig). Die Erkennung braucht kein I2C.
- **Automatisch (`boot_failure`, F13):** Scheitert der erste `setupCamera()`-Aufruf beim Boot, läuft genau eine Recovery mit der stärksten verfügbaren Stufe.
  - Gelingt sie, liefert `setupCamera()` `true`, und `app_main` startet UVC wie gewohnt.
  - Gelingt sie nicht, bleibt alles wie heute beim Boot-Fehler.
  - Der Aufruf sitzt in `CameraManager::setupCamera()`, nicht in `app_main` (Abschnitt 16.1).
  - Nur mit `CAMERA_AUTO_RECOVERY`, einmalig, keine Schleife.

**Grenze der Erkennung:** Selbstheilung frühestens etwa 8 s nach dem Ausfall. Schneller ginge es nur, wenn man `FB_GET_TIMEOUT` im Treiber senkt. Das ist heißer Pfad auf allen Boards und wird nicht vorgeschlagen.

**Ablauf:**
- Ein Worker-Task führt jede Recovery aus, eine zur Zeit. Das Kommando wartet auf das Ergebnis (Timeout 20 s). Der Auto-Auslöser stellt nur ein.
- Stack: Start mit 6 KB, High-Water-Mark messen. `esp_camera_init` läuft heute mit 3,5 KB.
- Solange keine Kamera läuft, wartet das Gate bis zu 3 s, bevor es `NULL` liefert. Sonst schreibt `usb_device_uvc.c:196` alle 30 ms eine ERROR-Zeile.

**Stufen:**
- `auto` nimmt die stärkste verfügbare (F5).
- Eine explizite Stufe ist strikt: Fehlt sie auf dem Board, kommt `not_supported`.
- Erfolg heißt: Init ok, PID gleich, erster Frame ≤ 2 s.

**Budget (⚠ Abweichung von F7):**
- Cooldown 5 s nach jedem Versuch.
- Nach **3 Fehlschlägen in Folge** ist die Automatik gesperrt (`suspended`).
- **Ratenfenster:** höchstens 10 automatische Versuche pro 10 min.
- Sperre und Fenster schreiben je eine WARN-Zeile in den persistenten Log.
- Manuelle Kommandos gehen an der Sperre vorbei, halten aber den Cooldown.
- Grund: Mit einem harten Limit von 10 pro Boot kann eine einzige Prüfung mit 10 Entladungen je Polarität und Prüfpunkt das Budget aufbrauchen. Danach wäre jeder weitere Ausfall „braucht Eingriff". Das Ziel „keine Endlosschleife" erreichen Fehlschlag-Sperre und Ratenfenster, ohne erfolgreiche Erholungen zu deckeln.

**Endzustand nach Fehlschlag:** Kamera versorgt, CE und RESET losgelassen, Treiber unten (F6).

**ESP-Neustart als letzte Stufe (F15):** eigene Kconfig-Option `CAMERA_RECOVERY_ESP_RESTART`, Default aus.
- Ist sie an und die Automatik gesperrt, startet die Firmware den ESP neu.
- Davor schreibt sie eine WARN-Zeile und setzt einen Marker in `RTC_NOINIT`-Speicher. Der überlebt einen Software-Reset, braucht aber keinen Flash-Zugriff.
- Der nächste Boot erkennt am Marker „Neustart durch Kamera-Recovery". Er loggt das getrennt und zählt es getrennt von `restart_device` und von einem echten ESD-Reset.

**Zähler als Testergebnis** (`get_camera_status`, dein Punkt 4; nur mit Feature, siehe 16.2 d):
- Versuche und Erfolge je Auslöser und je Stufe.
- Rail-Verdikte (`collapsed`/`partial`/`not_collapsed`/`inconclusive`).
- Sperren, die letzten 4 Reports.
- Heap intern (frei, größter Block).
- Reset-Grund dieses Boots (`esp_reset_reason`) und ob der Marker für den Recovery-Neustart gesetzt war.
- Beim Boot eine WARN-Zeile, wenn der Reset-Grund nicht Power-on oder Software war. Ein ESD-bedingter ESP-Neustart gehört ins Testergebnis. Diese Zeile gibt es nur mit Feature, sie sitzt in `CameraManager`.
- **Kein NVS** (F18). Stattdessen schreibt `get_camera_status {"persist": true}` am Ende eines Prüflaufs die Zähler als **eine** WARN-Zeile. Der LogManager übernimmt sie mit seinem normalen Flush in den persistenten Log. Es entsteht kein neuer Schreibpfad und kein Flash-Zugriff je Ereignis; der Capture muss dafür an sein.

**Logs:** Jede Recovery schreibt eine WARN-Zeile mit Auslöser, Stufe, Rail-Verdikt und Dauer, Fehlschläge eine ERROR-Zeile. Achtung: Der Log-Capture ist auf FFVR per Default **aus** (`CONFIG_DEBUG_LOG_DEFAULT_ENABLED` nicht gesetzt). Vor einem Prüflauf also einmal `set_debug_log_enabled true` pro Gerät; das bleibt im NVS gespeichert. Die Zähler funktionieren unabhängig davon.

```json
// recover_camera (success)
{ "result": "recovered", "trigger": "command", "requested": "auto", "performed": "power_cycle",
  "degraded": false, "steps": { "power": "done", "reset": "done", "reinit": "ok", "first_frame": true },
  "rail": { "verdict": "collapsed", "control_mv": [3100, 3080], "end_mv": [520, 510], "off_ms": 500 },
  "pid_before": 13920, "pid_after": 13920, "duration_ms": "<measured>" }
```

Die mV-Werte sind Beispiele für das Format, keine Messwerte.

---

## 9. Bench-Kommando ohne Gate (deine Antwort, Abschnitt 6, Punkt 2)

**Prüfung deines Vorschlags:**
- UVC ruft `fb_get` tatsächlich nur bei geöffnetem Host-Stream auf (`usb_device_uvc.c:145-156`).
- Der StreamServer läuft auf FFVR nie, weil kein WLAN.
- **Ein Flag, das nur beim Kommando geprüft wird, reicht aber nicht.** Öffnet der Host den Stream, während die Kamera aus ist, ruft `camera_start_cb` → `setCameraResolution` den hängenden `camera_sensor` auf. Das stürzt ab. Dazu kommt ein kleines Rennfenster während des Deinit selbst. Um beides abzufangen, müsste das Flag in die UVC-Callbacks, also in den heißen Pfad.

**Lösung ohne heißen Pfad:**
- `camera_power_cycle` läuft nur, solange **kein Streaming-Backend gestartet** wurde (`getUsbHandoverDone() == false`, `main_globals.cpp:39-46`). Das ist der Fall im Setup-/Heartbeat-Modus und nach einem gescheiterten Kamera-Boot.
- Dort existiert kein UVC, also kein Konsument.
- Das Kommando läuft synchron; der Serial-Task ist währenddessen blockiert, ein zweites Kommando kann nicht dazwischen.
- `camera_sensor` wird im Deinit-Pfad genullt; die Setter prüfen ihn bereits.

**⚠ Abweichung von `off/hold_s`:** ein synchroner Zyklus `camera_power_cycle {"off_ms": 500, "trace": true, "force": false}` mit `off_ms` ≤ 30 000 (die Obergrenze erlaubt den DMM-Blick aus F16). Die Antwort enthält den vollständigen Report mit Spuren. Es gibt keinen stehenbleibenden Aus-Zustand und keinen Timer-Kontext, und die Messdaten kommen in derselben Antwort zurück. `force` fährt die volle Sequenz auch auf Boards ohne Leitungen; so wird F2 per Wirkung bestätigt.

**Testablauf:** `switch_mode setup` → Neustart → ein beliebiges Kommando (hält den Heartbeat, `SerialManager.cpp:13-25`) → Bench-Zyklen über USB-Serial-JTAG → `switch_mode uvc`. Im Setup-Modus startet UVC nie von selbst (`openiris_main.cpp:131-135`).

Damit bleibt deine Reihenfolge möglich: Das Gate kommt erst mit AP3.

---

## 10. Grenze des Zustandsautomaten

Die Zustände bleiben wie in Stand 1: `Uninitialized`, `Starting`, `Running`, `Stopping`, `Off`, `Failed`.

- **Neu im ersten Wurf:** automatische Auslöser als Policy auf den Primitiven `stop()`/`start()`.
- **Weiterhin später:** UVC-abhängiges Abschalten (asynchroner Start aus dem TinyUSB-Kontext, ein eigener `CameraState_e` für „aus") und die Entscheidung CE-Aus gegen Software-Standby.
- **Kleine Ergänzung in AP3:** Der UVC-Start setzt die Framegröße nur, wenn sie sich ändert. `camera_start_cb` läuft im TinyUSB-Task; bei gestörtem Bus blockiert dort jeder SCCB-Aufruf bis 1 s (`sccb-ng.c:35`). `write_regs` bricht zwar beim ersten Fehler ab (`ov3660.c:96-108`), trotzdem gehört dort im Normalfall kein SCCB-Verkehr hin.

---

## 11. Reihenfolge und Arbeitspakete

Deiner Reihenfolge stimme ich zu. Das Bench-Kommando braucht kein Gate, weil es nur ohne Streaming-Backend läuft.

**Jedes AP gilt erst als abgeschlossen, wenn die Referenzkonfigurationen bitgleich zur Baseline bauen (Verfahren 16.3).** Weicht es ab, klärt der Vergleich, ob etwas nicht sauber ausgeschlossen war oder ob eine Kategorie-B-Änderung darin steckt; das melde ich vor dem Abschluss.

| # | Inhalt | Abnahme | Wirkung auf Rev.4/4.5 |
|---|---|---|---|
| AP0 | Vorab-Commit (a)+(b), Kategorie B | Abschnitt 1 | Neustart kommt 2 s später (alle Boards) |
| B0 | `tools/compare_builds.py`, Selbsttest (zweimal derselbe Stand, muss gleich sein), Baseline der Referenzkonfigurationen | Selbsttest bestanden, Baseline abgelegt | keine |
| AP1 | Kconfig, `CamLines`, `static_assert` (= Punkt c), ADC-Probe beim Boot, Boot-Logzeile, `get_camera_status` (Leitungen, Zustand, PID, Reset-Grund), alles hinter dem Feature | Rev.4.5: `absent`, Rev.5: `present`, auf allen drei ESPs (bestätigt F1/F2 in Hardware); 100 Warmstarts unverändert; Referenz bitgleich | 3 ms Pull-down auf unbeschaltetem Pad |
| AP2 | `camera_power_cycle` (Abschnitt 9): Deinit, Parken, CE/RESET, Rail-Check mit Spuren, Reinit; ADC2-Helfer | Rev.5 `collapsed`; Rev.4.5 mit `force` `not_collapsed`; Referenz bitgleich | nur auf Kommando |
| – | **Nachweisläufe** (`test_camera_power_proof`), danach Aus-Zeit und Wartezeiten festschreiben; dein DMM-Blick auf 1V5_Cx | – | – |
| AP3 | Lebenszyklus, Gate (UVC und StreamServer, ohne Feature zusammengefallen, 16.2 a), PID-/Framegrößen-Cache, Worker-Task | fps und Latenz unverändert auf FFVR; 200 × stop/start ohne Heap-Verlust; Referenz bitgleich | Mutex je Frame nur mit Feature |
| AP4 | Recovery: `recover_camera`, Auto-Auslöser (`frame_timeout`, `boot_failure`), Budget, Zähler, Logs, Reset-Grund, ESP-Neustart (Default aus), Testhaken (Default aus) | Tests aus Abschnitt 12; Referenz bitgleich | Stufe `reinit`, Auto nur bei Ausfall |
| AP5 | Tests, Menüpunkte im Setup-Tool (Status, Recovery) | grün auf Rev.4.5, Rev.5 und dem OV2640-Board | – |
| AP6 | Ergebnisse der Nachweisläufe nach `docs/` | – | – |

**Branches und Commits**, damit alles nachvollziehbar bleibt:
- **Kategorie B:** je Fix ein eigener Zweig `fix/<thema>` von `main`, ein Commit, mit Angabe im Commit-Text, was sich für andere Boards ändert. AP0 liegt so auf `fix/esp-timer-units`, einziger Commit ist `9156d3e`. Nach deiner Hardware-Abnahme geht er per Fast-Forward nach `main`.
- **Kategorie A:** ein Zweig `feature/camera-power` von `main`, erst **nach** dem Merge von AP0, damit die Baseline AP0 enthält. Pro AP mindestens ein Commit; jede Commit-Nachricht nennt das AP und das Ergebnis des Bitvergleichs.
- **Baseline:** Die Binärstände liegen außerhalb des Repos. Commit-Hash, Konfiguration und Toolchain-Version stehen im Commit von B0.
- **Dieses Dokument:** Es ist noch nicht eingecheckt; Stand 1 ist dadurch nicht mehr im Original vorhanden, nur über die Änderungstabellen. Vorschlag: Stand 3 nach deiner Freigabe auf einem eigenen Zweig `docs/cam-ce-reset` committen, jeder weitere Stand als eigener Commit.
- Ich wechsle den Zweig nie mit unversionierten Änderungen an Quelldateien. Vor jedem Commit prüfe ich, auf welchem Zweig ich stehe.

**Testhaken für AP4** (nur mit `CAMERA_TEST_HOOKS`): `camera_test_fault {"kind": "hold_reset"}` (Rev.5: RESET mitten im Stream) bzw. `{"kind": "sensor_standby"}` (alle Boards: `0x3008[6]`, DS S. 36). Beide stoppen die Frames. So lässt sich der Auto-Auslöser ohne ESD-Pistole prüfen.

---

## 12. Tests ohne Instrumente (`tests/`)

| Test | Board | Modus | prüft | Dauer (ca.) |
|---|---|---|---|---|
| `test_camera_capability.py` | alle | beliebig | `lines` passt zu `--board-rev`; Rail-Verdikt eines erzwungenen Bench-Zyklus passt zur Fähigkeit | 1 min |
| `test_camera_power_proof.py` | Rev.5 | Setup | 50 Bench-Zyklen mit Spur, alle `collapsed`; Abfall- und Anstiegszeiten als CSV | 5 min |
| `test_camera_recovery.py` | alle | UVC, Host-Stream offen | 200 × `recover_camera`: Erfolg, PID gleich, Frames laufen ohne Neuöffnen weiter, größter freier interner Block stabil | 25 min |
| `test_camera_auto_recovery.py` | alle | UVC, Host-Stream offen | Testhaken → Auto-Auslöser → Erholung ohne Kommando; Sperre nach 3 Fehlschlägen | 10 min |
| `test_boot_cycles.py` | alle | UVC | 100 Warmstarts, Kamera `running`. Kaltstarts nur mit schaltbarer USB-Versorgung | 30 min |
| bestehende Suite | Rev.4.5 | – | Regression | – |

**Was angesteckt sein muss:**
- Ein Board per USB. Alle drei ESPs melden sich über den Hub an. Jeder Lauf adressiert einen ESP wie bisher über `--board` und `--connection`.
- Neue Optionen:
  - `--board-rev 4.5|5`,
  - `--uvc-index N` (Kamera-Index des ESP unter Test, weil OpenCV unter Windows keine Gerätenamen liefert),
  - optional `--power-off-cmd`/`--power-on-cmd` (Shell-Befehle zum Schalten der USB-Versorgung; ohne sie werden Kaltstarts übersprungen).
- Neue optionale Abhängigkeitsgruppe `hwtest` mit `opencv-python` für den Host-Stream. Keine andere Anwendung darf die Kamera belegen.
- Die Tests schalten zu Beginn `set_debug_log_enabled true` ein und lesen am Ende `get_persistent_logs` mit aus.

---

## 13. Offene Fragen

Beantwortet und eingearbeitet: F13 (ja, Abschnitt 8), F14 (ja), F15 (Option, Default aus), F16 (Argument akzeptiert, DMM-Blick per Kommando), F17 (zwei Knoten), F18 (kein NVS, Zusammenfassung auf Kommando).

Weiter offen:
- **F19:** Welche Revision hat das OV2640-Board? Davon hängt ab, welche Stufen der Test dort erwartet.
- **F20:** Gibt es einen vom PC schaltbaren USB-Port? Die Tests laufen auch ohne ihn und überspringen dann Kaltstarts.

Neu:
- **F21 (Kategorie-B-Kandidat, frage vorher, weil das beabsichtigte Verhalten zur Debatte steht):** `LogManager` sammelt bei aktivem Capture jede WARN- und ERROR-Zeile bis zum nächsten Flush (Default 10 s) in einem unbegrenzten `std::vector` (`LogManager.cpp:170-174`). Bei einer Log-Flut wächst das ohne Grenze. Auf Boards ohne PSRAM kann das den Heap erschöpfen. Eine Obergrenze hieße, bei Überlauf Zeilen zu verwerfen und das mitzuzählen. Soll der Log unter Flut vollständig bleiben oder gedeckelt werden?
- **F24 (erledigt):** Lösung A, Backport des Fixes aus v5.5 (17.6).
- **F25 (erledigt):** Der Fehler folgt der Kamera mit dem geknickten Kabel (17.6).
- **F23 (erledigt, deine Freigabe „wie es sinnvoller ist"):** Soll `tools/openiris_device.py` beim Verbinden DTR/RTS schon *vor* dem Öffnen auf low setzen, damit ein Verbinden im Setup-Modus den ESP nicht mehr neu startet (17.5, Befund 2)? Dagegen spricht nur, falls sich `setup_openiris.py` oder die Tests auf den frischen Boot verlassen, etwa um sicher im Startfenster zu landen. Das habe ich nicht geprüft.
- **F22:** Referenzkonfigurationen für den Bitvergleich: `project_babble` (S3, UVC und WLAN; kompiliert UVCStream und StreamServer mit) und `wrooms3` (S3, nur WLAN). Einverstanden, oder willst du eine bestimmte? Ein klassischer ESP32 (`esp32AIThinker`) ginge zusätzlich, aber nur in einem eigenen Worktree, weil `switchBoardType.py` beim Plattformwechsel Komponenten verschiebt.

## 14. Nicht verifiziert

- Die Pad-Strukturen aus Tab. 1-3 sind Ersatzschaltbilder. Das Rail-Check-Verfahren ist darauf ausgelegt, bei Abweichungen `inconclusive` statt eines falschen Ergebnisses zu liefern.
- Ob XCLK nach `esp_camera_deinit()` weiterläuft. Der Plan hängt nicht davon ab.
- Der Wert der internen Pulls des S3 (≈ 45 kΩ). Mit dem ADC nur noch eine Frage des Abstands.
- Die EN-Schwelle und die Ausgangsentladung des TP132 (kein Datenblatt). Deshalb nie ein Pull-down auf CE, und die Entladung zeigt die Abfallkurve.
- Welcher Pull-Registersatz im RTC-Modus wirkt. Das prüfe ich in AP2 am Chip.
- ADC2-Genauigkeit auf dem S3. Die relative Messung macht sie weitgehend unerheblich.
- Ob PCLK und HREF einen Pfad nach DOVDD haben (nicht in Tab. 1-3). Für den Plan nicht nötig.
- 1V5_Cx (Abschnitt 5).
- Das Verhalten des UVC-Hosts bei einer Lücke von etwa 1 s, Heap nach vielen Reinits, die Laufzeit von `setupCamera()`. Das decken die Tests ab.
- Ob sich der OV3660 nach einem Power-Cycle wie nach einem Kaltstart verhält (Befund vom 2026-07-20). Der Plan nutzt bewusst denselben `setupCamera()`-Pfad, den Rest zeigen die Tests.
- Ob neue `REQUIRES`-Kanten die Link-Reihenfolge der Referenzkonfigurationen ändern (16.2). Das zeigt der erste Vergleich nach AP1 bzw. AP3.
- Ob die `always_inline`-Wrapper in jeder Optimierungsstufe exakt denselben Code erzeugen wie der direkte Aufruf. Das zeigt der Vergleich nach AP3.

## 15. Nebenbefunde (nur benannt)

- `LogManager` mit unbegrenztem Sammelpuffer: jetzt als Frage F21 (Abschnitt 13). Unabhängig davon ist er ein Grund für die Wartezeit im Gate (Abschnitt 8).

---

## 16. Isolation gegenüber fremden Boards

### 16.1 Kategorie A: Feature-Symbole

Vorbild ist der FanManager. Alle Symbole stehen in `components/CameraManager/Kconfig.projbuild`, sind nach der Funktion benannt und per Default aus:

```
config CAMERA_POWER_CONTROL           bool, default n    CE/RESET lines, probe, camera_power_cycle, get_camera_status
    CAMERA_POWER_EN_GPIO              int,  depends on CAMERA_POWER_CONTROL
    CAMERA_HW_RESET_GPIO              int,  depends on CAMERA_POWER_CONTROL
    CAMERA_RAIL_SENSE                 bool, depends on CAMERA_POWER_CONTROL && !GENERAL_ENABLE_WIRELESS
        CAMERA_RAIL_SENSE_GPIO_A/B    int,  depends on CAMERA_RAIL_SENSE
config CAMERA_RECOVERY_ENABLE         bool, default n    frame gate, worker, recover_camera, get_camera_status
    CAMERA_AUTO_RECOVERY              bool, depends on CAMERA_RECOVERY_ENABLE       frame_timeout, boot_failure
        CAMERA_RECOVERY_ESP_RESTART   bool, default n, depends on CAMERA_AUTO_RECOVERY
    CAMERA_TEST_HOOKS                 bool, default n, depends on CAMERA_RECOVERY_ENABLE
```

- Durch `depends on` landen abhängige Symbole ohne den Schalter gar nicht in `sdkconfig.h`. Der Code prüft deshalb immer zuerst den Schalter.
- Recovery hängt nicht an Power-Control. Ein Board ohne Leitungen könnte Recovery mit der Stufe `reinit` haben; umgekehrt gibt es Power-Control mit Bench-Kommando ohne Recovery.
- Gesetzt wird alles ausschließlich in `boards/facefocusvr/eye_L`, `eye_R` und `face`. Es gibt keine Abfrage auf die Board-Identität.

**Wo der Code sitzt, und was ohne Feature übrig bleibt:**

| Ort | Inhalt | ohne Feature |
|---|---|---|
| `components/CameraManager/CMakeLists.txt` | neue Quellen nur per `if(CONFIG_…)`, wie beim FanManager: `CamLines.cpp`, `CameraPower.cpp` (Sequenzen, Rail-Check, ADC2), `CameraRecovery.cpp` (Policy, Worker) | nicht übersetzt |
| `CameraManager.hpp` | Frame-Wrapper (16.2 a); neue Deklarationen unter `#if` | Wrapper = direkter Treiberaufruf |
| `CameraManager.cpp` | unter `#if`: Probe vor dem ersten Init, `boot_failure` danach, Reset-Grund-Zeile, `camera_sensor` im Deinit-Pfad nullen | Präprozessorausgabe identisch |
| `UVCStream.cpp`, `StreamServer.cpp` | `esp_camera_fb_get/return` und `esp_camera_sensor_get` 1:1 durch die Wrapper ersetzt, sonst nichts | identischer Code |
| `CommandManager.hpp/.cpp` | Enum-Werte, Map-Einträge und `case`-Zweige unter `#if`, jeweils **am Ende** | identisch |
| `CommandManager/CMakeLists.txt` | neue Kommandodatei per `if(CONFIG_…)` | nicht übersetzt |
| `main/openiris_main.cpp` | **keine Änderung** | – |
| `components/esp32-camera` | **keine Änderung** | – |
| `boards/facefocusvr/*` | einzige Stelle, an der die Symbole gesetzt werden | – |

Zwei Stellen, die in deiner Liste fehlten:
- **Enum `CommandType`:** Neue Werte müssen ans Ende. Mitten in der Aufzählung würden sie die Nummern aller folgenden Kommandos verschieben und damit Code in jeder Konfiguration ändern.
- **`main/openiris_main.cpp` bleibt unberührt.** Es ist die einzige der betroffenen Dateien mit zeilennummer-empfindlichen Makros (`ESP_ERROR_CHECK` in Zeile 100, 103, 270 und 271). Jede Zeile darüber, auch ein `#include` unter `#if`, verschiebt `__LINE__`-Konstanten im Binary jeder Konfiguration. Deshalb sitzen Probe, `boot_failure` und Reset-Grund-Zeile in `CameraManager::setupCamera()`. Die anderen betroffenen Dateien enthalten keine solchen Makros (geprüft).

### 16.2 Deine Punkte (a)–(f)

**(a) Frame-Gate.** Das geht ohne hässlichen Code:

```cpp
// CameraManager.hpp
#if CONFIG_CAMERA_RECOVERY_ENABLE
camera_fb_t* cameraAcquireFrame();
void cameraReleaseFrame(camera_fb_t* fb);
#else
// Without the feature these are the plain driver calls: no lock, no state, no symbol.
__attribute__((always_inline)) static inline camera_fb_t* cameraAcquireFrame() { return esp_camera_fb_get(); }
__attribute__((always_inline)) static inline void cameraReleaseFrame(camera_fb_t* fb) { esp_camera_fb_return(fb); }
#endif
```

- `always_inline` erzwingt die Einbettung unabhängig von der Optimierungsstufe der jeweiligen Konfiguration. Ob der Code wirklich gleich bleibt, zeigt der Vergleich nach AP3.
- Ein Haken: `StreamServer` hängt heute nicht an `CameraManager` (`components/StreamServer/CMakeLists.txt` hat es nicht in `REQUIRES`). Für den Wrapper braucht es diese Kante.
- `REQUIRES` lassen sich nicht an Kconfig knüpfen; das beschreibt der Kommentar in `components/FanManager/CMakeLists.txt:1-5`. Eine neue Kante kann die Link-Reihenfolge ändern.
- Zeigt der Vergleich das, ist Plan B: `CAMERA_RECOVERY_ENABLE` setzt zusätzlich `!GENERAL_ENABLE_WIRELESS` voraus, und StreamServer bleibt ganz unberührt. Das trifft FFVR nicht, nähme einem künftigen WLAN-Board aber die Recovery. Ich entscheide das erst mit Messdaten und sage dir dann Bescheid.
- Dieselbe Frage stellt sich bei `CameraManager`. Die Kamerakomponente braucht für ADC2 künftig `esp_adc` und für die ADC1-Probe `Monitoring` (`AdcSampler`). Beide sind schon im Build, nur die Kanten sind neu.

**(b) `SCCB_Deinit`.** Erledigt, und zwar ohne Änderung. Die Analyse zeigt keinen erreichbaren Fehler (Abschnitt 7), also gibt es weder einen Kategorie-B-Fix im Treiber noch eine Umgehung außerhalb.

**(c) `static_assert`.** Die Asserts stehen in `CamLines.cpp`. Die Datei wird ohne Feature gar nicht übersetzt, sie werden also nie ausgewertet. Die inneren Prüfungen sind zusätzlich an ihr eigenes Feature gebunden:

```cpp
// CamLines.cpp, only built with CONFIG_CAMERA_POWER_CONTROL
static_assert(CONFIG_CAMERA_POWER_EN_GPIO >= 0 && CONFIG_CAMERA_HW_RESET_GPIO >= 0, "camera lines not configured");
#if CONFIG_LED_DEBUG_ENABLE
static_assert(CONFIG_LED_DEBUG_GPIO != CONFIG_CAMERA_POWER_EN_GPIO && CONFIG_LED_DEBUG_GPIO != CONFIG_CAMERA_HW_RESET_GPIO,
              "debug LED collides with a camera line");
#endif
#if CONFIG_FAN_PWM_ENABLE
static_assert(CONFIG_FAN_PWM_GPIO != CONFIG_CAMERA_POWER_EN_GPIO && CONFIG_FAN_PWM_GPIO != CONFIG_CAMERA_HW_RESET_GPIO,
              "fan PWM collides with a camera line");
#endif
#if CONFIG_CAMERA_RAIL_SENSE
// ADC2 range, must be DVP data pins, must not be the 32 kHz crystal pins if one is ever enabled
#endif
```

**(d) Kommandos.**
- `camera_power_cycle` hängt an `CAMERA_POWER_CONTROL`, `recover_camera` an `CAMERA_RECOVERY_ENABLE`, `camera_test_fault` an `CAMERA_TEST_HOOKS`.
- `get_camera_status` hängt an einem der beiden Hauptschalter.
- Die angebotene Ausnahme beanspruche ich nicht. Ohne Feature hätte der Status nur Zustand und PID, die heute niemand abfragt. Dafür würde sich jedes Binary ändern und die Bitgleichheit als Prüfmittel verloren gehen.

**(e) Worker und ADC2.** Beide leben in Quellen, die ohne Feature nicht übersetzt werden. Es entsteht also kein Task und kein ADC-Kanal. Angelegt werden sie erst beim ersten `setupCamera()` mit Feature. `!GENERAL_ENABLE_WIRELESS` steht nur als zusätzliche Bedingung an `CAMERA_RAIL_SENSE`.

**(f) `CONFIG_LED_DEBUG_GPIO`:** Der Default bleibt, die Kollision fängt der Assert aus (c).

### 16.3 Bitgleichheit: Verfahren

**⚠ Abweichung:** Ein direkter Byte-Vergleich von `blink.bin` taugt nicht, denn schon zwei Builds desselben Stands unterscheiden sich:
- **Zeitstempel:** Build-Zeit und -Datum stehen in der App-Beschreibung (`CONFIG_APP_COMPILE_TIME_DATE=y`).
- **Version:** Sie kommt aus `git describe` und ändert sich mit jedem Commit, zuletzt z. B. `v1.2.4-2-g35e815a-dirty` im Build-Log.
- **ELF-Hash:** Der SHA-256 der ELF-Datei steht an Offset `0xb0` im Image (`esptool elf2image --elf-sha256-offset 0xb0`). Er deckt die Debug-Information samt Zeilennummern ab. Jede Änderung an einer gemeinsamen Datei, auch innerhalb eines `#if`, ändert ihn, ohne eine einzige Instruktion zu ändern.
- **Prüfsummen:** Die Image-Prüfsumme und der angehängte SHA-256 folgen daraus.

„Bitgleich" heißt deshalb bei mir: **identisches Image bis auf ELF-Hash und die daraus abgeleiteten Image-Prüfsummen; Zeit und Version sind für den Referenzbuild festgesetzt.** Alles andere muss Byte für Byte gleich sein.

**Ablauf:**
1. **Eigener Worktree:** Referenzbuilds laufen in einem eigenen `git worktree`. `switchBoardType.py` schreibt das eingecheckte `sdkconfig` und verschiebt beim Plattformwechsel Komponenten (`switchBoardType.py:202-248`); im Arbeitsbaum soll das nicht passieren.
2. **Feste Referenzeinstellungen:** Die Referenzkonfiguration bekommt eine kleine Zusatzdatei mit `CONFIG_APP_REPRODUCIBLE_BUILD=y` (entfernt Zeit, Datum und Pfade; IDF `Kconfig:257-263`) sowie `CONFIG_APP_PROJECT_VER_FROM_CONFIG=y` und `CONFIG_APP_PROJECT_VER="reference"`.
3. **Vergleich mit `tools/compare_builds.py`** (neues Host-Werkzeug):
   - geht die Segmente von `blink.bin` durch,
   - blendet die 32 Byte ELF-Hash sowie Prüfsumme und angehängten SHA-256 aus,
   - meldet jede Abweichung mit Adresse und Symbol aus `blink.map`.
4. **Selbsttest:** derselbe Stand zweimal gebaut, muss gleich sein. Erst dann gilt die Baseline.
5. **Referenzkonfigurationen:** `project_babble` und `wrooms3` (F22).
6. **Zeitpunkte:**
   - Baseline unmittelbar vor AP1, auf dem Stand mit allen Kategorie-B-Fixes bis dahin (mindestens AP0).
   - Vergleich nach jedem AP.
   - Jeder spätere Kategorie-B-Fix bekommt eine neue Baseline, dokumentiert im Commit.

Nebenwirkung: Das eingecheckte `sdkconfig` bekommt beim nächsten Build Zeilen wie `# CONFIG_CAMERA_POWER_CONTROL is not set`. Das ist kosmetisch, `sdkconfig.h` bleibt dadurch unverändert.

### 16.4 Kategorie B: Stand

| Fix | Stand | Änderung für andere Boards |
|---|---|---|
| AP0, Timer-Einheiten | Commit `9156d3e` auf `fix/esp-timer-units`, wartet auf deine Hardware-Abnahme | Abschnitt 1 |
| `SCCB_Deinit` | zurückgezogen, kein Fehler | – |
| `LogManager`-Sammelpuffer | Kandidat, Frage F21 | – |

---

## 17. Umsetzungsstand

### 17.1 Zweig und Commits

Zweig `feature/camera-power`. ⚠ Er zweigt von `fix/esp-timer-units` ab, nicht von `main`, weil AP0 noch auf deine Hardware-Abnahme wartet und die Baseline AP0 enthalten soll. Nach der Abnahme `main` per Fast-Forward auf `fix/esp-timer-units` setzen; dann liegt der Feature-Zweig ohne Umbau darauf.

| Commit | Inhalt |
|---|---|
| `a3199ad` | dieses Dokument, Stand 3 (⚠ auf dem Feature-Zweig statt auf einem eigenen `docs/`-Zweig, damit es nicht zu viele Zweige werden) |
| `de3ae87` | B0: `tools/compare_builds.py`, Baseline-Angaben im Commit-Text |
| `68cb4fa` | AP1: `CamLines`, Probe beim Boot, `get_camera_status` |
| `c003f0e` | AP1-Nachtrag: digitale Pulls vor der Probe löschen |
| `a703f47` | AP2: `camera_power_cycle`, Rail-Check (`RailSense`) |
| `a69d7d2` | `tools/camera_power_bench.py` |
| `ccca927` | AP2-Nachtrag: Zyklus auf eigenem Task (Stack, Befund auf Hardware, 17.5) |
| `16effe2` | Bench-Werkzeug öffnet den Port ohne Chip-Reset (17.5) |
| `76d14c4` | Dokument: Ergebnisse Rev.4.5 |
| `80ba1a6` | AP2-Nachtrag: übersteuerte ADC-Werte begrenzen, gemessene Schwellen (17.6) |
| `e9a7214` | Bench-Werkzeug bricht ab, wenn das Gerät nicht mehr antwortet |

Kategorie B, eigener Zweig von `main`: `fix/serial-no-reset-on-connect` (`7a7fe7f`), `tools/openiris_device.py` verbindet ohne Board-Reset (F23).
Kategorie B, eigener Zweig von `main`: `fix/i2c-nack-busy-wait` (F24, Lösung A):
- `a81b0a7`: `esp_driver_i2c` aus ESP-IDF v5.4.2 unverändert als Projektkomponente. ⚠ Zwei Commits statt einem, damit der eigentliche Fix im zweiten als kleiner Diff lesbar bleibt.
- `0994932`: begrenzte Warteschleife nach NACK, wörtlich wie in v5.5, und ein Build-Schutz, der bei einer anderen IDF-Version abbricht.
- In den Feature-Zweig gemergt als `ab89e37`.

Nicht gepusht, nichts nach `main` gemergt.

### 17.2 Abweichungen vom Plan

| Thema | Plan | umgesetzt | Grund |
|---|---|---|---|
| Referenzbuilds | eigener Worktree | eigenes `sdkconfig` und eigener Build-Ordner per `idf.py -D SDKCONFIG=… -B …`; ab AP2 zusätzlich ein Worktree `../OpenIris-refbuilds/wt`, nur damit Builds parallel zur Arbeit laufen | das eingecheckte `sdkconfig` und `build/` bleiben unberührt. Gegenprobe: derselbe Stand im Worktree und im Hauptbaum gebaut ist byte-identisch, sogar ohne Ausblenden |
| neue Komponentenkanten (16.2, 14) | `REQUIRES`, Plan B bei geänderter Link-Reihenfolge | `idf_component_optional_requires` unter `if(CONFIG_…)` | eine `REQUIRES`-Kante ändert laut IDF-Quelltext die Link-Reihenfolge aller Boards. Die optionale Kante entsteht nur mit Feature. Der offene Punkt aus Abschnitt 14 ist damit erledigt |
| Status-Schalter | „einer der beiden Hauptschalter" | abgeleitetes Symbol `CAMERA_STATUS` ohne Prompt (y mit `CAMERA_POWER_CONTROL`, ab AP4 auch mit `CAMERA_RECOVERY_ENABLE`) | eine Bedingung statt einer Oder-Verknüpfung an jeder Stelle |
| Fähigkeitsprobe | nur Pull-down, `present` ≥ 1200 mV | Pull-up **und** Pull-down. `present`: mit Pull-down ≥ 1200 mV und ≥ 150 mV unter dem Pull-up-Wert. `absent`: mit Pull-down ≤ 400 mV und mit Pull-up ≥ 2000 mV. Sonst `unknown` | ohne Beleg, dass der Pull überhaupt wirkt, könnte ein offenes Pad fälschlich `present` ergeben. Jetzt endet das in `unknown`. Wirkung auf Rev.4/4.5: je 3 ms Pull-up und Pull-down auf dem unbeschalteten Pad |
| Reset-Grund | WARN außer Power-on/Software | auch `usb` gilt als Routine | Host-Reset über USB-Serial-JTAG (z. B. beim Öffnen des Monitors) ist kein Prüfereignis |
| Zustände | sechs aus Abschnitt 10 | nur die erreichbaren: AP1 `uninitialized/starting/running/failed`, AP2 zusätzlich `stopping/off` | kein toter Code |
| `camera_power_cycle` | Sperre bei `getUsbHandoverDone()` | zusätzlich im WLAN-Streaming-Modus gesperrt (nur Boards mit WLAN) | dort ist der StreamServer Konsument. FFVR betrifft das nicht |
| Rev.4/4.5 mit `force` | `not_collapsed` | `not_collapsed` **oder** `inconclusive` | ohne Reset-Leitung ist die Kamera vor CE low nicht im Reset und kann D0/D6 low treiben; dann ist die Positivkontrolle zu niedrig. Beides ist fail-safe |
| Spuren | Abfall alle 20 ms | Abfall alle 20 ms, höchstens 150 Punkte (3 s); Anstieg alle 0,5 ms über die 20 ms bis zur RESET-Freigabe | begrenzte Antwortgröße |

Zusätzlich, nicht im Plan: `tools/camera_power_bench.py` für die Nachweisläufe (reines Host-Werkzeug).

### 17.3 Bitgleichheit

Baseline `a3199ad` (Firmware = `9156d3e`), ESP-IDF v5.4.2, `xtensa-esp-elf-gcc` 14.2.0. Selbsttest des Vergleichs bestanden, Wiederholungsbuild byte-identisch.

| Stand | `project_babble` | `wrooms3` |
|---|---|---|
| AP1 `68cb4fa` | identisch | identisch |
| AP2 `a703f47` | identisch | identisch |
| AP2-Nachtrag `ccca927` | identisch | identisch |
| AP2-Nachtrag `80ba1a6` | nicht gebaut: geändert sind nur Dateien, die ohne Feature nicht übersetzt werden | – |

**Neue Baseline nach dem I2C-Fix (Kategorie B, ändert jedes Image):** `8534138` = `fix/esp-timer-units` + `fix/i2c-nack-busy-wait` (lokaler Merge ohne Zweig; nachbaubar aus den beiden Zweigen). App-SHA-256 `project_babble` `d1cfa7f7…`, `wrooms3` `fdd29f92…`.

Wirkung des Fixes auf `project_babble`:
- `main` → unveränderte Kopie: Layout verschiebt sich, weil der eingebettete Quellpfad `/IDF/components/…` zu `./components/…` wird. Keines der 8963 Symbole ändert seine Größe.
- Kopie → Fix: genau ein Symbol ändert sich, `s_i2c_send_commands` 524 → 592 Byte.

| Stand | `project_babble` | `wrooms3` |
|---|---|---|
| Feature-Zweig `ab89e37` gegen Baseline 2 | identisch | identisch |

„Identisch" heißt: `app.bin` bis auf ELF-Hash, Prüfsumme und Image-SHA gleich, `bootloader.bin`, `partition-table.bin` und `sdkconfig.h` ganz gleich.

Auf dem Stand von AP2 bauen alle neun S3-Konfigurationen: die drei FFVR-Boards mit Feature (ohne neue Warnungen), dazu `project_babble`, `wrooms3`, `wrooms3QIO`, `wrover`, `esp_eye` und `seed_studio_xiao_esp32s3`. Nicht gebaut sind die drei Boards mit klassischem ESP32 (`esp32AIThinker`, `esp32Cam`, `esp32M5Stack`), weil `switchBoardType.py` dafür Komponenten verschiebt. Der Feature-Code wird dort nicht übersetzt; geändert haben sich für sie nur `#if`-Zeilen und CMake-Listen.

Das eingecheckte `sdkconfig` (eye_L) enthält die neuen Zeilen. Gegenprobe per `reconfigure` auf einer Kopie: kconfgen erzeugt dieselbe Datei.

### 17.4 Hardware-Abnahme (von dir)

Das Bench-Werkzeug braucht Setup-Modus: `switch_mode setup` → Neustart → innerhalb der Startverzögerung ein beliebiges Kommando (z. B. `--status`).

**AP1, auf allen drei ESPs, je Rev.5 und Rev.4.5:**
1. `uv run tools/camera_power_bench.py --port COMx --status`
2. Erwartet: `lines.presence` = `present` (Rev.5) bzw. `absent` (Rev.4.5). Die Werte unter `probe_mv` brauche ich, um die Platzhalterschwellen festzulegen.
3. Im Boot-Log die Zeilen `[CAM_LINES] CE/RESET lines: …` und `[CAMERA_STATUS] Last reset: …`.
4. Kamera läuft danach im UVC-Modus wie bisher.

**AP2 (Bench):**
1. Rev.5: `--cycles 5 --trace --out ap2_<rolle>.jsonl`. Erwartet `rail collapsed`, `reinit ESP_OK`, erster Frame, PID gleich. Die Datei brauche ich für die Schwellen und die Aus-Zeit.
2. Rev.4.5: ohne `--force` → `not_supported`, nichts angefasst. Mit `--force` → `not_collapsed` oder `inconclusive`, Kamera läuft danach.
3. DMM-Blick (F16): `--off-ms 5000`, in den 5 s 1V5_Cx messen.
4. Danach `switch_mode uvc`, Neustart, Stream läuft.

**Bekannte Grenze:** `start_streaming` unmittelbar (< 150 ms) vor `camera_power_cycle` senden vermeiden. Der Streaming-Start läuft verzögert in einem Timer und würde mitten in den Zyklus fallen. Eine Absicherung bräuchte `openiris_main.cpp`, das unberührt bleibt.

### 17.5 Ergebnisse auf Hardware: Platine Rev.4/4.5 (2026-09-27)

Eine Platine mit drei ESPs, von mir geflasht (jeweils vorher kompletter Erase). Rev.4/4.5 laut Lüfter-Erkennung auf eye_R (`legacy`, 0 mV) und laut Leitungsprobe auf allen drei ESPs.

| Prüfung | Ergebnis |
|---|---|
| Probe beim Boot | alle drei `absent`: Reset-Knoten 3126–3154 mV mit Pull-up, 0–1 mV mit Pull-down, CE 423–598 mV (offen). Die RTC-Pulls wirken also im ADC-Modus (offene Frage aus Abschnitt 14 für ADC1-Pads geklärt) |
| Boot-Logzeilen | `[CAMERA_STATUS] Last reset: …` und `[CAM_LINES] CE/RESET lines: …` vorhanden, Kamera-Init danach unverändert (`ESP_OK`, OV3660) |
| `get_camera_status` | UVC- und Setup-Modus: `running`, PID 13920 (0x3660), `init ESP_OK` |
| Sperre im UVC-Modus | `camera_power_cycle` (auch mit `force`) → `busy` |
| ohne `force` | `not_supported`, `lines: absent`, nichts angefasst |
| mit `force`, 3 × 20 Zyklen + 3 × 5 + Einzelläufe | 0 Fehlschläge. Dauer 1,16–1,20 s, erster Frame 0–35 ms, PID gleich, `reinit ESP_OK` |
| Rail-Verdikt mit `force` | `not_collapsed` (Kontrolle und Ende beide ≈ 2,8 V) oder `inconclusive` (die Kamera treibt ein Pad low, ≈ 3 mV). Nie `collapsed`, wie in 17.2 erwartet |
| `off_ms` 5000 | gemessen 5001 ms, Zyklus ok |
| UVC nach den Zyklen | im selben Boot per `switch_mode uvc` + `start_streaming`: alle drei 320×320 bei 31,2 fps, wie vorher |
| AP0 `restart_device` | Antwort nach 0,10 s, Gerät weg nach 2,8 s (2 s Verzögerung + Windows-Erkennung) |
| AP0 `start_streaming` | Antwort kommt, UVC startet 150 ms später (mit gespeichertem Modus `uvc`, siehe Korrektur in Abschnitt 1) |

**Befund 1, behoben (`ccca927`):** Im Serial-Task blieben beim Zyklus nur 440 Byte Stack frei; der Reinit läuft tief im Kommandopfad. Der Zyklus läuft jetzt auf einem eigenen Task mit 6 KB (belegt ≤ 3,4 KB), das Kommando wartet synchron. Danach frei: Zyklus-Task ≥ 2760 Byte, Serial-Task ≥ 2312 Byte. Das ist zugleich die Größenangabe für den Worker in AP3.

**Befund 2, nur benannt (F23):** `tools/openiris_device.py` setzt beim Verbinden im Setup-Modus den ESP zurück. pyserial öffnet mit DTR/RTS aktiv, das Tool nimmt sie erst danach zurück; auf USB-Serial-JTAG ist das der Reset (`rst:0x15 USB_UART_CHIP_RESET`). Jede Verbindung von `setup_openiris.py` oder den Tests startet das Gerät also neu. Deshalb stand überall `reset_reason: usb`, und ein direkt nach dem Öffnen gesendetes Kommando kann mit `Write timeout` scheitern. Mein Bench-Werkzeug öffnet jetzt ohne diesen Übergang; `openiris_device.py` habe ich nicht geändert.

**Noch nicht geprüft (braucht Rev.5):** `present`, `collapsed`, Abfall- und Anstiegskurve, DMM-Blick auf 1V5_Cx, Schwellen und Aus-Zeit. Ob der RTC-Pull-up an den ADC2-Pads wirkt, zeigt diese Platine nicht, weil die Kamera D0/D6 treibt. Auf den ADC1-Pads wirkt er (Probe), und es ist derselbe RTC-Mechanismus.

### 17.6 Ergebnisse auf Hardware: Platine Rev.5 (2026-09-27)

Flash wie in 17.5 (jeweils vorher kompletter Erase). Rev.5 laut Lüfter-Erkennung (`rev5`, 3052/1927 mV) und Leitungsprobe.

| Prüfung | eye_L | face | eye_R |
|---|---|---|---|
| Probe | `present`: 3149 mV Pull-up, 2500 mV Pull-down, CE 3149 mV | `present`: 3163 / 2474 / 3163 mV | `present`: 3162 / 2477 / 3162 mV |
| Nachweisläufe mit Spur | 50 + 20 Zyklen, alle `collapsed` | 50 + 20 Zyklen, alle `collapsed` | siehe unten |
| weitere Zyklen ohne Spur | 60, alle `collapsed` | 60, alle `collapsed` | – |
| SCCB-NACKs | 0 | 0 | 5 in 24 Zyklen |

**Messwerte (eye_L und face, 2 × 70 Zyklen mit Spur):**
- Positivkontrolle 3102–3119 mV (einzelne Werte am ADC-Anschlag, jetzt auf 3300 mV begrenzt, `80ba1a6`), Endwert 410–451 mV. Die Endwerte sind Schiene plus Diodenspannung: **V(2V8_Cx) < 0,45 V** am Ende jeder Aus-Zeit.
- Abfall: 0,4 ms nach CE low schon ≈ 2,1 V, beide Pads < 1 V nach ≤ 20 ms (erster Punkt des 20-ms-Rasters), Plateau ab ≈ 60 ms. So schnell fällt die Schiene nur mit aktiver Ausgangsentladung im TP132LC28 oder einer merklichen Last. Für LC15 ist das weiter nur die Vermutung aus Abschnitt 5 (c).
- Anstieg: ≤ 0,4 ms nach der CE-Freigabe zurück auf Kontrollniveau (DS fordert < 5 ms).
- Zyklus 1,16 s, erster Frame 3 ms, PID gleich, `reinit ESP_OK`; Zyklus-Task ≥ 2840 Byte Stack frei.

**Schwellen:** unverändert, jetzt belegt statt Platzhalter. Probe: `present` ab 1200 mV mit Pull-down (gemessen ≥ 2474), `absent` bis 400 mV (gemessen ≤ 1). Rail-Check: Kontrolle ab 2600 mV (gemessen ≥ 3102), `collapsed` bis 1000 mV (gemessen ≤ 451).

**Aus-Zeit:** Für 2V8_Cx reichen 20–60 ms. Die 500 ms Mindest-Aus-Zeit bleiben trotzdem, weil 1V5_Cx nicht beobachtbar ist; der DMM-Blick (F16) steht noch aus.

**Recovery nachgewiesen:** Zweimal war die Kamera an eye_R nach einem Hänger verklemmt. Ein ESP-Reset half nicht (Boot: `ESP_ERR_NOT_SUPPORTED`, PID 0), `camera_power_cycle` holte sie jeweils in 1,15 s zurück (PID 0 → 0x3660, erster Frame 0–3 ms).

**Befund 3, eye_R (F25):**
- NACKs an wechselnden Registern (`3006`, `350a`, `3821`, `3a00`, `3a19`, `5000`), gelegentlich 8 s ohne Frame nach dem Reinit.
- Etwa alle 5–25 Zyklen hängt der Reinit ganz (Befund 4).
- Die Positivkontrolle liegt dauerhaft am ADC-Anschlag, die Pads klemmen also höher als bei den anderen beiden.
- 10 normale Boots ohne Power-Cycle laufen sauber (0 NACKs).
- Ein Lüfter auf 100 % (keine PWM-Flanken) ändert nichts.
- Dein Hinweis: Das Flachbandkabel dieser Kamera ist stärker geknickt. Das passt. Ein angeknackster Leiter für Masse oder DOVDD bricht beim Anlaufstrom nach dem Einschalten ein, beim langsamen Kaltstart des ganzen Boards kaum. *Vermutung* bis zum Tauschversuch.

**Befund 4, IDF-Fehler (F24):** Hält die Kamera nach einem NACK SDA fest, wartet `s_i2c_send_commands` in ESP-IDF v5.4.2 ohne Timeout auf den freien Bus (`i2c_master.c:541-545`). Der Task hängt für immer, der Task-Watchdog meldet `cam_power` auf CPU 0, das Gerät antwortet nicht mehr.
- Upstream ist das behoben (`release/v5.5` und `master`: Software-Timeout, dann FSM-Reset und Bus-Freigabe), `release/v5.4` hat es noch nicht.
- ⚠ Das korrigiert Abschnitt 7: Die Busfreigabe des IDF greift erst beim *nächsten* Transfer und wird in diesem Fall nie erreicht.
- Betrifft alle Boards, auch den normalen Boot; mit dem Power-Cycle wird es nur häufiger ausgelöst, sobald eine Kamera grenzwertig ist.

**F25 geklärt, Tauschversuch:** Kamera samt Kabel von eye_R und eye_L getauscht.
- NACKs und Hänger **folgen der Kamera** mit dem geknickten Kabel: an eye_L 4 NACKs und ein Hänger in 15 Zyklen, an eye_R mit der guten Kamera 0 NACKs in 40 Zyklen.
- Die Positivkontrolle am Anschlag **bleibt am eye_R-Platz**, gehört also zur Board-Seite, am ehesten zur ADC-Kalibrierung dieses ESP. Für das Verdikt ist das dank Begrenzung egal.

**F24 gelöst (A), auf Hardware geprüft:** mit I2C-Fix auf allen drei ESPs.

| ESP | Kamera | Zyklen | NACKs | Bus-Timeouts (vorher Hänger) | Fehlschläge | Hänger |
|---|---|---|---|---|---|---|
| eye_L | mit geknicktem Kabel | 60 | 14 | 9 | 6 × `first_frame` | 0 |
| eye_R | gut | 30 | 0 | 0 | 0 | 0 |
| face | gut | 30 | 0 | 0 | 0 | 0 |

Die 6 Fehlschläge: `esp_camera_init` meldet `ESP_OK`, obwohl einzelne Registerschreibvorgänge scheiterten, danach kommen 8 s keine Frames. Die Prüfung auf den ersten Frame fängt das ab, und der jeweils nächste Zyklus holt die Kamera zurück. Das bestätigt das Erfolgskriterium aus Abschnitt 8 und begründet die Wiederholung in AP4.

### 17.7 Nächste Schritte

AP1/AP2 sind auf Rev.4.5 und Rev.5 gelaufen, die Nachweisläufe sind gemacht (17.5, 17.6). Vor AP3/AP4 offen:
1. ~~F24~~ gelöst (A), ~~F25~~ geklärt (Kabel).
2. **F16:** DMM-Blick auf 1V5_Cx während `--off-ms 5000`, steht noch aus.
3. Weiter mit AP3 (Gate, Worker) und AP4 (Recovery). Die Kamera mit dem geknickten Kabel ist dafür ein guter Prüfling: Sie erzeugt reproduzierbar echte Ausfälle.

---

# Anhang A: Kontrollbericht Lüfterpfad (Stand 1, abgeschlossen)

Abgeschlossen durch deine Entscheidung vom 2026-09-27. Es gibt keinen Änderungsauftrag. Der Bericht bleibt hier als Protokoll stehen.

| # | Punkt | Status |
|---|---|---|
| 1 | Pull-up nach `gpio_reset_pin` | **anders gelöst**: `gpio_reset_pin` wird auf GPIO6 nie aufgerufen, Pulls bleiben durchgehend aus |
| 2 | Schwellen der Erkennung | **teilweise**: A-Schwelle 1500 mV ok, aber B steckt zweimal in der UND-Kette |
| 3 | Abnahmefenster gegen Klassifikator | **erfüllt im Klassifikator** (400 mV); numerische Abnahmefenster gibt es im Repo nicht |
| 4 | `set_fan_raw_duty` | **nicht erfüllt**: keine Rückfallzeit; Warnung nur im Log |
| 5 | `minPercent` auf Rev.5 | **teilweise**: 10 %, zur Laufzeit änderbar; Herleitung nicht belegt |
| 6 | Klemmlogik | **erfüllt** |
| 7 | Legacy-Pfad unverändert | **im Kern erfüllt** (LUT, Frequenz, kein Kickstart, Default-Min); zwei Verhaltensabweichungen |
| 8 | API und Werkzeug | **API erfüllt, Werkzeug nicht** |
| 9 | Pin-Parken eye_L/face | **nicht erfüllt** |
| 10 | Messergebnisse dokumentiert | **nicht erfüllt** |

**A.1 Pull-up nach `gpio_reset_pin`: anders gelöst.**
- `gpio_reset_pin` kommt nur in `CameraManager.cpp:119-133` (ESP_EYE/CAM_BOARD) und `LEDManager.cpp:58` (Debug-LED, auf FFVR aus) vor.
- Pfad auf GPIO6:
  1. `detectFanRevision` (`FanManager.cpp:39/50`),
  2. ADC-Init schaltet die Pulls ab (IDF `adc_oneshot.c:322-326`),
  3. Aufladen über den RTC-Treiber (`FanRevision.cpp:131-133`),
  4. `releasePin()` per `gpio_config` ohne Pulls (`FanRevision.cpp:79-89`).
- Digitaler Fallback ebenfalls ohne Pulls (`FanRevision.cpp:186-196`).
- Begründung als Kommentar in `FanRevision.cpp:75-77`.
- Nicht verifiziert: der Pull-Zustand zwischen Chip-Reset und `FanManager::setup`. Dieses Fenster gab es vorher identisch.

**A.2 Schwellen: teilweise.**
- A-Schwelle 1500 mV (`FanRevision.cpp:46, 93`); das B-Plausibilitätsband erzeugt nur eine Warnung (`:56-57, 95-99`).
- B steckt aber in der UND-Kette:
  - Rev.5: `b_mv < a_mv - 200` (`:93`),
  - Legacy: `a_mv < 400 && b_mv < 400` (`:103`).
- Folge auf einem alten Board mit B ≥ 400: Unknown, trotzdem Legacy-Kurve, aber `fallback_active`, eine ERROR-Zeile je Setzen (`FanManager.cpp:193`), und der Test schlägt fehl (`tests/test_fan.py:78-86`).

**A.3 Abnahmefenster: im Klassifikator erfüllt.**
- Die Legacy-Schwelle ist 400 mV (`FanRevision.cpp:47, 103`).
- Numerische Fenster gibt es im Repo nicht; der Test prüft nur das Verdikt.

**A.4 `set_fan_raw_duty`: nicht erfüllt.**
- Nur `ESP_LOGW` (`FanManager.cpp:276`), keine Rückfallzeit (`:267-285`), nicht persistiert (`FanManager.hpp:60`).
- Über die API unsichtbar (`FanManager.hpp:18-31`).

**A.5 `minPercent` Rev.5: teilweise.**
- 10 % (`boards/facefocusvr/eye_R:77`) entsprechen 2,93 V (`FanCurve.cpp:106-109`).
- Kconfig-Default und Fallback sind 25 (`main/Kconfig.projbuild:295`, `FanCurve.cpp:12-13`).
- Änderbar über `set_fan_tuning` (`device_commands.cpp:216-239`, `ProjectConfig.cpp:142-149`, `FanManager.cpp:217`).
- Herleitung nicht belegt.

**A.6 Klemmlogik: erfüllt.**
- `device_commands.cpp:109-115` fragt `isPercentAllowed`.
- `ProjectConfig.cpp:119-125` klemmt nur auf 0…100.
- Außerhalb des FanManagers gibt es keine Kopien mehr.

**A.7 Legacy: im Kern erfüllt.**
- LUT byteidentisch (101 Werte, per Skript verglichen: alt `FanManager.cpp@HEAD~1:31-42`, neu `FanCurve.cpp:37-48`).
- Frequenz aus `CONFIG_FAN_PWM_FREQ` = 20000 (`FanCurve.cpp:68`, `eye_R:71`).
- Kein Kickstart (`FanCurve.cpp:86-89`); Default-Min aus `CONFIG_FAN_PWM_DUTY_MIN` = 5 (`FanCurve.cpp:93`, `eye_R:72`).
- Abweichungen:
  - (a) Die Erkennung treibt GPIO6 pro Versuch 5 ms high, bis zu 3 Versuche (`FanRevision.cpp:66, 131-133`; dokumentiert in `FanRevision.hpp:45-46`).
  - (b) Der NVS-Override `fan_min_pct` greift auch auf Legacy (`FanManager.cpp:215-221`).

**A.8 API und Werkzeug.**
- API ok (`device_commands.cpp:153-161`).
- Das Werkzeug liest nur `fan_pwm_duty_cycle` und bietet fest „(0-100)" an (`tools/setup_openiris.py:198-212, 397, 408`).

**A.9 Pin-Parken: nicht erfüllt.** Auf eye_L und face wird der FanManager nicht gebaut (`components/FanManager/CMakeLists.txt:6-12`), und kein anderer Code fasst GPIO6 an.

**A.10 Messwerte: nicht erfüllt.** Es gibt nur Modell- und Rechenwerte (`FanRevision.cpp:42-45`, `FanCurve.cpp:106-113`, `main/Kconfig.projbuild:287-289`).

**A.11 Nebenbefunde.**
- `FanManager.cpp:93-96`: Der Kommentar sagt „APB, always", der Code nutzt `LEDC_AUTO_CLK` (wie vorher).
- `FanManager::getFanDutyCycle` liefert den gespeicherten, nicht den geklemmten Wert (`FanManager.cpp:205-208`).
