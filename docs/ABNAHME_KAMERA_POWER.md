# Abnahme: Kamera-Stromschaltung und Recovery (FaceFocusVR)

Stand 2026-09-28. Geprüft wurde der Zweig `feature/camera-power` auf Stand `cdfbd91` gegen die Ziele des Nutzers. Aus der Abnahme folgten vier Änderungen (Abschnitt 5). Danach ging der Zweig nach `main`.

Dieses Dokument beantwortet die Frage, **warum das Feature so gebaut ist**. Messwerte, Plan und Verlauf stehen in [CAM_CE_RESET_Analyse.md](CAM_CE_RESET_Analyse.md), die Arbeitsregeln in [UEBERGABE_KI.md](UEBERGABE_KI.md).

Grundlage der Prüfung:
- der Diff `main...feature/camera-power`,
- die betroffenen Stellen im Kameratreiber (`components/esp32-camera`) und in ESP-IDF 5.4.2,
- eine Zählung der Firmware-Zeilen nach Zweck.

Messergebnisse stammen aus Abschnitt 17 der Analyse. Am Code lassen sie sich nicht nachprüfen.

---

## 1. Die Ziele

Bewertet ist der Stand nach den Änderungen aus Abschnitt 5.

**1. Eine Kamera per Kommando zurücksetzen und stromlos machen: erreicht, das Aus als Zyklus.**
- `recover_camera` kennt `reinit`, `reset`, `power_cycle` und `auto` und läuft über Serial wie über CDC.
- Im Setup-Modus gibt es `camera_power_cycle` mit bis zu 30 s Aus-Zeit.
- Beide laufen durch dieselbe Sequenz, `CameraManager::runCycle()` in `CameraCycle.cpp`.
- Einen dauerhaften Aus-Zustand gibt es bewusst nicht. Ein Aus-Flag hätte im UVC-Modus nicht geschützt: Öffnet der Host den Stream, während die Kamera aus ist, greift der UVC-Start auf einen Sensor zu, den es nicht mehr gibt. Für den Blick mit dem Multimeter reichen 30 s.

**2. Das funktioniert im laufenden UVC-Stream, ohne Absturz und ohne Neuöffnen: erreicht.**
- UVC holt und gibt Frames nur über `cameraAcquireFrame()`/`cameraReleaseFrame()`, also durch das Frame-Gate (`CameraGate.cpp`).
- Die Sequenz schließt das Gate vor dem Abbau und öffnet es erst nach dem ersten neuen Frame. Den Sensorzeiger setzt sie unter dem Mutex auf null.
- Der Host sieht nur eine Lücke von etwa 1,0 s (17.7). Über 150 Recoveries im Stream liefen mit konstantem Heap.
- Geprüft ist das unter Windows. Das ist der Host des Produkts, später vielleicht Linux; andere Hosts muss niemand betrachten (Entscheidung des Nutzers).

**3. Es funktioniert automatisch, wenn eine Kamera ausfällt: erreicht, mit einer klaren Grenze.**
- `frame_timeout`: Ein Konsument bekommt kein Frame.
- `boot_failure`: Der erste Init scheitert. Dann folgen bis zu zwei Neustarts statt einem, weil an der Kamera mit dem geknickten Kabel etwa jeder zehnte Neustart scheiterte und der nächste immer gelang.
- Grenze: Als Ausfall zählt nur „keine Frames". Liefert der Sensor nach einer Entladung weiter Frames, aber mit verstellten Registern, erkennt das nichts.
- Die Erkennung braucht einen streamenden Host und dauert 4 s, auf dem S3 bis 8 s (Treiber-Timeout, GDMA-Reset, zweiter Timeout in `cam_take()`).

**4. Nichts davon hängt daran, dass I2C noch funktioniert: erreicht, unter einer Bedingung.**
- Erkannt wird ein Timeout im DMA-/VSYNC-Pfad. Abgeschaltet wird mit Gate, `esp_camera_deinit()`, GPIO und ADC.
- Der Deinit macht keinen Bustransfer. Das I2C-Gerät lässt sich nur mitten in einer Transaktion nicht entfernen, und jeder abgeschlossene Aufruf hinterlässt einen Endstatus.
- I2C braucht erst der Wiederanlauf. Den Erfolg misst ein DVP-Frame, kein Register.
- Bedingung: Jeder I2C-Aufruf muss zurückkehren. Das gilt nur dank des Backports in `components/esp_driver_i2c` (F24). Ohne ihn hing der Kamera-Task auf Hardware für immer, wenn die Kamera SDA festhielt.
- Läuft beim Abbau gerade ein SCCB-Zugriff, verzögert er den Stopp um dessen Timeouts von je 1 s.

**5. Im Aus-Zustand liegt an keinem Kamerapin eine Spannung, die dort nicht hingehört: erreicht. Belegt ist das teils durch Messung, teils nur durch Code.**
- Bevor CE fällt, ist der Treiber unten. `parkPins()` hat XCLK und die elf DVP-Leitungen auf Eingang mit Pull-down gesetzt, SDA und SCL auf Eingang ohne Pulls.
- RESET geht vor CE auf low und bleibt bis 20 ms nach der CE-Freigabe unten. An RESETB liegen dabei rechnerisch 0,3 V (Teiler 1 k zu 10 k), weniger als eine Diodenspannung.
- Gemessen: 2V8_Cx lag am Ende jeder Aus-Zeit unter 0,45 V, 1V5_Cx laut Multimeter bei 0 V.
- Nur per Code belegt sind XCLK und SCCB. Der Rail-Check sieht sie nicht (Befund C, Falle 1).
- Bewusste Ausnahme: Zum Messen werden D0 und D6 kurz hochgezogen, je Messung unter 1 ms mit etwa 60 µA.

**6. Auf Rev.4 und Rev.4.5 läuft derselbe Code, tut weniger und meldet das ehrlich: erreicht.**
- Es gibt ein Image je Rolle. Was ein Board kann, entscheidet die Leitungsprobe beim Boot (`CamLines::probe()`).
- Ohne Leitungen wählt `auto` die Stufe `reinit` und meldet `degraded: true`.
- `reset`, `power_cycle` und das Bench-Kommando ohne `force` enden mit `not_supported`, bevor etwas angefasst wird.
- Ein unklares Probe-Ergebnis zählt wie „absent" und wird als `unknown` gemeldet. Mit `force` kann der Rail-Check dort nie `collapsed` sagen.
- Geprüft ist das auf einer Platine Rev.4/4.5. Per Software lassen sich die beiden nicht unterscheiden, laut Netzliste sind sie an diesen Pads gleich.

**7. Keine Endlosschleife, aber auch kein Deckel, der mitten in einer ESD-Prüfung zu früh greift: erreicht nach der Änderung aus Abschnitt 4.**
- Vorher zählte das Ratenfenster (10 automatische Versuche in 10 min) auch gelungene Erholungen. Es wäre nach dem zweiten Prüfpunkt aufgebraucht gewesen.
- Die Sperre nach drei Fehlschlägen hielt, bis jemand ein Kommando schickte.
- Beides ist ersetzt. Die neuen Regeln stehen in Abschnitt 4.
- Auf Hardware geprüft am 2026-09-29 (Abschnitt 4): 11 gehaltene Erholungen ohne Sperre, Sperre nach drei nicht gehaltenen, Selbstauflösung nach 306 bis 313 s, ESP-Neustart höchstens einmal.

**8. Andere Boardkonfigurationen sind unberührt, das Feature ist opt-in und wird sonst nicht kompiliert: erreicht für das Feature. Der Zweig ändert aber bewusst alle Boards.**
- Alle Symbole sind per Default aus und werden nur in `boards/facefocusvr/*` gesetzt.
- Die Quellen kommen nur per `if(CONFIG_…)` in den Build, neue Abhängigkeiten nur optional. Im gemeinsamen Code stehen nur `#if`-Einhängepunkte und `always_inline`-Wrapper. Die Board-Identität wird nirgends abgefragt.
- Kategorie B, absichtlich auf allen Boards:
  - `esp_timer` in µs: `restart_device` kommt 2 s später, `start_streaming` 150 ms später.
  - I2C-Backport: Der I2C-Treiber ist eine gepatchte IDF-Kopie. Ihr Versionsschutz bricht jeden Build mit einem anderen IDF als 5.4.2 ab.
  - Host-Tool: Es verbindet sich ohne Board-Reset.
  - Die Sensor-Prüfung unter dem Mutex (Abschnitt 5) ändert kein Image, weil die beiden Setter keinen Aufrufer haben.
- Den Prüfstand der Builds nennt Abschnitt 6.

**9. Der Lüfterpfad ist unverändert: erreicht.**
- Unter `components/FanManager` und `main/` gibt es keinen Diff.
- Die Leitungsprobe nutzt dieselbe ADC1-Einheit wie die Lüftererkennung, aber nacheinander (Lüfter zuerst) und auf anderen Kanälen.
- Ein `static_assert` verhindert, dass der Lüfter-Pin auf CE oder RESET liegt.
- XCLK kommt auf dem S3 aus dem LCD_CAM-Block, nicht aus LEDC.

---

## 2. Befunde der Abnahme

**A. Der optionale ESP-Neustart konnte endlos laufen.**
- Mit `CONFIG_CAMERA_RECOVERY_ESP_RESTART=y` startete der ESP nach der Sperre neu. Der nächste Boot meldete den RTC-Marker nur.
- Eine Kamera, die sauber initialisiert, aber nie Frames liefert, hätte zusammen mit einem Host, der den Stream nach jedem Neustart wieder öffnet, grob einmal pro Minute einen Neustart erzeugt.
- Der Pfad war in keinem Build aktiv.
- Behoben, siehe Abschnitt 4. Die Option bleibt standardmäßig aus.

**B. Die Zahl „10 in 10 Minuten" war nicht begründbar.**
- Sie ersetzte in Stand 2 das „10 pro Boot", abgeleitet aus einem Prüfablauf war sie nicht.
- Erledigt: Das Ratenfenster ist weg.

**C. Der Rail-Check belegt „Schiene nahe 0 V", nicht „keine Rückspeisung".**
- Abschnitt 5 der Analyse behauptete, ein offener 260-µA-Pfad über RESETB würde als „nicht zusammengebrochen" auffallen.
- Die Schiene fällt aber in wenigen Millisekunden unter 1 V, wird also aktiv entladen oder belastet. Gegen so einen Pfad heben einige hundert µA sie vermutlich nicht über die Schwelle.
- Dass nichts zurückspeist, stützt sich auf Reihenfolge und Parken im Code, belegt nur durch Review.
- Korrigiert in Abschnitt 5 der Analyse, direkt an der falschen Stelle, und unter der Park-Tabelle in Abschnitt 4.
- Ein Test-Build, der RESET absichtlich nicht hält, würde es messbar machen. Er ist nicht gemacht und hat keine Eile.

**Entscheidungen des Nutzers aus der Abnahme:**
- Der Rail-Check bleibt (Abschnitt 3.4).
- Gebaut und verglichen wird vor dem Merge mit allen Boardkonfigurationen.
- Alles rund um IDF-Version und vendorten I2C-Treiber wird im nächsten Schritt über das Upstream-Repo gelöst. Bis dahin bleibt es, wie es ist.

---

## 3. Erklärung

### 3.1 Was ist Schalten, was ist Folge?

Der Diff täuscht über die Größe. Er hatte zur Abnahme 8.830 Zeilen:
- 3.878 davon sind die IDF-Kopie des I2C-Treibers, der eigentliche Fix hat 17 Zeilen,
- 1.267 sind Doku,
- rund 830 sind Host-Werkzeuge.

Die Firmware des Features hat rund 2.800 Zeilen. Ohne Kommentare, Leerzeilen und einzelne Klammern bleiben rund 1.700. Aufgeteilt nach Zweck (Zählung zum Stand `cdfbd91`; die Zuordnung ist eine Einschätzung, die Zahlen sind gezählt):

- **Schalten** (CE und RESET treiben, Wartezeiten, Pins parken): ~100 Zeilen, 6 %
- **Folgen des Anhaltens** (Gate, hängende UVC-Frames, Kamera-Task, Sensorzeiger, Framegröße, Erstframe-Prüfung, Endzustand nach Fehlschlag): ~315, 19 %
- **Nachweis** (Rail-Check ~225, Leitungsprobe ~80): ~305, 18 %
- **Politik** (Auslöser, Budget, Zähler, Boot-Wiederholung, ESP-Neustart, Testhaken): ~245, 15 %
- **Melden** (Status, Namen, JSON, Kommandos): ~465, 27 %
- **Isolation** (Kconfig, CMake, `static_assert`, `#if`-Verdrahtung): ~260, 15 %

Das An und Aus selbst ist also ein Sechzehntel. Die Folgen des Anhaltens sind dreimal so groß, der Rest dient dem Beweisen, Entscheiden, Melden und Abschotten. Die Änderungen nach der Abnahme bringen netto rund 80 Codezeilen dazu (154 neu, 72 entfernt), fast alle in Politik und Melden.

Jede Folge hat einen Grund im Treiber:
- Er gibt Frames zero-copy aus seinen Puffern heraus. Ein Konsument kann bis zu 8 s in `esp_camera_fb_get()` warten, und `esp_camera_deinit()` gibt Puffer und Queue trotzdem frei. Daraus folgt das Gate.
- Die Sensorstruktur verschwindet mit dem Deinit. Daraus folgen der Zeiger unter dem Mutex und die zwischengespeicherte PID.
- Der Init braucht rund 3,4 KB Stack, dem Serial-Task blieben 440 Byte. Daraus folgt der Kamera-Task.
- `esp_camera_init()` meldet auch dann `ESP_OK`, wenn Registerschreibvorgänge scheitern. Daraus folgt die Erstframe-Prüfung.
- Windows beendet einen Stream beim Schließen nicht, es holt nur keine Daten mehr ab. Daraus folgt das Zurückholen hängender Frames.

### 3.2 Ein Power-Cycle, vom Kommando bis zum ersten neuen Frame

Du schickst `recover_camera {"level": "power_cycle"}` über CDC, während der Host streamt. Der CDC-Task fährt den Zyklus nicht selbst. Er legt einen Auftrag in die Queue des Kamera-Tasks und wartet ohne Timeout auf ein Semaphor. Einen eigenen Task braucht es aus zwei Gründen. Der Treiber-Init braucht mehr Stack, als ein Kommando-Task übrig hat. Und der automatische Auslöser kommt aus dem UVC-Task, der während des Abbaus nicht im Treiber stecken darf. So laufen alle Neustarts nacheinander über einen Weg. Einen Timeout gibt es nicht, weil der Kamera-Task den Report auf den Stack des Wartenden schreibt; nach einem Timeout wäre dieser Stack nicht mehr gültig. Begrenzt ist dafür jeder einzelne Schritt.

Zuerst prüft der Kamera-Task das Budget. Für ein Kommando gilt nur der Cooldown von 5 s. Danach prüft er, ob das Board die gewünschte Stufe hat. Beides kommt vor allem anderen, damit ein abgelehnter Auftrag nichts anfasst.

Dann schließt er das Gate. UVC bekommt kein neues Frame mehr, sein nächster Aufruf wartet bis zu 3 s am Gate. Das ist länger als ein ganzer Zyklus. UVC erlebt einen normalen Neustart also nicht als Fehler, das nächste Frame kommt nur später. Die Sequenz wartet, bis kein Frame mehr draußen ist und niemand im Treiber hängt, höchstens 9 s. Auf dem S3 kann ein Konsument bis zu 8 s in `esp_camera_fb_get()` stecken. Normalerweise ist nach einer Übertragung Schluss, nach etwa 30 ms. Ist ein Frame länger als 500 ms draußen, holt der Host es offenbar nicht mehr ab. Dann gibt UVC es zurück, oder das Gate übernimmt es. Das passiert hier, weil der Treiber jetzt zum letzten Mal steht.

Nun läuft `esp_camera_deinit()` unter dem Sensor-Mutex, und der Sensorzeiger wird null. Direkt danach parkt `parkPins()` die Leitungen. Das muss vor dem Abschalten passieren, denn die Treiber hinterlassen ihre Pins so, dass sie die tote Schiene speisen würden. Der I2C-Treiber lässt seine Pull-ups an, und XCLK bleibt auf dem S3 an den Kamera-Takt geroutet. Zurücknehmen muss man das Parken nicht, der nächste Init stellt jeden dieser Pins neu ein.

Dann geht RESET auf low, und es folgen 2 ms Wartezeit. RESET kommt vor CE, weil das Datenblatt RESETB low vor dem Abschalten verlangt. Außerdem würde sonst der 10-k-Pull-up des Reset-Knotens rund 260 µA in die abgeschaltete Kamera drücken. Die 2 ms reichen dem Knoten mehrfach (τ ≈ 90 µs) und stellen sicher, dass die Kamera bei der folgenden Messung im Reset ist.

Jetzt kommt die Positivkontrolle: ADC2 misst D0 und D6 mit kurz eingeschaltetem Pull-up, vier Messungen je Pad. Sie gehört genau hierher. Die Kamera ist noch versorgt, aber im Reset, ihre Ausgänge sind hochohmig. Der Pin zeigt also, wie hoch die Schiene auf diesem Board in diesem Zyklus steht, und am Ende der Aus-Zeit wird relativ dazu gemessen. Vor dem Reset könnte die Kamera die Pads selbst treiben, nach dem Abschalten fällt die Schiene schon.

Jetzt geht CE auf low, beide LDOs sind aus. Nach mindestens 500 ms werden die Endwerte gemessen. Ist die Schiene noch nicht unten, wird alle 100 ms erneut gemessen, bis höchstens 3 s. Laut Messung reichen 20 bis 60 ms, die 500 ms sind bewusst großzügig.

CE wird losgelassen, RESET bleibt unten, und die Sequenz wartet 20 ms. Das Datenblatt verlangt einen Anstieg unter 5 ms (gemessen 0,4 ms) plus t2 und t3 vor RESETB high; die 20 ms sind etwa das Doppelte. Dann wird RESET losgelassen, gefolgt von 25 ms, weil SCCB frühestens 20 ms nach RESETB high kommen darf. Diese Wartezeit steht hier und nicht in `setupCamera()`, weil `setupCamera()` auch der Boot-Pfad ist. Beim Boot gibt es keinen RESET-Puls, auf den man warten müsste.

Dann holt `gpio_config()` die beiden Messpads zurück ins Digitale. Der ADC hat sie in den RTC-Mux gelegt, und die Pin-Einrichtung des Kameratreibers holt sie dort nicht heraus. Ohne diesen Schritt wären D0 und D6 für die Kamera tot. Deshalb steht er direkt vor dem Init.

`setupCamera()` ist derselbe Pfad wie beim Boot:
- XCLK auf 23 MHz,
- `esp_camera_init()`,
- Umschaltung auf 20 MHz mit 100 ms Wartezeit, bis die PLL wieder eingerastet ist,
- Sensorprofil.

Es ist derselbe Pfad, weil die OV3660 nur über diese Umschaltung zuverlässig Frames liefert. Danach setzt die Sequenz die Framegröße, die der Host ausgehandelt hat. Auf FFVR wirkt das heute nicht, weil Profil und Host beide 320×320 nutzen.

Zuletzt holt der Kamera-Task selbst ein Frame, am geschlossenen Gate vorbei. Kommt es innerhalb von 2 s und ist die PID gleich, öffnet das Gate. Der wartende UVC-Aufruf bekommt sein Frame, und der CDC-Task schickt die Antwort. Entscheidend ist dieses Frame, nicht der Rückgabewert des Inits, denn der kann `ESP_OK` sein, obwohl nichts kommt. Ab jetzt beobachtet die Politik, ob die Erholung hält (Abschnitt 4). Scheitert etwas, bleibt das Gate zu und der Treiber unten. CE und RESET sind dann losgelassen, die Kamera ist versorgt.

Von den gemessenen rund 1,2 s entfallen 0,5 s auf die Aus-Zeit und knapp 50 ms auf die festen Wartezeiten. Der Rest ist fast ganz `setupCamera()`. Das ist aus den Konstanten gerechnet, nicht einzeln gemessen. Beim automatischen Auslöser unterscheidet sich nur der Anfang: Niemand wartet auf das Ergebnis, und das Budget wertet zusätzlich die laufende Beobachtung und die Sperre aus.

### 3.3 Wo die Größe steckt

**Melden (~465 Zeilen).** Dazu gehören `get_camera_status` mit Zählern, Rail-Verdikten, den letzten vier Recoveries, Gate-Zustand, Heap und Reset-Grund, außerdem die Reports der Kommandos. Ohne das wäre die Recovery eine Blackbox. Eine ESD-Prüfung hätte als Ergebnis nur Logzeilen, und die sind auf FFVR per Default aus. Niemand wüsste, ob ein Power-Cycle die Schiene wirklich unten hatte oder nur der Reset geholfen hat. Vieles davon ist mechanisch und ließe sich straffen, etwa die Gate-Diagnose, die mit dem Fix für Befund 5 (17.7) kam.

**Folgen des Anhaltens (~315).** Ohne diesen Teil gäbe es vier Probleme:
- Das Gerät stürzt ab, sobald eine Recovery auf einen laufenden Stream trifft. UVC läse aus freigegebenem Speicher oder wartete auf eine gelöschte Queue.
- Nach dem Schließen der Kamera-App unter Windows hinge jede Recovery.
- Der Init auf dem Serial-Task würde dessen Stack überlaufen lassen.
- Ohne Erstframe-Prüfung hätten 6 von 60 Zyklen mit der beschädigten Kamera Erfolg gemeldet, obwohl keine Frames kamen.

**Nachweis (~305).** Ohne Leitungsprobe bräuchte man entweder ein Image pro Revision. Oder Rev.4/4.5 würde einen Power-Cycle melden, der nur unbeschaltete Pads bewegt. Ohne Rail-Check hieße `power_cycle ok` nur „CE wurde bewegt, und die Kamera lief danach". Ein Board, bei dem CE die LDOs nie erreicht, sähe dann aus wie ein gutes.

Knapp dahinter liegen Isolation (~260) als Preis für Ziel 8 und Politik (~245) für die Ziele 3 und 7.

### 3.4 Was später raus kann

Sicher raus kommt die I2C-Kopie beim Wechsel auf IDF 5.5 oder neuer; ihr Versionsschutz erzwingt die Entscheidung dann. Das ist der nächste Schritt des Nutzers, über das Upstream-Repo.

**Der Rail-Check bleibt.** Er ersetzt nicht das Multimeter. Das Multimeter hat einmal gezeigt, dass das Prinzip auf diesem Board funktioniert. Der Rail-Check zeigt dagegen in jedem Zyklus, dass CE auf *diesem* Board wirkt. Ohne ihn melden ein gutes Board und eines mit einem Fehler im CE-Pfad dasselbe, weil die Kamera schon vom Reset-Puls allein wiederkommt. Ein solcher Fehler kann eine kalte Lötstelle sein, ein defekter LDO-Enable oder eine künftige Board-Variante. Das wäre dasselbe Vortäuschen, das Ziel 6 ausschließt, nur auf Rev.5.

Mögliche Schritte, falls sich das später lohnt:
1. **Spur und adaptive Verlängerung entfernen**, zusammen knapp ein Drittel des Rail-Codes. Beide haben ihren Zweck erfüllt, Schwellen und Aus-Zeit sind festgelegt. Freigegeben, aber vorerst nicht gemacht, weil es kurz vor der Prüfung Bench-Werkzeug und Doku mitändern würde.
2. **Den Check selbst entfernen**, erst wenn zwei Dinge belegt sind:
   - Jeder Power-Cycle in AP6 hat `collapsed` gemeldet (Zähler `rail.collapsed` gleich der Zahl der Power-Cycle-Versuche).
   - Eine Fertigungsprüfung deckt den CE-Pfad jedes Boards ab, etwa mit einem Bench-Zyklus.

Drin bleiben müssen Leitungsprobe, Gate, Erstframe-Prüfung, Kamera-Task und Budget; jedes davon trägt eines der Ziele.

### 3.5 Die zwei Fallen

**Falle 1: das Parken von XCLK und SCCB.** Ein falscher Pin-Zustand fällt hier niemandem auf:
- Auf dem S3 schaltet `esp_camera_deinit()` XCLK nicht ab, das Makro dafür ist dort leer. Das Pad bleibt an den Kamera-Takt geroutet.
- Losgelassen wird es nur, weil `gpio_config()` in IDF 5.4.2 über `gpio_output_disable()` auch die Quelle der Ausgangsfreigabe umstellt. Das Enable-Bit allein zu löschen reicht laut IDF-Kommentar nicht („so that output disable could take effect").
- Bei SCCB lässt der I2C-Treiber seine Pull-ups an.

Kein Test fängt einen Fehler hier: XVCLK hat keinen Pfad in die Schiene, und die geschätzt 130 µA über die SCCB-Pull-ups heben die Schiene vermutlich nicht über die Schwelle. Die Kamera kommt so oder so wieder, und der Rail-Check meldet `collapsed`. Wer `parkPins()` für überflüssig hält, baut die Rückspeisung unbemerkt wieder ein. Seit der Abnahme steht das als Kommentar an `parkPins()` und in Abschnitt 4 und 5 der Analyse.

**Falle 2: Zugriffe an Gate und Mutex vorbei.** Den Sensorzeiger darf man nur *unter* dem Mutex prüfen, weil eine Recovery ihn jederzeit auf null setzt.
- Vor der Abnahme prüften `setVFlip()` und `setHFlip()` ihn *vor* dem Mutex, und der Kommentar in `takeDriverDown()` behauptete das Gegenteil.
- Die Setter haben keinen Aufrufer, aber ein Kommando für die Spiegelung würde naheliegenderweise genau sie nehmen.
- Behoben: Die Prüfung liegt jetzt im Mutex, an beiden Settern steht der Grund, und der Kommentar stimmt.
- Dieselbe Regel gilt für jeden neuen Frame-Konsumenten: nur über `cameraAcquireFrame()`, nie `esp_camera_fb_get()` direkt. Deshalb setzt die Recovery „kein WLAN" voraus, denn der StreamServer ruft den Treiber direkt auf.

Gut abgesichert sind dagegen die RTC-Mux-Falle und die Reihenfolge RESET vor CE. Die RTC-Mux-Falle ist an drei Stellen kommentiert und würde vermutlich laut scheitern, weil der Treiber Frames ohne JPEG-Startmarke verwirft. Die Reihenfolge RESET vor CE steht mit Datenblattwerten im Code.

---

## 4. Budget nach der Abnahme

**Die Grundlage ist die ESD-Prüfung.** Nach IEC 61000-4-2 gibt es mindestens 10 Einzelentladungen je Prüfpunkt und Polarität, bei Routineprüfung im Abstand von 1 s. Die Vorerkundung arbeitet mit bis zu 20 Entladungen pro Sekunde, und es gibt mehrere Prüfpunkte. Realistisch sind 40 bis über 100 Entladungen in einigen Minuten. Bewertet wird, ob sich das Gerät nach einer Salve selbst erholt. Dass es während einer Salve teilweise dunkel ist, ist in Ordnung. Den konkreten Prüfplan bestätigt der Nutzer noch; diese Werte sind die Untergrenze.

**Regeln** (`CameraRecovery.cpp`):
- **Gehalten:** Eine Erholung gilt als gehalten, wenn nach ihrem ersten Frame 30 s lang Frames kommen. Geprüft wird das bei jedem Frame, das durch das Gate geht; solange nichts beobachtet wird, kostet das eine einzige Ladeoperation. Gehaltene Erholungen zählen gegen kein Budget. Eine Pause, in der der Host keine Frames abholt, ist kein Ausfall: Das erste Frame, das mehr als 30 s nach dem ersten kommt, macht die Erholung zur gehaltenen (im Test beobachtet, als die Streams nach einer Pause wieder geöffnet wurden).
- **Nicht gehalten:** Kommt während der Beobachtung ein `frame_timeout`, hat die Erholung nicht gehalten. Ein gescheiterter Neustart hat per Definition nicht gehalten.
- **Sperre:** Drei nicht gehaltene Neustarts in Folge sperren die automatischen Auslöser. Eine gehaltene Erholung setzt die Folge auf null.
- **Ende der Sperre:** Sie löst sich nach 5 min ohne weiteren Fehlschlag selbst; die Folge beginnt dann neu. Ein erfolgreiches `recover_camera` oder ein Reset hebt sie sofort auf.
- **Cooldown:** 5 s nach jedem Neustart, auch nach manuellen.
- Das Ratenfenster ist entfallen.

**Grenzen:**
- Eine dauerhaft kaputte Kamera erzeugt höchstens drei Neustarts pro gut 5 Minuten.
- Eine Kamera, deren Erholungen jedes Mal 30 s halten, wird unbegrenzt oft erholt. Das ist Absicht, denn jede dieser Erholungen hat gewirkt.
- Treffen zwei Salven so, dass drei Erholungen hintereinander innerhalb von 30 s wieder ausfallen, bleibt die Kamera bis zu 5 min dunkel und kommt dann selbst zurück.

**ESP-Neustart als letzte Stufe** (Option `CONFIG_CAMERA_RECOVERY_ESP_RESTART`, Default aus): Er startet den ESP, wenn die Automatik gesperrt wird, und läuft höchstens einmal, bis wieder ein Start gehalten hat. Der Marker im RTC-Speicher durchläuft diese Zustände:
- **„pending"** setzt die Firmware direkt vor dem Neustart.
- **„used"** macht der nächste Boot daraus. Damit gibt es keinen zweiten Neustart, auch über weitere Resets hinweg.
- **Gelöscht** wird der Marker, sobald ein Start 30 s lang Frames geliefert hat. Das gilt auch für den Lauf direkt nach dem Boot. Danach steht die Stufe wieder zur Verfügung.
- Ein Power-on löscht den Marker ebenfalls, weil der RTC-Speicher dann undefiniert ist.

Eine Schleife kann nur entstehen, wenn jeder Neustart die Kamera mindestens 30 s zum Laufen bringt. Dann ist jeder Neustart ein Gewinn, und sie liegen mindestens rund eine Minute auseinander.

**Im Status** (`get_camera_status`):
- `recovery.suspended` und, solange gesperrt, `resume_in_s`: die restliche Ruhezeit in Sekunden, sonst `null`. Die Sperre fällt beim nächsten automatischen Auslöser nach Ablauf, also einige Sekunden später (siehe unten).
- `unheld_in_row` und `unheld_limit` (3): „2 von 3" heißt, die nächste nicht gehaltene Erholung sperrt.
- `held`, `not_held`, `suspensions`, `resumes`,
- je Eintrag in `last` das Feld `held` (`true`, `false`, oder `null` für „noch offen oder vom nächsten Neustart überholt"),
- mit der Option zusätzlich `esp_restart_armed`.

Die Zusammenfassung per `{"persist": true}` enthält dieselben Zähler und „suspended now no" bzw. „yes, resumes in N s".

**Hardware-Test** (2026-09-29, Rev.5, ESP face):
- Test-Image vom aktuellen `main` mit `CONFIG_CAMERA_TEST_HOOKS=y` und `CONFIG_CAMERA_RECOVERY_ESP_RESTART=y`.
- Ausfälle per `camera_test_fault hold_reset`, die das Gerät als `frame_timeout` erkennt.
- Alle UVC-Streams am PC offen (OpenCV unter Windows).

1. **Kein Rückschritt im Normalfall:**
   - 5 von 5 `recover_camera` im Stream: Power-Cycle, Schiene `collapsed`, 1176 bis 1200 ms.
   - Die Streams liefen ohne Neuöffnen weiter; face stand je etwa 1 s, die anderen Streams gar nicht.
2. **Gehaltene Erholungen kosten kein Budget:**
   - 11 automatische Erholungen in 8,1 min, jede gehalten.
   - `unheld_in_row` blieb 0, es gab keine Sperre und keine Ablehnung. Das alte Ratenfenster hätte die elfte verweigert.
   - Interner Heap konstant: frei 132 895 Byte, größter Block 31 744 Byte.
3. **Sperre und Selbstauflösung:**
   - Drei Erholungen fielen jeweils innerhalb von 30 s wieder aus. Danach standen `suspended: true`, `resume_in_s: 300` und `unheld_in_row` 3 von 3. 30 s später zeigte `resume_in_s` 270.
   - Die Sperre löste sich 313 s nach dem Sperren (erster Lauf), im zweiten Lauf nach 306 s. Danach lief eine Erholung, die gehalten hat.
   - Die Abweichung von 300 s setzt sich aus drei Teilen zusammen:
     - **300 s Ruhezeit**, gezählt ab dem Fehlschlag, der gesperrt hat.
     - **Warten auf den nächsten automatischen Auslöser: 0 bis 8 s.** Solange die Kamera dunkel ist, kommt etwa alle 8 s ein `frame_timeout` (4 s Treiber-Timeout, GDMA-Reset, noch einmal 4 s). Im ersten Lauf wurden zwischen 30 s nach dem Sperren und der Auflösung 34 davon abgewiesen, einer je 8,3 s. Die Sperre fällt beim ersten Auslöser nach Ablauf.
     - **Abfrageintervall des Prüfskripts:** 10 s im ersten Lauf, 5 s im zweiten.
4. **ESP-Neustart höchstens einmal:** zweimal geprüft, erst mit einem Skript, dann mit `tools/camera_budget_check.py`.
   - Eine Sperre bei scharfem Neustart startete den ESP 12,6 s später neu. Die Verzögerung von 12 s setzt sich aus 10 s Log-Flush und 2 s zusammen. Danach meldete face `restarted_by_recovery: true` und `esp_restart_armed: false`.
   - Eine Sperre direkt nach diesem Boot startete ihn nicht neu; die Uptime lief weiter, und die Sperre löste sich nach 5 min selbst.
   - Nach 30 s Frames stand `esp_restart_armed` wieder auf `true`. Die nächste Sperre startete den ESP wieder neu (Gegenprobe).
   - Nebenbefund: Der Marker „used" übersteht auch das Flashen, weil esptool nur einen USB-Reset auslöst. Nach dem Flashen eines Test-Images stand deshalb `esp_restart_armed: false`, bis 30 s Frames kamen. Das ist gewollt, denn nur ein Power-on oder ein gehaltener Lauf löschen ihn; beim Testen sollte man es aber wissen.

**Wiederholen** mit einem Test-Image: `uv run --with opencv-python tools/camera_budget_check.py held --port COMx` (etwa 45 s je Erholung) und `... suspend --port COMx` (7 min, mit ESP-Neustart-Option 12 min). Das Werkzeug hält die Streams selbst offen und öffnet sie nach einem ESP-Neustart neu.

---

## 5. Änderungen nach der Abnahme

| Commit | Inhalt |
|---|---|
| `801c731` | Budget: Nur Neustarts, die nicht gehalten haben, zählen. Drei in Folge sperren die Automatik, die Sperre löst sich nach 5 min selbst. Ratenfenster entfernt |
| `4d64c46` | ESP-Neustart höchstens einmal, bis wieder ein Start gehalten hat (Befund A) |
| `4dde6ea` | Sensorzeiger unter dem Mutex prüfen in `setVFlip()`, `setHFlip()`, `loadConfigData()`; Kommentar in `takeDriverDown()` korrigiert (Falle 2). Kategorie B ohne Wirkung auf andere Images |
| `870ffa0` | Befund C in der Analyse korrigiert, Kommentar an `parkPins()` (Falle 1) |
| `1ea22b1` | dieses Dokument, Analyse 17.9, Übergabe; danach Merge nach `main` (`be1cea1`) |
| `1e6e966` | `get_camera_status`: `resume_in_s` (restliche Ruhezeit) und `unheld_limit`, damit eine Sperre während einer Prüfung nicht wie ein Defekt aussieht |
| `fb30735` | `tools/camera_budget_check.py`: der Hardware-Test aus Abschnitt 4 als Werkzeug |
| `61b5f2c` | FFVR-Version 1.3.2 |

Spur und adaptive Verlängerung (3.4, Schritt 1) sind nicht entfernt. Das spart knapp ein Drittel des Rail-Codes, würde aber kurz vor der Prüfung Bench-Werkzeug und Doku mitändern. Besser nach AP6.

---

## 6. Prüfstand vor dem Merge

Geprüft auf Stand `870ffa0`; der anschließende Doku-Commit ändert keinen Code. Werkzeuge: ESP-IDF v5.4.2, `xtensa-esp-elf-gcc` 14.2.0.

**Alle 12 Boardkonfigurationen bauen:**
- die neun S3-Konfigurationen: `facefocusvr_eye_L`, `facefocusvr_eye_R`, `facefocusvr_face`, `project_babble`, `wrooms3`, `wrooms3QIO`, `wrover`, `esp_eye`, `seed_studio_xiao_esp32s3`;
- die drei klassischen ESP32: `esp32AIThinker`, `esp32Cam`, `esp32M5Stack`. Sie wurden damit zum ersten Mal gebaut, in einem eigenen Worktree ohne `usb_device_uvc` (siehe Übergabe, Abschnitt 5).

**Varianten des Features (auf eye_L) bauen ebenfalls:**
- ESP-Neustart plus Testhaken,
- nur Recovery ohne Leitungen,
- nur Leitungen ohne Recovery.

**Warnungen:** Aus dem Zweig kommt keine neue.
- In geänderten Dateien warnt der Compiler nur an Stellen, die es schon vorher gab (`CameraManager.cpp:176`, `OpenIrisTasks.cpp:11`).
- Die Variante ohne Leitungen warnt über drei unbenutzte Hilfsfunktionen in `CameraCycle.cpp`. Das ist so seit AP3, und kein Board nutzt diese Variante.
- Auf den klassischen ESP32 kommen 303 Kconfig-Hinweise dazu: Symbole für S3 und UVC aus `base_defaults`, die es dort nicht gibt.

**Bitvergleich gegen Baseline 2 (`5e510c7`):** `project_babble` und `wrooms3` sind identisch. Die `app.bin` ist bis auf ELF-Hash und Prüfsummen gleich; Bootloader, Partitionstabelle und `sdkconfig.h` sind ganz gleich. Für diese beiden Konfigurationen ist damit auch belegt, dass die Kategorie-B-Änderung an den Settern kein Image ändert.

**Hardware:** Budget und ESP-Neustart am 2026-09-29 geprüft, Ergebnisse in Abschnitt 4.

**Nach dem Merge, Release 1.3.2 (`61b5f2c`):**
- Bitvergleich gegen Baseline 2 erneut: `project_babble` und `wrooms3` identisch.
- Release-Bins `FFVR Eye L [1.3.2].bin`, `FFVR Eye R [1.3.2].bin`, `FFVR Face [1.3.2].bin` (`merge-bin -f raw`). Bootloader, Partitionstabelle und App sind byte-gleich zum Build; weder Testhaken noch ESP-Neustart sind im Image.
- Auf alle drei ESPs der Rev.5-Platine geflasht (vorher `erase_flash`).
- Danach alle drei Streams 28 bis 30 fps ohne Lücke; je zwei `recover_camera` im Stream ok (Power-Cycle, `collapsed`, 1175 bis 1193 ms).
