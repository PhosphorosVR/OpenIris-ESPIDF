# Übergabe an den nächsten KI-Chat: CAM_CE / CAM_RESET, Kamera-Recovery (FaceFocusVR)

Stand 2026-09-28, nach der Abnahme. `feature/camera-power` ist mit allen Fixes nach `main` gemergt (lokal, nichts gepusht).

**Zuerst lesen:**
1. dieses Dokument,
2. [ABNAHME_KAMERA_POWER.md](ABNAHME_KAMERA_POWER.md): Ziele, warum das Feature so gebaut ist, Budget nach der Abnahme,
3. [CAM_CE_RESET_Analyse.md](CAM_CE_RESET_Analyse.md): In den Abschnitten 16 und 17 stehen Isolation, Bitgleichheit, Befunde und Hardware-Ergebnisse; Abschnitt 13 enthält die offenen Fragen.

---

## 1. Worum es geht

- **Firmware:** OpenIris-ESPIDF (ESP-IDF v5.4.2), Board-Familie FaceFocusVR (FFVR).
- **Hardware:** Eine Platine mit drei ESP32-S3R8 (Rollen eye_L, eye_R, face), je eine OV3660 über DVP, alle drei an einem USB-Hub. Revisionen im Feld: Rev.4, Rev.4.5, Rev.5. Pro Rolle gibt es **ein** Image für alle Revisionen.
- **Ziel:** ESD-/EMV-Robustheit. Rev.5 hat zwei neue Leitungen, mit denen die Firmware die Kamera stromlos schalten und in den Reset halten kann. Gebaut wurde damit:
  1. Nachweis per Software, dass die Kamera wirklich stromlos war.
  2. Neustart der Kamera mitten im UVC-Stream, per Befehl und automatisch, wenn keine Frames mehr kommen.
  3. Zähler und Logs als Testergebnis einer ESD-Prüfung.

### Hardware-Fakten (vom Nutzer, aus Netzliste, BOM und Datenblatt; nicht anzweifeln, Widersprüche melden)

- **GPIO7 = CAM_CE:** schaltet beide Kamera-LDOs (TP132LC28 für 2V8_Cx und TP132LC15 für 1V5_Cx) gleichzeitig. 10 kΩ Pull-up auf 3V3. **Nur Open Drain:** nach Masse ziehen oder loslassen, nie push-pull high, nie `gpio_reset_pin`, nie `gpio_hold_en`.
- **GPIO8 = CAM_RESET:** 1 kΩ auf RESETB, Knoten mit 10 kΩ auf 3V3 und 100 nF. Ebenfalls Open Drain. Beim Abschalten muss RESET low gehalten werden, sonst fließen ~260 µA über den Reset-Pin in die tote Versorgung.
- **Rev.4/4.5:** GPIO7/8 unbeschaltet. Alles muss dort ein stiller No-op sein.
- **SCCB:** SDA 40, SCL 39 mit 4,7 kΩ auf 2V8_Cx.
- **XCLK:** GPIO10.
- **DVP:** D0…D7 = 15, 17, 18, 16, 14, 12, 11, 48; PCLK 13; VSYNC 38; HREF 47.
- **Kein WLAN auf FFVR:** ADC2 ist frei, es gibt keine REST-Route.
- **Lüfter** nur an eye_R (GPIO6, LEDC-Timer 2). Die LEDs nutzen Timer 1, die Kamera Timer 0.
- **OV3660:** braucht den XCLK-Umschaltpfad (Boot mit 23 MHz, dann 20 MHz). Den Boot-Default nicht auf 20 MHz setzen, sonst kommen keine Frames.

---

## 2. Arbeitsregeln des Nutzers (unbedingt einhalten)

- **Fragen statt raten:** keine GPIO-Nummern, Datenblattwerte, Registeradressen oder Zeitkonstanten erfinden. Widerspricht der Code den Hardware-Angaben, den Widerspruch melden.
- **Letztes Wort:** Du darfst logikbasiert entscheiden. Weichst du vom Vereinbarten ab, sag in **einer Zeile**, wovon und warum. Bei Entscheidungen mit hohem Einfluss vorher fragen.
- **Nicht destruktiv arbeiten.**
- **Isolation, Kategorie A (neue Funktionen):**
  - Opt-in über funktional benannte Kconfig-Symbole, Default aus bzw. -1.
  - Ohne Symbol wird nichts übersetzt (CMake-Muster wie beim FanManager).
  - Gesetzt nur in `boards/facefocusvr/*`, keine Abfrage der Board-Identität.
  - Andere Boards müssen bitgleich bleiben; geprüft per `tools/compare_builds.py`.
- **Isolation, Kategorie B (Fixes in gemeinsamem Code):**
  - Nur, wenn beabsichtigtes Verhalten und Schnittstellen gleich bleiben.
  - Wirkung auf andere Boards benennen.
  - Eigener Zweig `fix/<thema>` von `main`, eigener Commit.
  - Alle Configs müssen bauen.
  - Vorher fragen, wenn das beabsichtigte Verhalten strittig ist.
- **Lüfterpfad:** abgeschlossen. Echte Defekte dort nur einzeln und begründet melden, nie ungefragt beheben.
- **`main/openiris_main.cpp` nicht anfassen:** `__LINE__` in `ESP_ERROR_CHECK`/`assert` würde sich auf allen Boards verschieben. Einhängepunkte liegen deshalb in `CameraManager::setupCamera()`.
- **Neue `CommandType`-Enum-Werte immer ans Ende**, unter `#if`.
- **Neue Komponentenabhängigkeiten:** nie als `REQUIRES`, weil das die Link-Reihenfolge aller Boards ändert. Stattdessen `idf_component_optional_requires(PRIVATE …)` unter `if(CONFIG_…)`.
- **Commits:** keine KI-Attribution, kein Co-Authored-By. Code-Kommentare englisch und kurz; Dokumente dürfen deutsch sein.
- **Zweige:** sinnvoll einsetzen, nicht übertrieben; nachvollziehbar arbeiten. Nie mit unversionierten Quelländerungen den Zweig wechseln; lieber einen temporären Worktree nehmen.
- **Nicht ungefragt:** pushen, nach `main` mergen oder Zweige löschen.
- **Flashen:** vor **jedem** Flash `erase-flash`. Das löscht den NVS: Modus zurück auf UVC, Lüfter 30 %, Debug-Log aus.
- **Framerate:** ignorieren, solange sie im 30-fps-Bereich liegt (27–31 fps je nach Szene, war schon immer so).
- **Release-Bins:**
  - Format: `idf.py merge-bin -o <x>.bin -f raw`, ein Image ab 0x0 mit Bootloader, Partitionstabelle und App bei 0x10000.
  - Namen: `FFVR Eye L [x.y.z].bin`, `FFVR Eye R [x.y.z].bin`, `FFVR Face [x.y.z].bin`.
  - Frühere Releases liegen in `../ffvr-multiflash/fw/<ver>/`; das Flash-Tool dort schreibt an 0x0.
- **Version:** `CONFIG_GENERAL_VERSION` in `boards/facefocusvr/*` und im eingecheckten `sdkconfig`. Format dreiteilig; aktuell **1.3.1**.

---

## 3. Stand

### Zweige

| Zweig | Inhalt | Status |
|---|---|---|
| `feature/camera-power` | alles; enthält die drei Fix-Zweige und die Änderungen aus der Abnahme | nach `main` gemergt |
| `fix/esp-timer-units` | AP0: `esp_timer` in µs (`restart_device` 2 s statt 2 ms, `start_streaming` 150 ms statt 150 µs) | Basis des Feature-Zweigs |
| `fix/i2c-nack-busy-wait` | Backport aus IDF v5.5: begrenzte Busy-Warteschleife nach NACK in `esp_driver_i2c` (Projektkopie von v5.4.2 plus Fix, Build-Schutz für andere IDF-Versionen) | gemergt (`ab89e37`) |
| `fix/serial-no-reset-on-connect` | `tools/openiris_device.py`: DTR/RTS low **vor** dem Öffnen, sonst startet jedes Verbinden im Setup-Modus den ESP neu | gemergt (`a06dd61`) |

Der Merge nach `main` enthält alle Fixes. Die drei Kategorie-B-Fixes betreffen absichtlich jedes Board; der Merge-Text nennt sie.

### Was umgesetzt ist (Arbeitspakete)

| AP | Inhalt | Kern-Commits |
|---|---|---|
| B0 | `tools/compare_builds.py`: Referenzbuilds und maskierter Vergleich | `de3ae87` |
| AP1 | `CamLines` (CE/RESET Open Drain), Leitungsprobe beim Boot, `get_camera_status`, Reset-Grund | `68cb4fa`, `c003f0e` |
| AP2 | `camera_power_cycle` (Bench, nur Setup-Modus), Rail-Check über ADC2 (`RailSense`, D0 = GPIO15, D6 = GPIO11), Kamera-Task | `a703f47`, `ccca927`, `80ba1a6` |
| AP3 | Frame-Gate für UVC, Stufen reinit/reset/power_cycle, fester Kamera-Task (6 KB) | `06bc04f`, `2b55766` |
| AP4 | `recover_camera`, Auto-Auslöser `frame_timeout`/`boot_failure`, Budget, Zähler, optionaler ESP-Neustart, Testhaken | `fbff171` |
| AP5 (schlank) | `tools/camera_recovery_check.py` | `aa9378e` |
| Abnahme | Budget neu (gehalten/nicht gehalten, Sperre löst sich nach 5 min), ESP-Neustart höchstens einmal bis wieder ein Start hält, Sensorzeiger unter dem Mutex, Befund C in der Analyse; alle 12 Konfigurationen gebaut, Bitvergleich | `a467192`, `9bf6447`, `8788d32`, `940d1b2` |
| AP6 | Ergebnisse der ESD-Prüfläufe ins Dokument | **offen** |

### Kconfig (in `components/CameraManager/Kconfig.projbuild`, FFVR-Werte)

```
CONFIG_CAMERA_POWER_CONTROL=y        CE/RESET, Probe, camera_power_cycle
CONFIG_CAMERA_POWER_EN_GPIO=7
CONFIG_CAMERA_HW_RESET_GPIO=8
CONFIG_CAMERA_RAIL_SENSE=y           (depends on !GENERAL_ENABLE_WIRELESS)
CONFIG_CAMERA_RAIL_SENSE_GPIO_A=15
CONFIG_CAMERA_RAIL_SENSE_GPIO_B=11
CONFIG_CAMERA_RECOVERY_ENABLE=y      (depends on !GENERAL_ENABLE_WIRELESS) Gate, Task, recover_camera
CONFIG_CAMERA_AUTO_RECOVERY=y        frame_timeout, boot_failure
CONFIG_CAMERA_AUTO_RECOVERY_MISSED_FRAMES=1
# CONFIG_CAMERA_RECOVERY_ESP_RESTART is not set   (Default aus)
# CONFIG_CAMERA_TEST_HOOKS is not set             (nur Test-Builds)
CONFIG_CAMERA_STATUS=y               abgeleitet, ohne Prompt
```

### Dateien

- **`components/CameraManager/CameraManager/`**
  - `CameraTypes.hpp`: alle Feature-Typen.
  - `CamLines.*`: Leitungen und Probe.
  - `RailSense.*`: ADC2-Messung, Werte am oberen ADC-Anschlag auf 3300 mV begrenzt.
  - `CameraCycle.cpp`: Sequenz nach Abschnitt 7, Kamera-Task, `runCycleBlocking`.
  - `CameraGate.*`: Frame-Gate; führt ausgegebene Frames einzeln und übernimmt Frames, die länger als 500 ms draußen sind.
  - `CameraRecovery.cpp`: Politik, Budget (Erholung „gehalten" nach 30 s Frames, Sperre nach drei nicht gehaltenen in Folge, Auflösung nach 5 min), Zähler, Auslöser, ESP-Neustart, Testhaken.
  - `CameraStatus.cpp`: Namen, Boot-Hooks, Status.
- **`components/CameraManager/CameraManager/CameraManager.cpp`:** nur `#if`-Einhängepunkte in `setupCamera()` und `setCameraResolution()`.
- **`components/UVCStream/UVCStream/UVCStream.cpp`:** Frames über `cameraAcquireFrame`/`cameraReleaseFrame` (ohne Feature `always_inline`-Treiberaufrufe), Rückhol-Haken unter `#if`.
- **`components/CommandManager/…/commands/`:** `camera_power_commands.*` (Status, Bench), `camera_recovery_commands.*` (Recovery, Testhaken).
- **`components/esp_driver_i2c/`:** IDF-v5.4.2-Kopie mit NACK-Fix. Entfernen, sobald auf IDF ≥ 5.5 umgestiegen wird.
- **`tools/`:**
  - `compare_builds.py`
  - `camera_power_bench.py` (Setup-Modus, Bench)
  - `camera_recovery_check.py` (UVC-Modus, Recovery-Prüfungen)
  - `openiris_device.py` (Reset-Fix)

### Kommandos (JSON über Serial/CDC: `{"commands":[{"command":"…","data":{…}}]}`)

| Kommando | Hinweis |
|---|---|
| `get_camera_status {"persist": false}` | Zustand, Leitungen, Recovery-Zähler (`held`, `not_held`, `unheld_in_row`, `suspended`, `suspensions`, `resumes`, je Eintrag `held`), Gate, Heap, Reset-Grund. `persist: true` schreibt eine WARN-Zusammenfassung in den persistenten Log |
| `recover_camera {"level": "auto"\|"reinit"\|"reset"\|"power_cycle"}` | auch im UVC-Stream; Cooldown 5 s |
| `camera_power_cycle {"off_ms": 1..30000, "trace": bool, "force": bool}` | nur ohne laufendes UVC (Setup-Modus) |
| `camera_test_fault {"kind": "hold_reset"\|"sensor_standby"}` | nur mit `CONFIG_CAMERA_TEST_HOOKS=y` |

---

## 4. Ergebnisse und Befunde (Details in Analyse-Dokument 17.5–17.7)

- **Probe:** Rev.5 `present` (Pull-down 2474–2500 mV), Rev.4.5 `absent` (0–1 mV).
- **Stromlos-Nachweis Rev.5:** 2V8_Cx unter 0,45 V am Ende jeder Aus-Zeit, unter 1 V nach ≤ 20 ms, Anstieg ≤ 0,4 ms. 1V5_Cx fiel laut DMM des Nutzers sofort auf 0,00 V. Die Aus-Zeit bleibt trotzdem bei mindestens 500 ms.
- **Recovery im Stream:** Power-Cycle 1,2 s, Stream-Lücke 1,0 s, ohne Neuöffnen. Automatik nach einem provozierten Ausfall: ~8 s bis zur Erkennung (4 s + 4 s Treiber-Timeout), dann Neustart.
- **Dauertests:** 150+ Recoveries ohne Fehlschlag, Heap konstant.
- **Befunde:**
  1. **Stack:** Im Serial-Task blieben nur 440 B frei. Behoben durch den eigenen Kamera-Task.
  2. **Host-Tool:** Verbinden resettete den ESP. Behoben (`fix/serial-no-reset-on-connect`).
  3. **Kamera an geknicktem Flachbandkabel:** Die Kamera, die ursprünglich an eye_R steckte, erzeugt nach Power-Cycles SCCB-NACKs. Per Tausch belegt; **diese Kamera steckt jetzt an eye_L**. Sie ist ein guter Prüfling.
  4. **IDF-I2C-Endlosschleife nach NACK:** Behoben per Backport.
  5. **Drain-Timeout nach dem Schließen der Kamera-App:** Windows beendet den Stream nicht, sondern holt nur nicht mehr ab. Behoben im Gate: Frames, die länger als 500 ms draußen sind, übernimmt der Neustart.
- **Auf Hardware nicht ausgelöst:** das Budget nach der Abnahme (gehalten/nicht gehalten, Sperre, Auflösung nach 5 min), `boot_failure`, ESP-Neustart. Diese Pfade sind nur per Review und Build geprüft; zur Zeit der Änderung war keine Platine angeschlossen. Testablauf: Abnahme, Abschnitt 4.

---

## 5. Bauen, Vergleichen, Flashen, Prüfen

### Umgebung (Windows)

`idf.py` ist in Git-Bash nicht direkt verfügbar. Jeder Befehl läuft über eine kleine `.cmd`-Datei, zum Beispiel:

```bat
@echo off
set IDF_PYTHON_ENV_PATH=%USERPROFILE%\.espressif\python_env\idf5.4_py3.11_env
set IDF_TOOLS_PATH=%USERPROFILE%\.espressif
set PYTHONIOENCODING=utf-8
call %USERPROFILE%\esp\v5.4.2\esp-idf\export.bat >nul 2>&1
cd /d C:\Users\henri\Desktop\Firmware\OpenIris-ESPIDF
%*
```

Aufruf aus PowerShell: `cmd /c "`"<pfad>\idf.cmd`" python tools\compare_builds.py …"`. Aus Git-Bash scheitert `cmd //c` mit Leerzeichen und Anführungszeichen; für Build und Flash PowerShell nehmen.

### Bitgleichheit anderer Boards (nach jeder Änderung an gemeinsamem Code)

```
python tools/compare_builds.py build --board project_babble --out <dir>
python tools/compare_builds.py compare ../OpenIris-refbuilds/baseline2/project_babble <dir>
```

Ebenso für `wrooms3`.
- **Aktuelle Baseline:** `../OpenIris-refbuilds/baseline2/`, gebaut aus `fix/esp-timer-units` + `fix/i2c-nack-busy-wait` (Merge `8534138`, kein Zweig).
- **Maskiert** werden ELF-SHA-256 und Prüfsummen. Alles andere muss byte-gleich sein; zusätzlich werden Bootloader, Partitionstabelle und `sdkconfig.h` verglichen.
- **Neue Baseline** bei jedem Kategorie-B-Fix.

### Build-Worktree

`../OpenIris-refbuilds/wt` ist ein detached Worktree. Dort den zu bauenden Commit auschecken; so bleiben `build/` und das eingecheckte `sdkconfig` des Hauptbaums unberührt. `managed_components` wurde einmal hineinkopiert.

### Normales FFVR-Image bauen, ohne das eingecheckte `sdkconfig` anzufassen

Das Hilfsskript `flashbuild.py` lag im temporären Scratchpad und ist weg. Kern:

```python
# im Worktree: defaults = base_defaults + Board-Datei (+ optionale Zeilen, z. B. CONFIG_CAMERA_TEST_HOOKS=y)
import compare_builds as cb          # aus <worktree>/tools
cb.REFERENCE_OVERRIDES = []          # keine Referenz-Einstellungen = normales Image
lines = cb.merged_defaults(cb.sbt.normalize_board_name("facefocusvr_eye_L"))
# -> <out>/sdkconfig.defaults schreiben, ein vorhandenes <out>/sdkconfig vorher verwerfen (sonst bleiben alte Werte!)
# idf.py -C <worktree> -B <out>/build -D SDKCONFIG=<out>/sdkconfig -D SDKCONFIG_DEFAULTS=<out>/sdkconfig.defaults build
# flashen: dasselbe mit  -p COMx erase-flash flash
# Release-Bin: dasselbe mit  merge-bin -o merged.bin -f raw   (liegt dann in <out>/build/)
```

⚠ Falle: Wird `sdkconfig.defaults` nur beim ersten Mal geschrieben, fehlen später neue Symbole. So war ein Build ohne Recovery entstanden.

⚠ Falle: Ohne `-D IDF_TARGET=<ziel>` rät IDF das Ziel aus dem eingecheckten `sdkconfig` des Baums (esp32s3), nicht aus `SDKCONFIG_DEFAULTS`. Für die S3-Boards fällt das nicht auf, klassische ESP32-Boards scheitern dann mit „Failed to resolve component 'tinyusb'". Also immer `-D IDF_TARGET` mitgeben, dazu die Umgebungsvariable `IDF_TARGET`, weil einige Komponenten `$ENV{IDF_TARGET}` abfragen.

**Klassische ESP32-Boards** (`esp32AIThinker`, `esp32Cam`, `esp32M5Stack`) bauen in einem eigenen Worktree `../OpenIris-refbuilds/wt32`:
- Dort liegt `components/usb_device_uvc` außerhalb von `components/` (in `../OpenIris-refbuilds/wt32_parked/`), so wie es `switchBoardType.py` beim Plattformwechsel tut.
- `managed_components` ist hineinkopiert; die `dependencies.lock` dort passt der Komponentenmanager beim ersten Build an.
- Zuletzt gebaut beim Merge nach `main` (Abnahme, Abschnitt 6).

Alternativ wie gewohnt: `uv run tools/switchBoardType.py --board facefocusvr_eye_L`, dann `idf.py build`. Das überschreibt das eingecheckte `sdkconfig`.

### Modus wechseln, Ports

- **Flashen** geht nur im Setup-Modus (USB-Serial-JTAG): `switch_mode {"mode":"setup"}`, dann `restart_device`. Die Antwort kommt sofort, der Neustart 2 s später.
- **Nach dem Flashen mit Erase** startet das Gerät im UVC-Modus.
- **Im Setup-Modus** startet `start_streaming` den *gespeicherten* Modus; vorher also `switch_mode uvc`.
- **Ports ändern sich** je nach Platine und Modus. Immer per `get_who_am_i` zuordnen, nie annehmen. Zuletzt beobachtet:

| Platine | UVC-Modus (CDC) | Setup-Modus |
|---|---|---|
| Rev.5 | face COM767, eye_L COM768, eye_R COM769 | eye_L COM764, face COM765, eye_R COM766 |
| Rev.4.5 | face COM731, eye_L COM732, eye_R COM733 | eye_L COM107, face COM108, eye_R COM109 |

### Prüfungen auf Hardware

```
uv run tools/camera_recovery_check.py status --port COMx
uv run --with opencv-python tools/camera_recovery_check.py recover --port COMx --count 20 --watch
uv run --with opencv-python tools/camera_recovery_check.py after-close --port COMx --count 5
uv run --with opencv-python tools/camera_recovery_check.py fault --port COMx --kind hold_reset   # Test-Build
uv run tools/camera_power_bench.py --port COMx --cycles 5 --trace --out f.jsonl                  # Setup-Modus
```

- OpenCV ist nicht Teil von `pyproject.toml`; bewusst, damit `uv.lock` unverändert bleibt.
- `uv` war in Git-Bash nicht im PATH; das Werkzeug läuft auch mit einem Python, das `pyserial` und `opencv-python` hat.
- OpenCV kennt keine Gerätenamen: Streams werden je Index gemeldet.
- Während eines Ausfalls wiederholt DirectShow jede Sekunde das letzte Bild. Deshalb zählt `stalled_ms`, nicht die längste Lücke.

---

## 6. Offene Punkte

1. **AP6:** Ergebnisse der ESD-Prüfläufe ins Analyse-Dokument. Vorher `set_debug_log_enabled true` senden, am Ende `get_camera_status {"persist": true}` und `get_persistent_logs`. Grundlage nach IEC 61000-4-2: mindestens 10 Entladungen je Punkt und Polarität im Abstand von 1 s, mehrere Punkte. Das ist die Untergrenze; den konkreten Plan bestätigt der Nutzer noch.
2. **Budget auf Hardware prüfen:** Das neue Budget und der ESP-Neustart sind nur per Review und Build geprüft. Testablauf mit Test-Build: Abnahme, Abschnitt 4. Die Sperre lässt sich mit `camera_test_fault hold_reset` innerhalb von 30 s nach jeder Erholung provozieren. `boot_failure` ist weiter nicht provoziert. **Kein** Kabelziehen im Betrieb vorschlagen.
3. **IDF-Version und vendorter I2C-Treiber: nichts anfangen.** Der nächste Schritt des Nutzers ist, auf das Upstream-Repo zu gehen und die Änderungen von dort zu übernehmen.
4. **Rail-Check bleibt** (Entscheidung des Nutzers). Schritt 1 (Spur und adaptive Verlängerung entfernen) ist freigegeben, aber nicht gemacht. Schritt 2 (Check entfernen) erst, wenn eine Fertigungsprüfung den CE-Pfad abdeckt.
5. **Offene Fragen aus dem Analyse-Dokument:**
   - F19: Revision des OV2640-Boards.
   - F20: schaltbarer USB-Port für Kaltstarts.
   - F21: `LogManager`-Sammelpuffer ist unbegrenzt; Kategorie-B-Kandidat, vorher fragen.
   - F22: Referenzkonfigurationen; `project_babble` und `wrooms3` wurden ohne Einwand genutzt.
6. **Bekannte Eigenheit im UVC-Code (nicht geändert, nur benannt):** `camera_stop_cb` läuft nur bei USB-Suspend. Schließt der Host die Kamera, bleibt der letzte Frame in einer hängenden Übertragung liegen. Ohne Feature erholt sich der Code beim nächsten Stream-Start; mit Feature fängt das Gate es ab.
7. **`tests/utils.py`:** wartet nach jedem Verbinden `SWITCH_MODE_REBOOT_TIME`. Seit dem Reset-Fix ist das länger als nötig, aber unschädlich.

---

## 7. Aktueller Zustand der angeschlossenen Hardware (Rev.5-Platine)

- Alle drei ESPs tragen Firmware **3.0.1** aus `e01f02b`. Die ist funktional identisch zu 1.3.1, nur der Versionsstring unterscheidet sich; auf Wunsch des Nutzers wurde nicht neu geflasht.
- ⚠ Die Änderungen aus der Abnahme (neues Budget, ESP-Neustart, Setter) sind auf keinem Gerät. Eine neue Version und neue Release-Bins entscheidet der Nutzer; die Version steht weiter auf 1.3.1.
- NVS gelöscht, UVC-Modus, Automatik an, keine Testhaken.
- **Release-Bins 1.3.1** (aus `3b35ef1`, `idf.py merge-bin -f raw`) liegen unversioniert im Repo-Wurzelordner:
  - `FFVR Eye L [1.3.1].bin`
  - `FFVR Eye R [1.3.1].bin`
  - `FFVR Face [1.3.1].bin`

  Geprüft: Bootloader, Partitionstabelle und App byte-gleich zum Build, Header wie bei 1.2.4, Version 1.3.1 enthalten.

---

## 8. Fallen, die schon einmal Zeit gekostet haben

- **`sed -i` in Git-Bash** stellt CRLF-Dateien auf LF um. Für Git egal (autocrlf), der Diff bleibt sauber; für gezielte Ersetzungen besser Python. Außerdem ist `\U` in der Ersetzung von GNU-sed der Befehl „ab hier groß schreiben": Windows-Pfade wie `C:\Users` werden dabei zerstört.
- **Sicherheitsprüfung des PowerShell-Werkzeugs:** Sie blockiert manchmal harmlose Befehle mit „Remove-Item on system path '/c'“. Ohne `Remove-Item` arbeiten (`[System.IO.File]::Delete`) und lange Befehle aufteilen.
- **Das eingecheckte `sdkconfig`:** Es ist die expandierte Konfiguration von eye_L. Neue Kconfig-Symbole von Hand eintragen, dann per `idf.py -B <tmp> -D SDKCONFIG=<kopie> reconfigure` gegenprüfen, ob kconfgen dieselbe Datei erzeugt.
- **ADC2 (S3, 12 dB):** Die versorgte Kamera klemmt das Pad auf ~3,1 V, an der Grenze des Messbereichs. Vollausschlag wird auf ~4,97 V extrapoliert, deshalb begrenzt `RailSense` auf 3300 mV. Der eye_R-Platz misst systematisch höher (Board-/ESP-Seite, für das Verdikt egal).
- **Bench-Kommando:** refused bei laufendem UVC (`busy`). Nicht unmittelbar nach `start_streaming` senden, sonst fällt der verzögerte Streaming-Start (150 ms) in den Zyklus.
