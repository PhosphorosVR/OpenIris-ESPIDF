# Plan: Angleichung an Upstream (EyeTrackVR/OpenIris-ESPIDF)

Erstellt am 2026-09-29, Stand 2026-09-30.
- Deine Entscheidungen zu F1 bis F7 stehen in Abschnitt 6.
- F5 (eine aus Lizenzgründen entfernte Datei), die aufgeräumten Zweige und die Releases stehen in Abschnitt 7.
- Umgesetzt wird nur, was freigegeben ist.

**Grundregeln:**
- **Nichts verlässt die eigenen Repos.** `upstream` (EyeTrackVR) ist nur lesend: kein Push, kein PR, kein Issue, kein Kommentar.
- **Pushes nach `origin`** nur nach Freigabe genau dieses Pushes. Ein Push, der überschreibt statt anzuhängen (Kraftpush), braucht zusätzlich eine ausdrückliche Bestätigung, nachdem die Prüfergebnisse vorliegen.
- **Git auf dem üblichen Weg.** Vor jedem Befehl, der löscht oder umschreibt, steht ein Satz, was er tut und was danach anders ist.

**Grundlage der Analyse:**
- `main` = `9b4ccfc` (FFVR 1.3.2). Seit 2026-09-30 sind Commits dazugekommen (Abschnitt 7).
- `upstream/main` = `4d13ec1`
- gemeinsame Basis `6971464` (Merge von PR #33, 2026-02-05)
- Stand: 76 Commits vor, 8 hinter Upstream

**Was für diesen Plan getan wurde:** nur gelesen und verglichen.
- Upstream wurde als reine Lese-Referenz geholt (`refs/remotes/upstream/main`, dazu `refs/remotes/upstream/feature/facefocus-duty-cycle-poc`). Es gibt keinen neuen Remote-Eintrag.
- Dabei kam der Upstream-Tag `0.2.1rc0` mit. Er zeigt auf `054dda6`, einen Commit, den `main` schon enthält, und ist nur lokal (siehe R8).
- Der Merge und die Cherry-Picks wurden mit `git merge-tree` simuliert. Das legt nur Objekte an und ändert weder Zweig noch Index noch Arbeitsbaum.
- IDF-Quellen und `esp32-camera` sind direkt von GitHub gelesen, nicht aus dem Gedächtnis.

---

## 0. Kurzfassung

1. **Die 8 Upstream-Commits sind klein:** 5 inhaltliche Commits und 3 Merge-Commits. Upstream hat seit der gemeinsamen Basis **keine Datei unter `components/` oder `main/`** angefasst. Geändert sind:
   - zwei neue Boards (Venti, WROOM-S3 N8R8),
   - eine Umbenennung (`wrooms3` → `wrooms3N8R2`, `wrooms3QIO` → `wrooms3QION8R2`),
   - ein Linux-Fix im Setup-Tool,
   - `sdkconfig`, `dependencies.lock`, CI-Matrix, README, Tests.
2. **IDF: Upstream ist nicht auf 5.5.** Die CI baut weiter mit **v5.4.2** (`build-and-release.yml`, Zeilen 62 und 69). Die 5.5.2 im `sdkconfig`-Kopf und in `dependencies.lock` stammt aus der lokalen Umgebung eines Beitragenden (Commit `f2d5889`, Linux-Fix).
3. **Der I2C-Fehler ist in Upstreams IDF-Version (5.4.2) offen.** Auch in 5.4.3, 5.4.4, im aktuellen `release/v5.4`, in **5.5.0 und 5.5.1** ist er drin. Behoben ist er erst ab **v5.5.2**, außerdem in 6.0, 6.1 und `master`, und zwar wörtlich mit dem Code aus meiner Kopie. **Durch den Upstream-Abgleich allein fällt die Treiberkopie nicht weg.** Sie fällt erst mit einem IDF-Wechsel auf ≥ 5.5.2, und das ist ein eigener Schritt.
4. **Merge-Simulation:** Es gibt nur **2 Textkonflikte** (CI-Matrix, `sdkconfig`). Wichtiger sind **3 stille Stellen**, die Git ohne Konflikt zusammenführt:
   - `dependencies.lock` bekäme IDF 5.5.2,
   - `setup_openiris.py` lehnt unter Windows jeden COM-Port ab (Fehler bei Upstream),
   - das neue Venti-Board setzt ein XCLK-Symbol, das es bei mir nicht mehr gibt.
5. **Empfehlung:**
   - `upstream/main` per **Merge** (kein Rebase) in einen Zweig `sync/upstream-2026-09` holen und die Konflikte wie in Abschnitt 3 auflösen.
   - Danach zwei, drei kleine Folge-Commits.
   - Prüfen, ob die drei FFVR-Images **bitgleich** zu 1.3.2 bleiben. Das ist zu erwarten, weil Upstream keinen Firmware-Eingang der FFVR-Builds ändert.
   - Erst dann nach `main`.
   - PRs entstehen später **nicht aus `main`**, sondern als kleine Themen-Zweige direkt von `upstream/main`.
6. **Korrektur an deiner Einschätzung:**
   - Die „drei Kategorie-B-Fixes“ sind für Upstream **ein** eigenständiger Bugfix: `c59c41c`, `esp_timer` in µs an zwei Stellen, `restart_device` und `startStreamingCommand`.
   - Der Sensorzeiger unter dem Mutex (`4dde6ea`) ist bei Upstream kein Fehler. Dort gibt es weder `sensor_mutex` noch wird `camera_sensor` je auf null gesetzt. Der Fix gehört zum Kamera-Lebenszyklus.
   - Dafür gibt es zwei weitere sofort PR-taugliche Kandidaten: den Serial-Fix `2fad53c` und den LEDC-Timer-Fix. Den LEDC-Timer-Fix hat der Maintainer in deinem PR #20 schon als „good catch“ gelobt.

---

## 1. Was die 8 Commits enthalten

| Commit | Autor, Datum | Inhalt | Dateien | Überschneidung mit meinen Dateien | Bedeutung für mich |
|---|---|---|---|---|---|
| `169082a` | dan2wik, 2026-06-30 | Neues Board `venti_N8R8`: Kopie von `project_babble`, aber 8 MB Flash und Octal-PSRAM (N8R8-Modul) | `boards/venti_N8R8/venti_N8R8`, CI-Matrix, README, `tests/conftest.py` | CI-Matrix (ich: FFVR entfernt), `conftest.py` (ich: `fan` bei eye_R) | Board-Datei setzt `CONFIG_CAMERA_USB_XCLK_FREQ=23000000`; das Symbol heißt bei mir `…_DEFAULT` (siehe 2a.1). Erbt alle meine Änderungen am gemeinsamen Code |
| `39de196` | dan2wik, 2026-06-30 | Venti: LED-Helligkeit 100 → 30 % | nur Venti-Datei | keine | Meine Duty-Formel (×256 statt ×255) ergibt bei 30 % denselben Rohwert 76. Kein Unterschied |
| `b30096d` | Merge PR #37 | – | – | – | – |
| `0c19e34` | lorow, 2026-06-11 | WROOM-S3 aufgeteilt: `wrooms3` → `wrooms3N8R2`, `wrooms3QIO` → `wrooms3QION8R2` (reine Umbenennung, Inhalt 100 % gleich), neu `wrooms3N8R8` (Octal-PSRAM) | Board-Dateien, CI-Matrix, README, `conftest.py` | CI-Matrix, `conftest.py` | **`wrooms3` ist meine Referenzkonfiguration für den Bitvergleich** (siehe 2c). `compare_builds.py` nimmt den Namen als Argument und läuft weiter; Doku und Aufrufe müssen den neuen Namen nehmen |
| `e48d73c` | Merge PR #36 | – | – | – | – |
| `f2d5889` | Nils Ponsard, 2026-07-09 | Linux-Fix für `setup_openiris.py` (Port-Prüfung, README). **Nebenbei** `sdkconfig` mit IDF 5.5.2 neu erzeugt, `dependencies.lock` auf `idf 5.5.2`, `uv.lock` auf 0.2.1rc0 | README, `sdkconfig`, `dependencies.lock`, `uv.lock`, `tools/setup_openiris.py` | `sdkconfig`, `dependencies.lock`, `setup_openiris.py` | **Neue Port-Prüfung ist unter Windows kaputt** (siehe 2a.2). Lock und `sdkconfig` passen nicht zur Upstream-CI (5.4.2) |
| `ce96c28` | Merge PR #38 | – | – | – | – |
| `4d13ec1` | lorow, 2026-07-19 | Eingechecktes `sdkconfig` zurück auf den Stand von `project_babble` (4 MB, Quad-PSRAM, LED 100 %) | `sdkconfig` | `sdkconfig` (meins ist die expandierte Konfiguration von eye_L) | Nur für lokale Builds ohne `switchBoardType` relevant |

**Zu deinen Schwerpunkten:** Upstream hat seit der gemeinsamen Basis **nichts** an Kameratreiber, UVC, CommandManager, CameraManager oder an der Boardstruktur geändert, abgesehen von neuen Board-Dateien und der Umbenennung. Geprüft mit `git diff --stat 6971464 upstream/main`: 11 Dateien, keine davon unter `components/` oder `main/`.

### 1.1 IDF-Version und der I2C-Fehler (F24)

**Welche IDF nutzt Upstream?**

| Quelle | Version | Bewertung |
|---|---|---|
| `.github/workflows/build-and-release.yml`, Zeilen 62 und 69 (`esp-idf-ci-action`, `esp_idf_version`) | **v5.4.2** | Damit entstehen Upstreams Release-Images |
| `sdkconfig`-Kopf, `dependencies.lock` (`idf: 5.5.2`) | 5.5.2 | Aus `f2d5889`: lokal neu erzeugt, kein bewusster Wechsel. Seit der Basis kein Commit mit IDF-Bezug |
| `main/idf_component.yml` | `idf: "^5.0"` | Unverändert, keine Festlegung |

Upstream ist also gemischt. Die CI baut mit 5.4.2, mindestens ein Beitragender baut lokal mit 5.5.2.

**Ist der Fehler behoben?** Die Stelle ist `s_i2c_send_commands()` in `components/esp_driver_i2c/i2c_master.c`, die Warteschleife nach NACK. Direkt aus den Tags gelesen:

| ESP-IDF | Warteschleife nach NACK | Quelle |
|---|---|---|
| v5.4.2 (Upstream-CI, meine Kopie) | **unbegrenzt** (`while (busy) nop;`) | `i2c_master.c:543` |
| v5.4.3 | **unbegrenzt** | `:543` |
| v5.4.4 | **unbegrenzt** | `:544` |
| `release/v5.4` (Stand `47fded9`) | **unbegrenzt** | `:545` |
| v5.5 (5.5.0) | **unbegrenzt** | `:543` |
| v5.5.1 | **unbegrenzt** | – |
| **v5.5.2** | begrenzt, danach FSM-Reset und Bus-Freigabe | `:594-603` |
| v5.5.5 | begrenzt | `:596-605` |
| v6.0, v6.1, `master` | begrenzt | – |

- Nicht einzeln geprüft sind v5.5.3, v5.5.4 und v6.0.1 bis v6.0.3. Weil 5.5.2 und 5.5.5 den Fix haben, ist er dort mit hoher Wahrscheinlichkeit auch drin.
- Meine Kopie ist v5.4.2 plus **genau** die Zeilen aus v5.5.2. Der Diff gegen das Original-v5.4.2 zeigt nur diesen einen Block.

**Folgerungen:**
- **Die Kopie kann erst mit IDF ≥ 5.5.2 ersatzlos weg.** Nicht mit 5.5.0 oder 5.5.1.
- Der Hinweis im Versionsschutz (`components/esp_driver_i2c/CMakeLists.txt`: „v5.5+ has the fix“) und die Übergabe („Entfernen, sobald auf IDF ≥ 5.5 umgestiegen wird“) sind deshalb **zu ungenau**. Richtig ist ≥ 5.5.2. Das korrigiert Folge-Commit 3 (Abschnitt 3).
- Der Versionsschutz ist robust. IDF setzt `ENV{IDF_VERSION}` selbst (`tools/cmake/version.cmake:5`), er greift also auch in der CI.
- Die zweite unbegrenzte Schleife (`while (i2c_ll_is_bus_busy) {}` in `s_i2c_send_command_async`) steckt in allen Versionen, auch in 5.5.5. Sie betrifft nur den asynchronen Pfad. SCCB legt den Bus ohne `trans_queue_depth` an (`sccb-ng.c:129-133`), nutzt also den synchronen Pfad. Kein Handlungsbedarf.

**Was ein IDF-Wechsel auf ≥ 5.5.2 zusätzlich bedeutet.** Ein eigener, späterer Schritt, siehe Phase 5:
- Die Aussagen der Abnahme zu `gpio_config()` und zu den I2C-Pull-ups (Falle 1) gelten in 5.5.2 weiter:
  - `gpio_config()` ruft für Eingänge weiterhin `gpio_output_disable()` auf.
  - Diese Funktion setzt in 5.5.2 **zusätzlich** die Pin-Funktion auf GPIO (`gpio.c:211`). Das Parken wird dadurch eher robuster.
  - Die I2C-Pull-up-Logik in `i2c_common.c` ist gleich geblieben.
  - Die Zeilennummern in der Analyse verschieben sich.
- **Nicht belegt:** ob `esp32-camera` 2.0.15 (meine Kopie) unter 5.5.2 fehlerfrei baut und läuft.
  - Indiz dafür: Upstreams Lockdatei aus `f2d5889` hat 2.0.15 zusammen mit IDF 5.5.2 aufgelöst.
  - Die Release-Notes von 2.0.16 bis 2.1.8 nennen keinen Fix speziell für 5.5, nur für IDF 6.
- Jedes Image aller Boards ändert sich. Ein Bitvergleich gegen eine 5.4.2-Baseline ist dann unmöglich. Es braucht eine neue Baseline und einen vollen Hardwaretest.
- Nebenbefund: `esp32-camera` v2.1.8 (2026-09-25) behebt eine NULL-Dereferenz in den SCCB-Zugriffen. Für die Recovery ist das interessant, wenn die Treiberkopie irgendwann neu aufgesetzt wird.

### 1.2 Umfeld bei Upstream (für die PR-Strategie)

- **Dein PR #20 „XCLK_FREQ_OVERRIDE + other small stuff“:** geschlossen am 2026-02-12, nicht gemergt. Der Maintainer lorow schrieb:
  - „I'm not sure if I'm sold on the current implementation of overrides BUT I do think it's a good idea to have two separate and clearly defined freq for the different sensors. Also, once more, good catch with the timers, thank you!“
  - Im Review fragte er ausdrücklich, warum `CAMERA_USB_XCLK_FREQ_DEFAULT` eingeführt wird, statt direkt zuzuweisen. Er wünscht sich die Override-Logik als kleine Methode, die früh greift.
- **Offener PR #40 (misonyah) „Serve a diagnostic frame when camera init fails“:**
  - ändert `CameraManager.cpp/.hpp`, `main/openiris_main.cpp`, `StreamServer`, `LEDManager` und `StateManager`,
  - wiederholt den Kamera-Init alle 5 s.
  - Das ist dein Themenfeld (`boot_failure`, Recovery). Wird #40 gemergt, gibt es beim nächsten Abgleich echte Konflikte in `CameraManager`.
- **Weitere offene PRs:** #39 (Thermal, WLAN), #35 (Board-Varianten für Quest Pro), #22, #15. Keiner fasst den FFVR-Pfad direkt an.
- **Upstream-Zweig `feature/facefocus-duty-cycle-poc`:** ein PoC vom 2025-08-19, älter als die gemeinsame Basis. Nicht relevant.

**Entscheidung (2026-09-30):** Abschnitt 1.2 und Abschnitt 4 bleiben als Vorarbeit stehen. Umgesetzt wird davon nichts:
- kein Issue, kein PR, kein Kommentar bei Upstream, auch nicht der Vorschlag „CI auf IDF ≥ 5.5.2 heben“;
- keine Arbeit am Lebenszyklus-Block, um ihn PR-tauglich zu machen.

---

## 2. Kollisionen

Grundlage ist die Simulation `git merge-tree --write-tree refs/heads/main upstream/main`:

```
CONFLICT (content): Merge conflict in .github/workflows/build-and-release.yml
CONFLICT (content): Merge conflict in sdkconfig
Auto-merging dependencies.lock
Auto-merging tests/conftest.py
Auto-merging tools/setup_openiris.py
```

### 2a. Echte inhaltliche Konflikte: beide lösen dieselbe Sache anders

**2a.1 XCLK-Symbol.** Kein Textkonflikt, deshalb gefährlich.

- **Upstream:** ein Symbol `CAMERA_USB_XCLK_FREQ`, Bereich 1 bis 24 MHz. Es steht in allen Board-Dateien, auch im neuen `venti_N8R8`.
- **Ich** (`c0c3f06`, Februar):
  - `CAMERA_USB_XCLK_FREQ_DEFAULT`, Bereich bis 40 MHz,
  - dazu `CAMERA_XCLK_FREQ_OV2640_OVERRIDE` und `…_OV3660_OVERRIDE`,
  - Umschaltung nach der Sensorerkennung in `setupCamera()`.
- **Wirkung nach dem Merge:**
  - Venti setzt ein Symbol, das mein Kconfig nicht kennt. kconfgen warnt und ignoriert es.
  - Venti bekommt die 23 MHz trotzdem, aber nur weil `boards/sdkconfig.base_defaults:582` zufällig denselben Wert setzt.
  - Jedes künftige Upstream-Board mit einem anderen Wert verlöre ihn still.
- **Bewertung:**
  - Die Sensor-Overrides sind nötig. Den Sensor kennt man erst nach dem Probe, und die OV3660 braucht den Umschaltpfad 23 → 20 MHz (Hardwarebefund vom 2026-07-20). Upstreams Variante kann das nicht, und der Maintainer will das Prinzip ohnehin („two separate and clearly defined freq“).
  - Die **Umbenennung** des Basis-Symbols bringt dagegen nichts. Sie kostet Kompatibilität mit jedem Upstream-Board, und der Maintainer hat genau sie hinterfragt.
- **Empfehlung:** das Basis-Symbol auf Upstreams Namen `CAMERA_USB_XCLK_FREQ` zurückbenennen, die beiden Override-Symbole behalten.
  - Betrifft 1 Codezeile (`CameraManager.cpp:142`), `main/Kconfig.projbuild`, `base_defaults`, die 5 Board-Dateien mit dem Symbol, `sdkconfig` und `sdkconfig.old`.
  - Der Wertebereich (bis 40 MHz) bleibt, wie er ist. Zurückdrehen würde eine gewollte Änderung, und wirksam ist er bei 23 MHz nicht.
  - Als eigener Folge-Commit, **nicht** im Merge-Commit.
  - Wirkung: `app.bin` gleich, `sdkconfig.h` unterscheidet sich nur im Symbolnamen. Das zeigt der Vergleich.
  - **Entschieden (F2):** zurück auf `CAMERA_USB_XCLK_FREQ`, die Overrides bleiben, der Bereich bis 40 MHz auch.
  - In den Kconfig-Hilfetext kommt englisch und knapp die Begründung: Warum es die Sensor-Overrides gibt, und dass die OV3660 auf FFVR mit 23 MHz startet und dann auf 20 MHz umschaltet, weil sie ohne diese Umschaltung keine Frames liefert.

**2a.2 Port-Prüfung in `tools/setup_openiris.py`.** Fehler bei Upstream, kommt ohne Konflikt herein.

- Upstream (`f2d5889`) prüft `if sys.platform == "windows":`. Unter Windows ist `sys.platform` aber `"win32"`, hier auf dem Rechner geprüft.
- Folge: Unter Windows landet jeder Aufruf im Linux-Zweig, und `--port COM69` scheitert mit „Port must be in /dev/tty“.
- Mein Anteil an der Datei (+167 Zeilen: Lüfter-, Log- und Debug-Menüs) kollidiert textlich nicht.
- **Empfehlung:** Upstreams Absicht (Linux-Unterstützung) übernehmen und den Vergleich auf `sys.platform == "win32"` korrigieren. Das ist ein eigener Folge-Commit und zugleich der kleinste denkbare PR an Upstream.

**2a.3 Eingechecktes `sdkconfig`.**

- Upstream: `project_babble` mit IDF 5.5.2.
- Ich: expandiertes eye_L mit IDF 5.4.2, Version 1.3.2, alle Recovery-Symbole.
- Git zeigt nur zwei Konfliktblöcke (Zeilen 612 und 1348: Board, Version, WLAN und ein PSRAM-Symbol). **Den Rest führt es still zusammen**, zu einem Zwitter: IDF-5.5.2-Kopf und neue SOC-Symbole zusammen mit meinen eye_L-Werten.
- **Empfehlung:** Die Datei komplett auf meine Seite setzen (`git checkout --ours sdkconfig`), nicht Block für Block auflösen. Upstreams Wahl (babble) ist dort richtig, für mein Repo aber nicht. PR-Zweige fassen `sdkconfig` nie an.
- **Dauerregel (2026-09-30):** Das eingecheckte `sdkconfig` nimmt bei **jedem** künftigen Upstream-Abgleich als ganze Datei die eigene Seite. Es wird nie blockweise aufgelöst.

**2a.4 `dependencies.lock`.** Wird ohne Konflikt auf `idf: 5.5.2` gesetzt.

- Ich baue mit 5.4.2, die Upstream-CI auch.
- Stimmt die IDF-Version im Lock nicht, löst der Component Manager die Abhängigkeiten beim Build neu auf.
- **Nicht belegt** ist, ob er dabei für `espressif/mdns: "*"` oder `led_strip: "^2.4.1"` neuere Versionen zieht. Wenn ja, änderten sich die Images **aller** Boards unbemerkt.
- **Empfehlung:** meine Zeile behalten (`idf: 5.4.2`). Das passt zur CI von Upstream und zu meiner Umgebung. Sollte dennoch etwas neu aufgelöst werden, zeigt es der Bitvergleich.

**2a.5 CI-Matrix.**

- Upstream: Venti dazu, `wrooms3*` umbenannt, `wrooms3N8R8` dazu.
- Ich: die drei FFVR-Boards entfernt (`bc7c404`, „Removed FFCR boards from Build-Matrix“).
- **Empfehlung:** Upstreams Liste übernehmen und die drei FFVR-Einträge weiter weglassen, so bleibt deine Absicht erhalten. Den Grund für `bc7c404` kenne ich nicht (Frage F1).
- **Entschieden (F1):** FFVR bleibt draußen.
  - Grund, der auch in den Merge-Text kommt: Das Produkt wird lokal gebaut, gegen die Baseline geprüft und mit dem eigenen Werkzeug geflasht. CI-Releases mit FFVR-Images wären eine zweite, unkontrollierte Firmwarequelle.
  - Aus demselben Grund verschwinden die FFVR-Images aus den eigenen Releases v1.0.0 bis v1.2.4 (Abschnitt 7.2).
- **Erledigt:** `bc7c404` war gewollt. Der Release-Text von v1.2.2 nennt „Removed FFVR boards from the build matrix“ als eigene Änderung (Anhang A). Die vorgeschlagene CI-Prüfung entfällt; die Frage stellt sich beim nächsten Abgleich nicht wieder.

### 2b. Reine Textkonflikte: mechanisch aufzulösen

- `.github/workflows/build-and-release.yml`: eine einzige Zeile (die Matrix). Auflösung wie 2a.5.
- `sdkconfig`: zwei Blöcke. Auflösung wie 2a.3, also die ganze Datei.
- `tests/conftest.py`: automatisch sauber. Mein `"fan"` bei eye_R bleibt, Upstreams neue Boardnamen kommen dazu.

### 2c. Upstream hat umbenannt oder entfernt, worauf meine Arbeit aufsetzt

Es gibt nur eine Umbenennung: **`wrooms3` → `wrooms3N8R2`** (und `wrooms3QIO` → `wrooms3QION8R2`).

- `wrooms3` ist eine der beiden Referenzkonfigurationen für den Bitvergleich (F22). Sie steht in der Abnahme, in der Analyse (17.x) und in der Übergabe.
- `compare_builds.py` ist nicht betroffen, es nimmt `--board` als Argument. Aufrufe mit `--board wrooms3` scheitern aber nach dem Merge.
- Die vorhandene Baseline `../OpenIris-refbuilds/baseline2/wrooms3` bleibt gültig, weil die Board-Datei unverändert umbenannt wurde. Beim nächsten Vergleich also `build --board wrooms3N8R2` gegen `baseline2/wrooms3`.
- Sonst hat Upstream nichts entfernt oder umbenannt, worauf Lüfter, Kamera-Power, Fixes, Boardconfigs oder Werkzeuge aufsetzen.

### 2d. Keine Kollision, aber zu wissen

- **Die zwei neuen Upstream-Boards erben meine Änderungen am gemeinsamen Code.** `venti_N8R8` und `wrooms3N8R8` wurden bei mir nie gebaut. Sie bekommen:
  - die `esp32-camera`-Kopie (OV3660-PLL, DVP-Treiberstärke 0x03, XCLK aus XTAL),
  - die I2C-Kopie,
  - den esp_timer-Fix,
  - LED auf LEDC-Timer 1,
  - Framegröße 320×320, `CAMERA_GRAB_LATEST`, minimale XCLK-Treiberstärke.

  Beide müssen mindestens bauen (siehe 5.2).
- **Die Babble-Overrides gehen nicht auf Venti über.** Bei mir setzen `project_babble` und `seed_studio` für die OV3660 27 MHz. Venti ist eine Babble-Kopie, bekäme diese Overrides aber nicht.
  - **Entschieden (F6): bekannter, gewollter Unterschied, kein Versehen.** Venti bleibt, wie sein Autor es definiert hat.
  - Es gibt weder Venti-Hardware noch einen Messwert. Die Grenze hängt an Sensor, FPC und Layout, nicht am PSRAM-Typ.
  - Geht die Override-Mechanik je an Upstream, gehört die Frage in diesen PR und wird von Leuten entschieden, die die Boards haben.
- **Ehrliche Grenze der Aussage „fremde Boards bleiben unberührt“:** `venti_N8R8` und `wrooms3N8R8` sind mit diesem Baum nie auf Hardware gelaufen. Sie erben die Kameratuning-Werte und die Treiberkopien. Geprüft wird nur, dass sie bauen.
- **PR #40 bei Upstream** (siehe 1.2): künftiges Konfliktrisiko in `CameraManager` und `openiris_main.cpp`.

---

## 3. Der Weg

### Reihenfolge (Entscheidung vom 2026-09-30)

1. F5, Schritte 1 und 2: die Datei aus dem aktuellen Stand entfernt, nicht mehr benötigte Zweige gelöscht. **Erledigt** (Abschnitt 7).
2. Dieser Plan als eigener Commit. **Erledigt.**
3. Tags und Releases (Abschnitt 7.2), `v1.0.0` als Probe zuerst. **Tags gelöscht.** Offen: v1.2.2 und v1.2.4 auf Entwurf stellen.
4. Prüfen, ob ein Image vom Git-Zustand abhängt (Abschnitt 7.3). **Erledigt**, daraus folgte Version 1.3.3 mit reproduzierbarem Build.
5. F5 Schritt 3, das Bereinigen der Historie. **Erledigt am 2026-09-30.** Ablauf und Prüfungen liegen außerhalb des Repos.
6. Phase 0. **Erledigt am 2026-09-30:** Tag `v1.3.3` und Baseline 3 (Abschnitt „Phase 0“ unten).
7. **Als Nächstes Phase 1**, dann 2 bis 4 wie unten. Die Phasen 5 (IDF) und 6 (PR-Vorbereitung) laufen in diesem Durchgang nicht.

**Festlegung:** Die Sicherungen von F5, alle außerhalb des Repos, bleiben liegen, bis der Upstream-Abgleich fertig ist. Aufgeräumt wird erst danach und nur nach Ansage.

**Die gemeinsame Basis `6971464` ist beim Bereinigen erhalten geblieben** (nachgeprüft am 2026-09-30).
- Neue Kennungen haben nur der Commit, mit dem die Datei kam, und seine Nachfahren bekommen. Alle älteren Commits haben ihre Kennung behalten.
- `6971464` ist Vorfahre des aktuellen `main`.
- Die Merge-Simulation findet ihre Basis damit wie vorher.

### Entscheidung: Merge, nicht Rebase

- `main` ist gepusht (`origin/main`). Ein Rebase würde 76 veröffentlichte Commits umschreiben, einschließlich der drei Fix-Merges und des Feature-Merges `be1cea1`.
- Beim Rebase käme jeder der vielen `sdkconfig`-Commits einzeln in Konflikt. Beim Merge sind es 2 Textkonflikte.
- Ein Rebase macht den PR nicht leichter. PRs werden ohnehin als eigene Zweige von `upstream/main` geschnitten (Phase 6). Ein hübscher linearer Verlauf von `main` nützt dem Maintainer nichts, er sieht nur den PR-Zweig.

### Zweige

| Zweig | Basis | Zweck |
|---|---|---|
| `main` | – | **bleibt unangetastet**, bis Phase 4 freigegeben ist |
| Tag `v1.3.3` | `5bd8f39` (Versions-Commit 1.3.3) | fester Rückweg auf den Auslieferstand. Ersetzt den früher geplanten `v1.3.2`. Tags werden nur einzeln gepusht, nie mit `--tags` |
| `sync/upstream-2026-09` | `main` | Merge von `upstream/main` und die Folge-Commits |
| `chore/idf-5.5` | später, von `main` | IDF-Wechsel auf ≥ 5.5.2 und Entfernen der I2C-Kopie (Phase 5) |
| `pr/<thema>` | **`upstream/main`** | je ein PR-Thema (Phase 6) |

### Phase 0: Sicherung, vor allem anderen

1. **Tag:** `v1.3.3` auf `5bd8f39`, den Versions-Commit, aus dem die drei 1.3.3-Images gebaut wurden. Namentlich gepusht am 2026-09-30.
   - Er ersetzt den früher geplanten `v1.3.2`; 1.3.2 bekommt keinen Tag.
   - Am selben Tag wurde er versehentlich auf GitHub entfernt und danach erneut gepusht.
2. **Images:**
   - 1.3.3 liegt benannt unter `../OpenIris-refbuilds/release_1.3.3/` (Abschnitt 7.3).
   - 1.3.2 liegt als `merged.bin` unter `../OpenIris-refbuilds/release_1.3.2/<board>/build/`. Es stammt aus dem Versions-Commit 1.3.2, heute `61b5f2c`. Die Version im Image nennt noch dessen Kennung von vor der Bereinigung.
   - Beide gehen **noch nicht** nach `../ffvr-multiflash/fw/`. Das kommt nach der ESD-Prüfung als eigener Schritt.
   - **Geklärt:** Das Multiflash-Werkzeug schreibt je Rolle genau eine Datei an Adresse `0x0` (`config.json` und `src/config.py`: `"flash_address": "0x0"`; `src/flasher.py`: `write-flash <Adresse> <Datei>`). Es erwartet also die Merge-Images ab Offset 0, wie sie für 1.3.2 und 1.3.3 gebaut wurden.
3. Baseline 3 bauen, auf dem aktuellen `main` mit IDF 5.4.2:

   ```
   python tools/compare_builds.py build --board facefocusvr_eye_L --out ../OpenIris-refbuilds/baseline3/facefocusvr_eye_L
   (ebenso facefocusvr_eye_R, facefocusvr_face, project_babble, wrooms3)
   ```

   Bisher lagen nur `project_babble` und `wrooms3` in der Baseline. Für diesen Schritt zählen gerade die FFVR-Boards.

   **Ergebnis (2026-09-30):** Gebaut aus `7872367`, sauberer Arbeitsbaum, unter `../OpenIris-refbuilds/baseline3/`. Das sind Referenzbuilds mit festgesetzter Version und reproduzierbarem Build; ihre App-Hashes sind deshalb nicht die der 1.3.3-Produktimages.

   | Board | SHA-256 `app.bin` |
   |---|---|
   | facefocusvr_eye_L | `4cb88d8b05a382eb8035bff873a800520ade1f64ab6cc331b5e4d39d904d8632` |
   | facefocusvr_eye_R | `81f497eb9ea8dd672959e8f3347d135da5d6852393ae52749f69af333f950d0c` |
   | facefocusvr_face | `e3ba8f8e556246714856d94fc64df128566580791dff9d9a61fc63fc8b6f3c84` |
   | project_babble | `637f48b8e5c59b49c2bbcaa032727d512ef484210b98703dc08b24681f132977` |
   | wrooms3 | `f9a6faa91ed7c45083537a2df051b51e2fe9b48800ce9ee535091de17f460b8d` |

   - `project_babble` und `wrooms3` sind per `compare_builds.py compare` **identisch mit Baseline 2**: App bis auf die maskierten Prüfsummen, dazu Bootloader, Partitionstabelle und `sdkconfig.h`.
   - Weder der Versions-Commit 1.3.3 noch das Bereinigen der Historie hat also fremde Boards verändert.
   - Nach dem Merge in Phase 3 wird `wrooms3N8R2` gegen `baseline3/wrooms3` verglichen.

**Zurück auf den Stand vor dem Abgleich geht jederzeit:**
- Bis Phase 4 ist `main` gar nicht berührt.
- Danach ohne Umschreiben: `git revert -m 1 <Merge-Commit>`, oder aus dem Versions-Commit bauen (`git worktree add --detach ../OpenIris-refbuilds/v133 5bd8f39`, das Image ist reproduzierbar), oder die gesicherten Bins flashen (vorher `erase-flash`).

### Phase 1: Sync-Zweig und Merge (mechanisch)

```
git remote add upstream https://github.com/EyeTrackVR/OpenIris-ESPIDF.git   # F7: ja
git fetch upstream
git tag -d 0.2.1rc0                  # löscht nur den mitgeholten Upstream-Tag, lokal
git switch -c sync/upstream-2026-09 main
git merge --no-ff upstream/main
```

**Vorschlag, nur nach deinem OK:** eine Sperre gegen Pushes an `upstream`.
- Der Befehl ist `git remote set-url --push upstream DISABLED`. Er setzt nur die Push-Adresse von `upstream` auf einen ungültigen Wert. Holen (`git fetch upstream`) funktioniert weiter, jeder Push an `upstream` scheitert sofort mit einer Fehlermeldung.
- Das ist bei Forks eine übliche Absicherung und lässt sich mit `git remote set-url --push upstream <URL>` jederzeit zurücknehmen.

Auflösungen, alle im Merge-Commit und dort im Text genannt:

| Datei | Auflösung |
|---|---|
| `.github/workflows/build-and-release.yml` | Upstreams Matrix ohne die drei `facefocusvr_*` (2a.5) |
| `sdkconfig` | ganz meine Seite: `git checkout --ours sdkconfig` (2a.3) |
| `dependencies.lock` | meine Seite, obwohl ohne Konflikt: `git checkout HEAD -- dependencies.lock` vor dem Commit (2a.4) |
| alles andere | Git-Ergebnis übernehmen |

Kontrolle vor dem Commit:
- `git diff main -- sdkconfig dependencies.lock` muss leer sein.
- `git diff main --stat` darf nur Upstreams Dateien zeigen: die neuen Boards, die Umbenennung, README, `conftest.py`, `setup_openiris.py`, `uv.lock` und die Matrix.

### Phase 2: Folge-Commits auf dem Sync-Zweig (je ein Commit, getrennt vom Merge)

1. `fix(tools): accept COM ports on Windows in valid_port`. Nur `sys.platform == "win32"` (2a.2).
2. `CAMERA_USB_XCLK_FREQ_DEFAULT` → `CAMERA_USB_XCLK_FREQ` (F2 entschieden). Die Overrides und der Bereich bis 40 MHz bleiben, die Begründung kommt in den Kconfig-Hilfetext (2a.1). Im eingecheckten `sdkconfig` das Symbol von Hand umbenennen und wie in der Übergabe (Abschnitt 8) per `reconfigure` gegenprüfen.
3. `docs/build`:
   - Übergabe und Abnahme: Referenzboard `wrooms3N8R2`, Befund zur IDF-Version (Abschnitt 1.1).
   - Meldung im Versionsschutz der I2C-Kopie: „v5.5.2+“ statt „v5.5+“. Das ist nur ein CMake-String und ändert kein Image.

### Phase 3: Prüfung

Siehe Abschnitt 5.2. Kurz: alle Konfigurationen bauen, FFVR und Referenzen bitgleich zu Baseline 3, pytest auf den drei FFVR-ESPs, kurzer Hardwaretest.

### Phase 4: nach `main`, nur nach deiner Freigabe

```
git switch main
git merge --no-ff sync/upstream-2026-09
```

Pushen erst nach Freigabe (`git push origin main`). Der Upstream-Tag `0.2.1rc0` ist dann schon lokal gelöscht (Phase 1). Tags werden ohnehin nur einzeln und nie mit `--tags` gepusht (R8).

### Phase 5: IDF-Entscheidung (später, eigener Zweig)

Empfehlung: **nicht jetzt und nicht vor AP6.**
- Der Wechsel ändert jedes Image aller Boards.
- Er braucht eine neue Baseline und einen vollen Hardwaretest, einschließlich Recovery, Budget und `camera_power_bench`.
- Die ESD-Prüfung sollte auf der Firmware laufen, die ausgeliefert wird.
- Upstreams CI baut mit 5.4.2. Mit meiner Kopie bin ich dort gleichauf und habe zusätzlich den Fix.

Wenn gewechselt wird, dann direkt auf eine aktuelle 5.5.x (≥ 5.5.2), und `components/esp_driver_i2c` wird gelöscht.

**Entschieden (F3):** Der Abgleich läuft jetzt auf 5.4.2, die Treiberkopie bleibt. Der IDF-Sprung ist ein eigenes Thema nach der ESD-Prüfung. Der Zweig `chore/idf-5.5` wird in diesem Durchgang nicht angelegt.

### Phase 6: PR-Vorbereitung (in diesem Durchgang nicht)

Zweige von `upstream/main` aus, je Thema einer, siehe Abschnitt 4. Schon simuliert (`git merge-tree` als Cherry-Pick auf `upstream/main`):

| Commit | Ergebnis |
|---|---|
| `c59c41c` (esp_timer) | **sauber** |
| `2fad53c` (Serial ohne Reset) | **sauber** |
| `4dde6ea` (Sensorzeiger) | Konflikte: `CameraCycle.cpp` gibt es upstream nicht, `CameraManager.cpp` in Konflikt. Bestätigt, dass er am Lebenszyklus hängt |

**Abweichung von deinem Vorschlag:** „Die drei Kategorie-B-Fixes getrennt von den Features“ passt im Kern: Bugfixes gehören getrennt. Für Upstream sind es aber nur zwei eigenständige Fixes (esp_timer, Serial) plus die zwei neuen (Windows-Port, LEDC-Timer). Der Sensorzeiger geht mit dem Lebenszyklus.

---

## 4. Was PR-tauglich ist und was nicht

Grundsatz: Ein PR „mein `main` nach Upstream“ ist nicht realistisch.
- Er umfasst 191 Dateien und 38.000 Zeilen, davon 3.878 I2C-Kopie und rund 25.000 `esp32-camera`-Kopie.
- Dazu kommen Commits wie „I hate my life“ oder „Fixed“ aus der ersten Phase (Februar bis August), die quer durch gemeinsamen Code gehen.

Annehmbar sind kleine Themen-Zweige, jeder für sich baubar und begründet. Dass mein Zweig nah an Upstream liegt, hilft dabei indirekt. Ich teste gegen denselben Stand, und Cherry-Picks gehen sauber durch.

| Block | Urteil | Begründung | Was es bräuchte |
|---|---|---|---|
| **esp_timer in µs** (`c59c41c`: `ScheduleRestart`, `startStreamingCommand`) | **PR-tauglich** | Echter Fehler bei Upstream, noch vorhanden (`device_commands.cpp:111`, `OpenIrisTasks.cpp:15`). 2 Zeilen, Cherry-Pick sauber | Nichts. Commit-Text ist schon englisch und begründet |
| **Serial ohne Reset** (`2fad53c`, `tools/openiris_device.py`) | **PR-tauglich** | Jedes Verbinden startete S3 und USB-UART-Boards neu. Betrifft alle. Cherry-Pick sauber | Nichts |
| **Windows-Port-Prüfung** (neu, Phase 2) | **PR-tauglich** | Upstreams eigener Fehler aus #38, eine Zeile | Commit in Phase 2 |
| **LED auf eigenem LEDC-Timer/-Kanal** (Teil von `dea4790`/`d559191`) | **PR-tauglich, kleiner Aufwand** | Maintainer: „good catch with the timers, thank you!“ (#20). Vermeidet die Kollision mit dem XCLK-Timer 0 auf LEDC-Plattformen | Aus den Misch-Commits herauslösen. Den Timer-Teil und die Duty-Skala (×256, 100 % = voll) als zwei Commits. Auf einem S3-Board mit externer LED gegenprüfen |
| **Sensorzeiger unter dem Mutex** (`4dde6ea`) | **gehört zum Lebenszyklus** | Bei Upstream gibt es keinen `sensor_mutex`, `camera_sensor` wird nie null. Also kein Fehler dort | Mit dem Lebenszyklus-Block |
| **Pro-Sensor-XCLK mit Umschaltung** (`c0c3f06` ff., `setupCamera()`) | **mit Aufwand** | Maintainer will das Prinzip, nicht die damalige Form | Basis-Symbol behalten (2a.1), Override-Auswahl als kleine Methode, OV3660-Umschaltpfad mit dem Hardwarebefund begründen. Eigener PR, Babble und XIAO mit OV2640 gegentesten |
| **OV2640-Register nur bei OV2640** (Teil von `97d34e5`/`bf74dbc`) | **mit Aufwand** | Echter Fehler für jedes Board mit OV3660: `0xFF/0xD3` bedeuten dort etwas anderes | Aus den Profil-Commits herauslösen. Ohne Profile als kleiner Fix |
| **Kamera-Profile, OV3660-Spiegelung, DPC-Schwellen, 320×320, `GRAB_LATEST`, XCLK-Treiberstärke** | **bleibt bei mir** (vorerst) | Tuning für FFVR (kurzes FPC, 320×320, Wärme). Ändert heute Verhalten aller Boards | Wenn überhaupt: pro Board per Kconfig, Default = Upstream-Verhalten, und nur mit Tests auf Fremd-Hardware |
| **I2C-Backport** (`components/esp_driver_i2c`) | **als Kopie nicht PR-tauglich** | Mindestens ein Beitragender baut mit 5.5.2. Mein Versionsschutz bräche dessen Build ab (FATAL_ERROR) | Stattdessen Issue oder PR: CI auf IDF ≥ 5.5.2 heben, mit Verweis auf den Fehler. Das entscheidet der Maintainer. Alternative mit Aufwand: die Kopie außerhalb von `components/` ablegen und nur bei IDF 5.4.2 über `EXTRA_COMPONENT_DIRS` einbinden, dann stört sie 5.5.x-Nutzer nicht. Das wäre auch für mein Repo robuster |
| **Kamera-Lebenszyklus** (Gate, Kamera-Task, Parken, `recover_camera`, Budget, Auto-Auslöser, CamLines, RailSense) | **mit großem Aufwand** | Nützlich (die Stufe `reinit` und `frame_timeout` wirken auf jedem Board), aber: (1) setzt auf den Umbau aus der ersten Phase auf (Mutex aus `b42b585`, XCLK-Pfad in `setupCamera()`, UVC-Änderungen); (2) läuft nur ohne WLAN, weil der StreamServer am Gate vorbeigeht, und gerade die Upstream-Hauptboards sind WLAN-Boards; (3) überschneidet sich mit PR #40; (4) groß (rund 2.800 Zeilen Firmware) und mit deutscher Doku | In Etappen, **vorher ein Issue**, um den Maintainer abzuholen. (a) die CameraManager-Basis; (b) generischer Kern: Gate, Kamera-Task, `reinit`, `frame_timeout`, StreamServer über das Gate; (c) FFVR-spezifisch per Kconfig: CE/RESET, Rail-Check, Power-Cycle; (d) Status und Zähler schlanker; (e) kurze englische Doku statt der Analyse |
| **Lüfterpfad** (FanManager, zwei Kennlinien, Board-Erkennung, `test_fan.py`, `fan_calibration.py`) | **bleibt bei mir** | Nur FFVR eye_R, abgeschlossen. `FanManager` existiert upstream gar nicht | – |
| **FFVR-Boardkonfigurationen** | **bleibt bei mir** (F4 entschieden) | Die FFVR-Boards **gibt es auch upstream**, dort mit 23 MHz, ohne Features, und in der Upstream-CI (Releases). Upstreams FFVR-Images sind also veraltet gegenüber deinem Produkt | Nichts, ausgeliefert wird nur aus dem eigenen Repo. Risiko siehe R11 |
| **LogManager, Debug-Log-Kommandos** | **mit Aufwand** | Allgemein nützlich | Vorher F21 lösen (unbegrenzter Sammelpuffer). Aus den Misch-Commits herauslösen |
| **UVC, `usb_device_uvc`, SerialManager-Übergabe** (D+-Pull-up, Übergabe USB → UVC, Frame-Rückgabe im `xfer_complete`) | **mit Aufwand** | Teilweise echte Fehlerbehebungen für S3-Boards | Einzeln herauslösen, auf babble oder XIAO testen |
| **`esp32-camera`-Kopie** (OV3660-PLL, SCCB-Polling, Treiberstärke, XCLK aus XTAL) | **nicht für OpenIris** | Gehört, wenn überhaupt, zu `espressif/esp32-camera`. Die Treiberstärke 0x03 ist eine FFVR-EMV-Abwägung | Allenfalls OV3660-PLL als PR an Espressif |
| **Werkzeuge** `compare_builds.py` | **optional, mit Aufwand** | Allgemein nützlich (reproduzierbare Referenzbuilds) | Englisches README, ohne FFVR-Bezug |
| **Werkzeuge** `camera_*_check.py`, `camera_power_bench.py`, `fan_calibration.py` | **bleibt bei mir** | FFVR-spezifisch | – |
| **`docs/`** (deutsch) | **bleibt bei mir** | intern | – |

**Reihenfolge der PRs**, wenn es so weit ist: zuerst die kleinen und klaren, damit Vertrauen entsteht und der Maintainer den Stil kennenlernt:
1. esp_timer
2. Serial
3. Windows-Port
4. LEDC-Timer
5. dann ein Issue zum Lebenszyklus, zur XCLK-Umschaltung und zur IDF-Version

---

## 5. Risiken und Prüfung

### 5.1 Risiken

| # | Was schiefgehen kann | Woran man es merkt |
|---|---|---|
| R1 | `sdkconfig` als Zwitter (5.5.2-Kopf mit eye_L-Werten) | `head -4 sdkconfig` zeigt 5.5.2; `git diff main -- sdkconfig` nicht leer |
| R2 | `dependencies.lock` mit 5.5.2 → der Component Manager löst beim Build neu auf, und neuere `mdns`/`led_strip`/`tinyusb` ändern alle Images (Verhalten nicht belegt) | `git status` zeigt `dependencies.lock` nach dem Build geändert; `compare_builds.py compare` nicht identisch; Versionen in `managed_components/*/idf_component.yml` |
| R3 | `setup_openiris.py` unter Windows unbrauchbar | `uv run tools/setup_openiris.py --port COMxx` → „Port must be in /dev/tty“ |
| R4 | `wrooms3` gibt es nicht mehr | `compare_builds.py build --board wrooms3` scheitert → `wrooms3N8R2` nehmen |
| R5 | Venti mit unbekanntem XCLK-Symbol | kconfgen-Warnung beim Venti-Build; `CONFIG_CAMERA_USB_XCLK_FREQ*` im erzeugten `sdkconfig.h` prüfen |
| R6 | neue Boards (`venti_N8R8`, `wrooms3N8R8`) bauen mit meinem gemeinsamen Code nicht | Build-Fehler |
| R7 | FFVR-Images ändern sich unerwartet | `compare_builds.py compare` gegen Baseline 3 nicht identisch. Erwartet ist identisch, weil kein Firmware-Eingang der FFVR-Builds sich ändert |
| R8 | Upstream-Tag `0.2.1rc0` (von meinem `fetch`, nur lokal) landet per `git push --tags` auf `origin` | `git ls-remote --tags origin` |
| R9 | Späterer IDF-Wechsel: `esp32-camera` 2.0.15 unter 5.5 unbelegt, kein Bitvergleich möglich | nur Build und Hardwaretest |
| R10 | Upstream merged PR #40 → nächster Abgleich mit echten Konflikten in `CameraManager`/`openiris_main.cpp` | `git merge-tree` vor jedem Abgleich |
| R11 | **Bekanntes Risiko (F4):** Upstream baut und veröffentlicht eigene FFVR-Images, die hinter deinem Produkt liegen, und jemand flasht eins auf deine Hardware | Nutzer meldet dunkles Bild, Lüfter oder Kamera verhalten sich anders. Siehe Absatz unter der Tabelle |

**Zu R11: Was ein Upstream-FFVR-Image auf deiner Hardware tut.** Abgeleitet aus Upstreams Board-Dateien (`boards/facefocusvr/*` auf `upstream/main`), nicht auf Hardware geprüft:
- **IR-LEDs:** 45 % an den Augen, 85 % im Gesicht. Das sind die Werte von v1.0.0; dein Produkt nutzt 75 % und 100 %. Auf den neueren, gekapselten Ringen wird das Bild also dunkler.
- **Lüfter an eye_R:** keine Ansteuerung, GPIO6 bleibt unbenutzt. Was der Lüfter dann tut, hängt an der Beschaltung (**von dir zu ergänzen**).
- **Kamera:** keine CE/RESET-Steuerung, keine Recovery.
- **OV3660:** Originaltreiber, XCLK bleibt ohne Umschaltung auf 23 MHz. Ob die OV3660 so auf Rev.5 zuverlässig Frames liefert, ist **nicht geprüft**.
- Eine Gefahr durch zu hohe LED-Leistung besteht nach diesen Werten nicht. Das Risiko ist Funktionsverlust, nicht Überlast.

### 5.2 Prüfung

**Bauen** (Phase 3, IDF 5.4.2, wie in der Übergabe Abschnitt 5):
- Alle S3-Konfigurationen:
  - `facefocusvr_eye_L`, `facefocusvr_eye_R`, `facefocusvr_face`
  - `project_babble`, **`venti_N8R8`** (neu)
  - **`wrooms3N8R2`**, **`wrooms3QION8R2`**, **`wrooms3N8R8`** (neu oder umbenannt)
  - `wrover`, `esp_eye`, `seed_studio_xiao_esp32s3`
- Die drei klassischen ESP32 im Worktree `wt32`.
- Immer mit `-D IDF_TARGET=…`.

**Bitvergleich** (`tools/compare_builds.py`) gegen Baseline 3 aus Phase 0:
- Nach dem Merge-Commit: `facefocusvr_eye_L/R/face`, `project_babble` und `wrooms3N8R2` gegen `baseline3/wrooms3` → **identisch erwartet**. Das ist der Kernbeweis, dass nichts verloren ging und nichts sich verschob.
- Nach Folge-Commit 1 (Tool) und 3 (Doku): identisch, keine Firmware-Datei betroffen.
- Nach Folge-Commit 2 (XCLK-Name): `app.bin` identisch; `sdkconfig.h` weicht nur im Symbolnamen ab. Das Werkzeug meldet `sdkconfig.h` als verschieden, dann von Hand prüfen, dass nur der Name abweicht.

**Tests in `tests/`** (pytest, am Gerät): Es braucht eine `tests/.env` mit `WIFI_SSID`, `WIFI_BSSID`, `WIFI_PASS`, `SWITCH_MODE_REBOOT_TIME`, `WIFI_CONNECTION_TIMEOUT` und `INVALID_WIFI_CONNECTION_TIMEOUT`. Sie ist hier **nicht vorhanden**; für die kabelgebundenen FFVR-Boards reichen Platzhalter bei WLAN.
- `test_commands.py` auf allen drei FFVR-ESPs (`--board facefocusvr_eye_L --connection COMxx` usw.).
- `test_fan.py` auf eye_R (Fähigkeit `fan`).
- Diese Tests decken Kommandos, Modi und Lüfter ab, nicht die Kamera-Recovery.

**Werkzeuge in `tools/`** auf der Rev.5-Platine:
- `camera_recovery_check.py status` und `recover --count 5 --watch` je ESP (UVC-Modus): Recovery und Gate.
- `camera_power_bench.py --cycles 3 --trace` auf einem ESP (Setup-Modus): Rail-Check `collapsed`.
- `setup_openiris.py --port COMxx` unter Windows: prüft Folge-Commit 1 und R3.
- `camera_budget_check.py` **nicht nötig**, solange der Bitvergleich identisch ist. Er braucht ein Test-Image und dauert 7 bis 12 min.

**Hardware** (Rev.5): Ist der Bitvergleich für alle drei FFVR-Images identisch, ist ein Hardwaretest formal nicht nötig; dasselbe Image lief schon. Als Absicherung trotzdem kurz:
1. Sync-Stand auf alle drei ESPs flashen, vorher `erase-flash`.
2. Drei Streams, 28 bis 30 fps, ohne Lücke.
3. Je zwei `recover_camera` im Stream (Power-Cycle, `collapsed`, rund 1,2 s).
4. `get_camera_status` und `get_who_am_i`.
5. Lüfterstatus auf eye_R.
6. Ein Verbinden mit `setup_openiris.py` ohne Board-Reset.

Das ist ungefähr eine halbe Stunde.

**Nicht abgedeckt, bleibt offen:** Venti und `wrooms3N8R8` auf Hardware. Ich habe keine; bauen muss reichen.

---

## 6. Entscheidungen (2026-09-30)

| Frage | Entscheidung | Wo umgesetzt |
|---|---|---|
| **F1** CI-Matrix | FFVR bleibt draußen. Grund: keine zweite, unkontrollierte Firmwarequelle; das Produkt wird lokal gebaut, geprüft und mit dem eigenen Werkzeug geflasht. Der Grund kommt in den Merge-Text. Beleg, dass `bc7c404` gewollt war: der Release-Text von v1.2.2. Die CI-Prüfung entfällt | 2a.5, Phase 1, Abschnitt 7.2 (Releases) |
| **F2** XCLK-Symbol | Zurück auf `CAMERA_USB_XCLK_FREQ`. Overrides und 40-MHz-Bereich bleiben, Begründung in den Kconfig-Hilfetext. Eigener Folge-Commit | 2a.1, Phase 2 |
| **F3** IDF | Abgleich auf 5.4.2, Treiberkopie bleibt. IDF-Sprung als eigenes Thema nach der ESD-Prüfung | 1.1, Phase 5 |
| **F4** Upstreams FFVR | Ausgeliefert wird nur aus dem eigenen Repo. Upstreams FFVR bleibt ein allgemeiner Stand, nichts geht hinüber. Upstreams FFVR-Releases sind ein bekanntes Risiko | R11 |
| **F5** Datei | Eine Datei wird aus Lizenzgründen entfernt, einschließlich Historie. Eigene Aufgabe vor dem Abgleich | Abschnitt 7 |
| **F6** Venti | Kein OV3660-Override für Venti. Bekannter, gewollter Unterschied | 2d |
| **F7** Remote | `upstream` als fester Remote, nur lesend. `0.2.1rc0` lokal löschen. Eine Push-Sperre ist vorgeschlagen, eingebaut wird sie nur nach OK | Phase 1 |

Weitere Festlegungen:
- **`sdkconfig`:** bei jedem Abgleich ganz die eigene Seite (2a.3).
- **Baseline 3:** wird in Phase 0 noch als `baseline3/wrooms3` gebaut und nach dem Merge mit `--board wrooms3N8R2` verglichen.
- **Upstream:** nichts, auch kein Issue und kein Kommentar (1.2).
- **Phase 4:** Freigabe erst nach dem Bericht zu Phase 3, mit dem Ergebnis des Bitvergleichs.

---

## 7. F5: Entfernte Datei, Zweige und Releases

- Am 2026-09-30 wurde eine Datei aus Lizenzgründen aus dem Repo entfernt.
- Die Historie wurde dazu am selben Tag bereinigt.
  - Dabei haben sich die Kennungen ab dem betroffenen Commit geändert. Die Dokumente nennen die neuen, ältere Fassungen in der Historie behalten die alten.
  - Die Zuordnung alt → neu liegt außerhalb des Repos.
  - Ältere Commits und die gemeinsame Basis mit Upstream (`6971464`) sind unverändert.
- Die Aufarbeitung mit allen Einzelheiten liegt bewusst nicht im Repo.

### 7.1 Aufgeräumte Zweige (2026-09-30)

Gelöscht wurden, auf GitHub und lokal, Zweige, die nicht mehr gebraucht werden.

**Vollständig in `main` enthalten.** Die Commits bleiben dort erhalten:
- `Driver-PLL-override`, `LogManager`, `OV3660-improvement`, `V1.2.4_branch` (= Tag `v1.2.4`), `Version-for-Lab`, `feature/camera-power`, `v1.2.2_branch` (= Tag `v1.2.2`);
- der Arbeitszweig der Entfernung;
- die drei Zweige, die es nur lokal gab. Die Dokumente nennen sie weiterhin:

| Zweig | Spitze | Inhalt |
|---|---|---|
| `fix/esp-timer-units` | `c59c41c` | Kategorie-B-Fix (AP0): `esp_timer` bekommt Mikrosekunden, also `restart_device` nach 2 s statt 2 ms und `startStreamingCommand` nach 150 ms statt 150 µs. Basis von `feature/camera-power`. |
| `fix/i2c-nack-busy-wait` | `7a04b60` (davor `b80d4fe`) | `esp_driver_i2c` aus IDF 5.4.2 als Projektkopie, plus begrenzte Warteschleife nach NACK aus IDF ≥ 5.5.2 und Versionsschutz. Gemergt in `daedcb0`. |
| `fix/serial-no-reset-on-connect` | `2fad53c` | `tools/openiris_device.py` öffnet den Port mit DTR/RTS low, damit Verbinden das Board nicht neu startet. Gemergt in `03f1e7b`. |

**Mit reproduzierbarem Inhalt:** die vier Zweige `kannweg_I-hate-m,y-life_3995545`, `kannweg_commands-for_3995545`, `kannweg_explicit-D+_-000530e` und `kannweg_fixed_5313f41`.
- Das waren Testbuilds vom 2026-04-24. Jeder stellte nur das eingecheckte `sdkconfig` eines älteren Stands auf eye_R um.
- Die Werte entsprechen der eye_R-Board-Datei des jeweiligen Stands. Beim letzten kam „Debug-Log aus“ dazu, damals der Kconfig-Standardwert.

Auf GitHub bleiben `main`, `LUT`, `OV3660`, `fixes`, `v1.0.1_branch` und `v1.0.2_branch`.

### 7.2 Releases v1.0.0 bis v1.2.4

**Entscheidung:** Alle fünf Tags werden auf GitHub gelöscht, die Releases werden damit zu Entwürfen.
- Grund: Die Releases bieten FFVR-Images an, also die zweite Firmwarequelle, die es nach F1 und F4 nicht geben soll.
- Die CI-Zips anderer Boards dürfen mit verschwinden.
- Reihenfolge: `v1.0.0` als Probe, dann `v1.0.1` und `v1.0.2`, zuletzt `v1.2.4` und `v1.2.2`.

Stand vor dem Löschen (GitHub-API, 2026-09-30):

| Release | veröffentlicht | Tag → Commit | in `main` | Anhänge |
|---|---|---|---|---|
| v1.0.0 | 2025-10-18 | `1a54226` (Upstream-Merge PR #16) | ja | 3 FFVR-Images 1.0.0 |
| v1.0.1 | 2026-01-02 | `0ed6037` (`v1.0.1_branch`) | nein | 3 FFVR-Images 1.0.1 |
| v1.0.2 | 2026-02-19 | `65ceea7` (`v1.0.2_branch`) | nein | keine |
| v1.2.2 | 2026-04-22 | `8ca8292` | ja | 3 FFVR-Images 1.2.2 und 9 CI-Zips anderer Boards |
| v1.2.4 | 2026-04-28 | `aa471cb` | ja | 3 FFVR-Images 1.2.4 und 9 CI-Zips anderer Boards |

Die Commits bleiben erhalten: `v1.0.1` und `v1.0.2` über ihre Zweige, die übrigen über `main`.

**FFVR-Images auf GitHub:** Alle sind per SHA-256 identisch mit den Dateien in `../ffvr-multiflash/fw/`.

| GitHub-Anhang | SHA-256 | lokale Kopie |
|---|---|---|
| `FFVR.Eye.L.1.0.0.bin` | `c1b4cbb49bbfd216809e4449f386a9328ad6fa5d96dcf659f67cca62d7a1285e` | `fw/100/FFVR Eye L [1.0.0].bin` |
| `FFVR.Eye.R.1.0.0.bin` | `6b09d8170b897bf8545a7b7b0bc85b7b9168b8cf6d685a32454bbb5d648ad7a1` | `fw/100/FFVR Eye R [1.0.0].bin` |
| `FFVR.Face.1.0.0.bin` | `83485734c0b88ee2983cfd7eab48cc71e3ac854d1e561bfa59b819a2ab7bc7a6` | `fw/100/FFVR Face [1.0.0].bin` |
| `FFVR.Eye.L.1.0.1.bin` | `7ee61bcc5f9f7a5465eb3849fea21e283016bba41e270dea0f1aad3352af19c5` | `fw/101/FFVR Eye L [1.0.1].bin` |
| `FFVR.Eye.R.1.0.1.bin` | `8d6fc2b68238e6442bb021d500bacb5027c5c82082b00a12720b0626f66e1ea0` | `fw/101/FFVR Eye R [1.0.1].bin` |
| `FFVR.Face.1.0.1.bin` | `fd0e91ce532d7a4678d424c96c2d8bcda0694e864ba80936ac9b0b2adc80beb5` | `fw/101/FFVR Face [1.0.1].bin` |
| `FFVR.Eye.L.1.2.2.bin` | `0ed83b7559f106b17cbff36fbabd9e6de1b856a4e2c6e04875eb49a4d1725968` | `fw/122/FFVR Eye L [1.2.2].bin` |
| `FFVR.Eye.R.1.2.2.bin` | `11e53469b47ce9e36274c4f6cef2397843d12957ac1259a6c5efc550864faa4a` | `fw/122/FFVR Eye R [1.2.2].bin` |
| `FFVR.Face.1.2.2.bin` | `4cba1210848bc73232c14b93451faeaab1ee000dbcbe30949dfb2687ba205e41` | `fw/122/FFVR Face [1.2.2].bin` |
| `FFVR.Eye.L.1.2.4.bin` | `bcf3ad30a6421eec913b2fde10a4d61512d233ab7052dabf816193eddc2baafe` | `fw/124/FFVR Eye L [1.2.4].bin` |
| `FFVR.Eye.R.1.2.4.bin` | `5cbd067be96dc9b3512b95d1426b307845ad1e06744ec96fc7fd356e2d3be58b` | `fw/124/FFVR Eye R [1.2.4].bin` |
| `FFVR.Face.1.2.4.bin` | `cff6681bd48cdba9b95cf3889bdc9f366090fb98636360dc0ecbb5f2b8c8d383` | `fw/124/FFVR Face [1.2.4].bin` |

Für v1.0.2 gab es auf GitHub keine Images. Lokal liegen sie unter `fw/102/`.

**Kern der Release-Texte.** Wörtlich stehen sie in Anhang A.
- **v1.0.0:** erste Firmware für FaceFocusVR.
- **v1.0.1:** wie v1.0.0, Augenringe 45 % → 100 %.
- **v1.0.2:** wie v1.0.0, Augenringe 45 % → 100 % und Gesicht 85 % → 100 %.
- **LED-Ring-Warnung (ab v1.0.1):** Diese Firmware nicht flashen, wenn die erste Ausgabe des LED-Rings bzw. des Gesichts verbaut ist (LEDs nicht gekapselt, sichtbar). Ist das Bild nach dem Flashen stark überbelichtet, v1.0.0 flashen.
- **v1.2.2:** **„DO NOT USE, COMMIT 000530E BROKE THE AUTO UPDATE FUNCTION“**. Leistung: Augen 75 %, Gesicht 100 %. Dieselbe LED-Ring-Warnung. Änderungsliste zu Kamera/UVC, Lüfter/LED, LogManager und USB. Unter „Miscellaneous“ steht auch „Removed FFVR boards from the build matrix“, ein Hinweis zu F1.
- **v1.2.4:** Augen 75 %, Gesicht 100 %, dieselbe LED-Ring-Warnung. Dazu:
  - OV3660-Stabilität, XCLK 27 → 20 MHz;
  - Build-Fix für den klassischen ESP32;
  - Warmstart zurück in den Setup-Modus für das Update-Werkzeug;
  - Übergabe JTAG → UVC unter Windows repariert;
  - USB-Trennfenster 200 → 300 ms.

**Was beim Löschen eines Tags passiert:**
- Der veröffentlichte Release wird zum **Entwurf**: für die Öffentlichkeit unsichtbar, für dich weiter sichtbar, samt angehängten Dateien. Die „Source code“-Knöpfe entfallen. Wird der Tag wieder gepusht, lässt sich der Release erneut veröffentlichen.
  - Quelle ist ein GitHub-Mitarbeiter im Community-Forum (Diskussion #7008, 2021). Die offizielle Doku sagt dazu nichts.
- Ein **Release zu löschen** entfernt Eintrag und Anhänge, der Tag bleibt (`gh release delete` hat dafür eine eigene Option `--cleanup-tag`).
- **Ohne Anmeldung sind Entwürfe nicht sichtbar.** Über die API lässt sich nur prüfen, dass Release und Anhänge öffentlich verschwunden sind. Ob der Entwurf mit Anhängen noch da ist, zeigt nur die Release-Seite, wenn man angemeldet ist.
- **Selbst im schlechtesten Fall geht nichts verloren:** Alle Images liegen byteidentisch lokal, die Release-Texte stehen wörtlich in Anhang A. Jeder Release ließe sich daraus neu anlegen.

**Ergebnis (2026-09-30):**
- **Alle fünf Tags sind auf GitHub gelöscht.** `git ls-remote origin` zeigt keine Tags mehr. `v1.0.1_branch` und `v1.0.2_branch` stehen unverändert, lokal gibt es die Tags weiter.
- Direkt nach jedem Löschen meldete GitHub noch etwa eine Minute lang den alten Stand, weil öffentliche Antworten zwischengespeichert werden. Geprüft wurde deshalb mit Abfragen, die den Zwischenspeicher umgehen.
- **v1.0.0, v1.0.1, v1.0.2:** öffentlich weg. Release-Seite, Release-Endpunkt und Anhänge liefern 404, die Releases fehlen in der öffentlichen Liste. Ob sie als Entwürfe mit Anhängen existieren, zeigt nur die angemeldete Ansicht; das steht noch aus.
- **v1.2.2, v1.2.4:** Sie sind nach dem Löschen der Tags nicht zu Entwürfen geworden und blieben als veröffentlicht stehen, auch Stunden später noch.
  - Ein Unterschied zu den drei anderen: Ihre Zielzweige (`V1.2.4_branch`, `v1.2.2_branch`) waren schon gelöscht. Ob das der Grund ist, ist nicht belegt.
  - Vorher geprüft: Die Images sind per SHA-256 und Größe gleich den lokalen Kopien, die Release-Texte gleich Anhang A. Die Metadaten sind außerhalb des Repos gesichert.
- **Entscheidung (2026-09-30): Nichts wird gelöscht.** Die Releases sollen als Entwürfe bestehen bleiben, damit nur der Inhaber Zugang hat.
  - Images werden nicht neu hochgeladen. Ob 1.2.x wieder angeboten wird, wird später entschieden.
- **Nachprüfung (2026-09-30): Nichts ist verloren, nichts wird neu angelegt.**
  - **v1.0.0 bis v1.0.2** sieht der Inhaber angemeldet als Entwürfe. Der zeichengenaue Vergleich ihrer Texte mit Anhang A folgt, sobald die GitHub-CLI angemeldet ist.
  - **v1.2.2 und v1.2.4** fehlen in jeder Liste, weil ihr Tag fehlt. Gelöscht sind sie nicht:
    - Text, Originaldatum, Ersteller und alle Anhänge sind unverändert, gegen die Sicherung geprüft.
    - Sie werden per API auf Entwurf gestellt (`draft: true`, „true makes the release a draft“) statt neu angelegt. So entstehen keine Dubletten, und die Originale bleiben erhalten.
    - Die CI-Zips anderer Boards bleiben dran; als Entwurf sind sie nicht öffentlich.
    - Es wird kein Tag angelegt, also entsteht auch kein „Source code“-Zip.
  - Weil nichts neu angelegt wird, gibt es keinen wiederhergestellten Release mit neuem Datum oder neuem Ersteller.
- **Festgehalten für das nächste Mal:** Dass ein Release nach dem Löschen seines Tags zum Entwurf wird, ist nur durch den Forumsbeitrag von 2021 belegt. Hier traf es für drei von fünf Releases zu. Die übrigen zwei blieben veröffentlicht, aber ungelistet. **Tags von Releases deshalb nicht löschen**, sondern Releases direkt per API auf Entwurf stellen.

### 7.4 Abschluss F5 (Stand 2026-09-30)

- **Auf GitHub:**
  - `main` = `7872367` mit bereinigter Historie. Darunter liegt der Versions-Commit 1.3.3, `5bd8f39`, mit dem Tag `v1.3.3`.
  - Unverändert sind die Zweige `LUT`, `OV3660`, `fixes`, `v1.0.1_branch` und `v1.0.2_branch`. Weitere Tags gibt es nicht.
  - Releases: v1.0.0 bis v1.0.2 sind Entwürfe. v1.2.2 und v1.2.4 stehen ohne Tag und ungelistet, bis sie auf Entwurf gestellt sind.
- **Sicherungen**, lokal und außerhalb des Repos; sie bleiben bis zum Ende des Upstream-Abgleichs und werden erst nach Ansage aufgeräumt:
  - `Sicherung_F5`: vollständige Kopie des Arbeitsordners vor dem Bereinigen, dazu ein Git-Bundle aller Stände;
  - `OpenIris-F5`: die Wegwerf-Klone des Bereinigens;
  - `OpenIris-ESPIDF_alt_vor_F5`: der alte Arbeitsordner, mit seinen weiter angebundenen Build-Worktrees `wt_alt` und `wt32_alt`;
  - Images: 1.0.0 bis 1.2.4 in `ffvr-multiflash/fw/`, 1.3.2 und 1.3.3 unter `OpenIris-refbuilds/release_1.3.x/`;
  - Release-Texte in Anhang A, Release-Metadaten außerhalb des Repos.
- **Offen von F5**, beides nach der Anmeldung der GitHub-CLI:
  - v1.2.2 und v1.2.4 auf Entwurf stellen und prüfen;
  - die Texte der v1.0.x-Entwürfe mit Anhang A vergleichen.

  Danach ist F5 abgeschlossen. Das Aufräumen der Sicherungen folgt nach dem Abgleich.

### 7.3 Reproduzierbarkeit: bei 1.3.2 nicht gegeben, ab 1.3.3 belegt

Zu klären, bevor Schritt 3 freigegeben wird: Fließt etwas davon ins Image?
- Commit-Kennung, `git describe`, Dirty-Markierung, Zweigname,
- Bauzeitpunkt,

egal ob über `PROJECT_VER`, den App-Deskriptor von IDF oder eine erzeugte Kopfdatei.

- **Wenn nein:** Alles ist gut.
- **Wenn ja:** Geflashtes geht nicht verloren, die Dateien sind gesichert. Verloren ginge aber die Möglichkeit, genau dieses Image neu zu bauen.
  - Dann wird die Version festgeschrieben, statt sie aus Git zu holen, als kleiner eigener Commit vor dem Umschreiben.
  - Vorher steht hier, was genau am Git-Zustand hängt und wo.

Die Vergleiche über Commits hinweg waren identisch, aber nur bei den Referenzbuilds. Diese setzen in `tools/compare_builds.py` `APP_REPRODUCIBLE_BUILD` und `APP_PROJECT_VER="reference"` fest, die normalen Produktbuilds nicht. Die identischen Vergleiche beantworten die Frage deshalb nicht.

**Ergebnis (2026-09-30): Nein. Das 1.3.2-Image lässt sich schon heute nicht byteidentisch neu bauen, auch ohne Umschreiben.**

Geprüft an den drei `merged.bin` unter `../OpenIris-refbuilds/release_1.3.2/` und an deren Build-Konfiguration. Am Git-Zustand und am Bauzeitpunkt hängt der App-Deskriptor im Image (ab Offset 0x10020 der Merge-Datei):

1. **Version aus `git describe`.**
   - Gesetzt ist weder `PROJECT_VER` in `CMakeLists.txt` noch `version.txt` noch `CONFIG_APP_PROJECT_VER_FROM_CONFIG`. IDF nimmt dann `git describe` (`tools/cmake/project.cmake`, Zeilen 669–708).
   - Im Image steht `v1.2.4-42-g` plus die Kurzkennung des 1.3.2-Build-Commits, bei **eye_R und face zusätzlich `-dirty`**.
   - Der Wert hängt am Tag `v1.2.4` (auf GitHub gelöscht, lokal noch da), an der Commit-Kennung und an nicht committeten Änderungen beim Bauen.
2. **Bauzeit und Datum:** `CONFIG_APP_COMPILE_TIME_DATE=y`, im Image bei eye_L `01:44:13` und `Sep 29 2026`. Jeder neue Build hat andere Werte.
3. **ELF-Prüfsumme im Deskriptor:** Sie ändert sich mit 1 und 2 mit.

Weitere Befunde:
- `CONFIG_APP_REPRODUCIBLE_BUILD` ist aus.
- Host-Pfade habe ich im Image nicht gefunden.
- Die Firmware liest den Deskriptor nicht. Sichtbar ist diese Version nur im Boot-Log und über `esptool image_info`. Was das Gerät über `get_who_am_i` als Version meldet, ist `CONFIG_GENERAL_VERSION` (1.3.2) und hängt nicht an Git.
- **„dirty“ bei eye_R und face:** Die Ursache lässt sich nicht mehr feststellen. Wahrscheinlich war es die Lock-Datei, denn das Build-Log meldet „update your lock file“. Belegt ist das nicht. Die ausgelieferten Images für eye_R und face stammen damit aus einem Stand, der so in keinem Commit steht.

**Entschieden (2026-09-30): Version festschreiben, als neue Version 1.3.3.** Ein eigener Commit vor dem Umschreiben, nur in `boards/facefocusvr/*`, damit andere Boards bitgleich bleiben:
- `CONFIG_APP_PROJECT_VER_FROM_CONFIG=y` und `CONFIG_APP_PROJECT_VER="1.3.3"`;
- `CONFIG_GENERAL_VERSION="1.3.3"`, damit `get_who_am_i` dieselbe Version meldet wie das Boot-Log. Beide Werte werden bei jedem Release angehoben;
- `CONFIG_APP_REPRODUCIBLE_BUILD=y`. Laut IDF-Hilfe entfernt das „all date, time, and path information“, genau wie bei den Referenzbuilds.

Bedingungen:
- Vorher wird geklärt, woher `-dirty` kam.
- Gebaut wird nur aus einem sauberen Arbeitsbaum, `git status` ist leer.
- Alle drei Images werden zweimal gebaut und per SHA-256 verglichen. Das Ergebnis liegt vor, bevor der Commit gepusht wird.
- Die 1.3.3-Bins bleiben lokal: kein Release, kein Tag, nichts auf GitHub.

Danach ergibt derselbe Commit mit derselben IDF und demselben Compiler ein byteidentisches FFVR-Image, unabhängig von Tags, Kennungen und Bauzeit. Das am 2026-09-29 gebaute 1.3.2 bleibt nur in der Sicherung.

**Ergebnis 1.3.3 (2026-09-30)**, Versions-Commit `5bd8f39`:
- Alle drei Images sind zweimal gebaut worden, jeweils in einem frischen Ordner und aus einem sauberen Arbeitsbaum. Die beiden Durchgänge sind **byteidentisch**, einzeln auch für App, Bootloader und Partitionstabelle.
- Im Image stehen Version `1.3.3` und keine Zeit, kein Datum. `get_who_am_i` meldet 1.3.3.

  | Image | SHA-256 `merged.bin` |
  |---|---|
  | eye_L | `183dcd15922ff93f52a88ed9165ee385953c135ff1ddaae43d13ed1ec2e0b2a3` |
  | eye_R | `51187d818358f30cb2f179bdf119c19551624659667b2afe7339e71cce6666d6` |
  | face | `02db73f572650df85ed9a0b8044ee5070fe8d9c6f25bbf80cf2220e40ab9bb1c` |

- Ein eye_L-Build aus der bereinigten Historie, also mit anderer Commit-Kennung und in einem anderen Ordner, ergab denselben Hash. Das Bereinigen hat die Firmware nicht berührt.
- Die Bins liegen nur lokal unter `../OpenIris-refbuilds/release_1.3.3/` (`FFVR Eye L [1.3.3].bin` usw.): kein Release, kein Tag. Die Hardware trägt weiter 1.3.2.

---

## Anhang A: Release-Texte wörtlich

Gelesen über die GitHub-API am 2026-09-30, bevor die Tags gelöscht wurden. Die Texte sind unverändert übernommen, einschließlich Tippfehlern.

### v1.0.0 (veröffentlicht 2025-10-18, Release-ID 255552803)

```text
Initial firmware release for FaceFocusVR boards.
```

### v1.0.1 (veröffentlicht 2026-01-02, Release-ID 273856991)

```text
This firmware is the same as v1.0.0, but with increased power output for the eye rings (45% → 100%).
In the newer IR ring design, the LEDs are encased in an additional plastic layer, which reduces light output. Therefore, more power is required to ensure the light remains clearly visible through the plastic.

DO NOT FLASH THIS FIRMWARE IF YOU ARE USING THE FIRST EDITION OF MY LED RING (LEDs not encased and visible).

After flashing, check the brightness in Babylonina. If the image is extremely bright (overexposed), flash v1.0.0 instead.

**Full Changelog**: https://github.com/PhosphorosVR/OpenIris-ESPIDF/compare/v1.0.0...v1.0.1
```

### v1.0.2 (veröffentlicht 2026-02-19, Release-ID 288102094)

```text
This firmware is the same as v1.0.0, but with increased power output for the eye rings (45% → 100%) and increased power output for face leds (85% → 100%).

In the newer IR ring/face design, the LEDs are encased in an additional plastic layer, which reduces light output. Therefore, more power is required to ensure the light remains clearly visible through the plastic.

DO NOT FLASH THIS FIRMWARE IF YOU ARE USING THE FIRST EDITION OF MY LED RING/FACE (LEDs not encased and visible).

After flashing, check the brightness in Babylonina. If the image is extremely bright (overexposed), flash v1.0.0 instead.

**Full Changelog**: https://github.com/PhosphorosVR/OpenIris-ESPIDF/compare/v1.0.0...v1.0.2
```

### v1.2.2 (veröffentlicht 2026-04-22, Release-ID 312326369)

```text
DO NOT USE, COMMIT 000530E BROKE THE AUTO UPDATE FUNCTION LOL

**Power Output:**
Eyes: 75%
Face: 100%

**Important Notice**
Do not flash this firmware if you are using the first edition of the LED ring/face (LEDs not encased and visible).

<br>
</br>


**Camera & UVC**
- Added sensor-specific handling for OV3660 and OV2640 (frame sizes, profiles, tuning)
- Introduced per-sensor XCLK frequency overrides and updated default USB XCLK configuration
- Improved UVC streaming stability through better pacing and bandwidth handling
- Fixed buffer management issues and optimized frame transfer
- Multiple iterations on OV3660 initialization and stability (including reverts where necessary)

**Fan & LED Control**
- Introduced configurable PWM limits (FAN_PWM_DUTY_MIN / FAN_PWM_DUTY_MAX)
- Added conditional fan control via CONFIG_FAN_PWM_ENABLE
- Implemented fan duty cycle linearization with LUT generator
- Updated fan and LED configuration; removed LED current monitoring

**System & Stability**
- Introduced camera profiles with sensor-specific configuration sets
- Added mutexes and general stability improvements
- Improved error handling in FanManager and SerialManager
- Addressed camera initialization issues

**Logging & Debugging**
- Added LogManager with support for RAM and persistent logging
- Implemented commands for retrieving, clearing, and managing logs
- Added configuration and runtime control for debug logging

**USB & Low-Level Improvements**
- Improved USB disconnect detection
- Enhanced USB initialization with proper error reporting and logging

**Miscellaneous**
- Removed FFVR boards from the build matrix
- General cleanup and minor fixes





**Full Changelog**: https://github.com/PhosphorosVR/OpenIris-ESPIDF/compare/v1.0.0...v1.2.2
```

### v1.2.4 (veröffentlicht 2026-04-28, Release-ID 314877072)

```text
Important Notice
Do not flash this firmware if you are using the first edition of the LED ring/face (LEDs not encased and visible).

FFVR Power Output:
Eyes: 75%
Face: 100%


- OV3660 sensor stability improvements
- OV3660 XCLK frequency lowered from 27 MHz to 20 MHz
- Build fix for classic ESP32 (LEDC_USE_XTAL_CLK guard)
- Reliable warm reset back into SETUP/boot mode for the update tool
- Fixed JTAG → UVC handover on Windows (no more re-enumeration as JTAG 0x1001)
- USB disconnect window bumped 200 ms → 300 ms



**Full Changelog**: https://github.com/PhosphorosVR/OpenIris-ESPIDF/compare/v1.2.2...v1.2.4
```
