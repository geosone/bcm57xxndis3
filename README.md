# BCM5722-Treiber für Windows for Workgroups 3.11

Ziel ist ein nativer NDIS-3.0-Netzwerktreiber im `.386`-Format für den
Broadcom NetXtreme BCM5722 (`14e4:165a`). Der Repository-Name legt keine
Chipfamilie fest. Der NDIS-Pfad ist vollständig implementiert, wurde aber
bisher nur gegen Simulationen und ohne Karte unter WfW getestet.

## Build und Tests

Der Build erzeugt `build/BCM57DBG.386`. Beim Start protokolliert er
VMM- und NDIS-Versionen und sucht den BCM5722 über
PCI-Konfigurationsmechanismus 1. Danach meldet er sich als NDIS-3.0-MAC an.
`AddAdapter` setzt die Karte zurück, richtet PHY, DMA-Ringe und den geteilten
Interrupt ein und registriert den Adapter. Es liest BDF, Revision, BARs, Subsystem und
IRQ und kann bei bereits eingeschaltetem Memory Decode lesend auf die
MMIO-Register zugreifen, ohne die Karte zu aktivieren oder DMA zu starten. Ausgabe: Emulator-Port
`0xE9`; daraus entsteht auf realer Hardware noch keine sichtbare Anzeige.

Voraussetzungen: GNU make, Python 3, Host-C-Compiler, Open Watcom 2.0 (hier
May-2026-Build unter `/opt/watcom`), JWasm (getestet: 2.19), JWlink mit
Windows-3.x-VxD-Unterstützung. Keine DDK-Bibliotheken für diesen Diagnosebuild.

```sh
make LINK386=/pfad/zu/jwlink
make check LINK386=/pfad/zu/jwlink
```

Auf diesem Rechner ist der passende Linker:
`/home/mario/mars/jwlink-fork/build/jwlinkLR/jwlink`.
`WATCOM`, `CC386`, `ASM`, `LINK386` und `HOST_CC` sind überschreibbar.
`make clean` entfernt ausschließlich `build/`.

`make check` prüft PCI-Erkennung und BARs, NDIS-Lebenszyklus und ABI, die
Bindungsschicht (Filter, Senden, Loopback, TransferData, OIDs, Reset) sowie
Hardware-Resetsequenzen, PHY, Ringe und Interrupts mit simulierten Registern
und LE-Objekte/DDB. Der Build markiert
32-Bit-Datenobjekte explizit als ausführbar, wie vom Windows-3.x-VxD-Lader
benötigt. `src/` enthält C-/Assemblerquellen, `tests/` Hosttests und den
Win16-Testhelfer, `tools/` Formatprüfung und Emulatorlauf, `docs/` Referenzbefunde.

Mit QEMU und mtools lässt sich eine vorhandene DOS/WfW-Installation prüfen:

```sh
make smoke LINK386=/pfad/zu/jwlink WFW_IMAGE=/pfad/zu/wfw.img
```

Das Rohimage muss Windows in `C:\WINDOWS` und eine FAT-Partition bei Byte 32256
enthalten; für einen anderen Offset `tools/smoke_wfw.py --partition-offset`
verwenden. Der Test ändert nur eine frische Kopie unter `/tmp/bcm57xx-wfw-*`.
Er erwartet keine Broadcom-Karte, startet eine Win16-Anwendung und beendet
Windows sauber. Mit `NDIS_IMAGE`, `VNETSUP_IMAGE` und `IFSHLP_IMAGE` können
zusätzlich passende, expandierte WfW-Netzwerkdateien in die Testkopie eingebunden
werden. Eine übergebene NDIS-Datei muss eine erfolgreiche Versionsabfrage liefern;
eine lediglich kopierte Datei gilt nicht als erfolgreich geladen.

Für den vollständigen NDIS-Test werden lokale WfW-Installationsmedien und
zusätzlich die libmspack-Entwicklungsdateien benötigt:

```sh
make network-fixture WFW_MEDIA=/pfad/zu/WfW NDIS_CPU_FIX=1
make smoke LINK386=/pfad/zu/jwlink WFW_IMAGE=/pfad/zu/wfw.img NETWORK_DIR=build/wfw-network
```

Dieser Lauf verwendet Microsofts NE2000 und NetBEUI als eingerichtete
Referenzumgebung. Unser eigener `AddAdapter`-Callback muss erreicht werden
und mangels BCM5722 in QEMU ablehnen. Der CPU-Fix
aus patcher9x betrifft nur die neu expandierte NDIS-Testdatei; die MIT-Lizenz
liegt unter `LICENSES/`. Die Originalmedien werden nicht verändert.

## Installation unter WfW 3.11

`make disk` legt `BCM57DBG.386` und `OEMSETUP.INF` (Vorlage:
[`inf/OEMSETUP.INF`](inf/OEMSETUP.INF)) in `build/disk/` ab. Diese Dateien
auf eine Diskette oder in ein Verzeichnis kopieren, dann in WfW „Netzwerk“
→ „Netzwerk-Setup“ → „Treiber“ → „Adapter hinzufügen“ → „Nicht aufgeführter
oder aktualisierter Netzwerkadapter“ wählen und den Pfad angeben.

Die INF folgt dem Aufbau von `NETWORK.INF` und der OEMSETUP.INF des
SMC1211TX-Treibers. In `PROTOCOL.INI` werden folgende Einträge erwartet:

```ini
[BCM5722]
DriverName=B57$

[BCM57DBG]
Adapters=BCM5722
```

`NDIS.386` liest beim Start `[BCM57DBG] Adapters=` und übergibt `BCM5722`
an `AddAdapter`; unter diesem Namen binden die Protokolle. Der Smoke-Test
prüft genau diese Zuordnung. Den Eintrag `DriverName` ignoriert der
NDIS3-Treiber. `B57$` ist der Name von Broadcoms NDIS2-Treiber `B57.DOS`;
mit `ndis2=7:b57.dos` in `[bcm5722]` und der Datei auf der Diskette lässt er
sich als Real-Mode-Treiber ergänzen. Ob WfW-Setup die Abschnitte genau so
schreibt, ist ohne Zielrechner noch nicht geprüft.

## Bestätigte Ausgangslage

Der Nutzer bestätigt, dass der vorhandene Windows-98/ME-Treiber mit seiner
angepassten INF auf der Zielkarte funktioniert. Die lose Datei liegt unter
`/home/mario/Iso/Driver/BMC5722/b57w9x.inf`.

Der Vergleich mit `Broadcom_57_xx_Win_me_98-10.24d.zip` ergibt:

- Die PCI-ID `14e4:165a` wird zusätzlich auf `BCM5750A1.Inst` abgebildet.
- Ein weiterer Eintrag nennt `SUBSYS_02A61028`.
- Der Anzeigename `BCM5722` wird ergänzt.
- Alle übrigen Installationsabschnitte bleiben unverändert.

Die INF-Dateien in diesem ZIP und in
`Broadcom_NetXtreme_10.24d_Win98SE-ME.7z` sind bytegleich.
Der isolierte Unterschied steht in
[`docs/bcm5722-win9x-inf.patch`](docs/bcm5722-win9x-inf.patch).
Die Subsystem-ID ist ein Eintrag des Patches; sie wurde nicht unabhängig an der
Zielhardware ausgelesen.

Das ist ein praktischer Hinweis auf nutzbare Hardwareunterstützung im vorhandenen
Win9x-Treiber. Daraus folgt keine Binärkompatibilität mit WfW: Die INF lädt
`b57w2k.sys` über `*ndis` und trägt NDIS-Version 4.0 ein. Die ebenfalls vorhandenen
Zeichenfolgen `ndis3` in den Bindungsabschnitten belegen keine WfW-Unterstützung.

## Vorhandene Referenzen

- `Broadcom_NetXtreme_NDIS2-16.2.0_DOS.7z` enthält `B57.dos`, `oemsetup.inf`,
  `PROTOCOL.INI` und `readme.txt`, aber keinen Quellcode. Die OEM-Datei deklariert
  den Treiber als `REAL`. Seine Funktion auf der Zielkarte ist noch nicht bestätigt.
- Linux [`tg3.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.c)
  und `tg3.h` liefern Hardwareinitialisierung, Register und Deskriptoren.
  Linux-spezifische Speicher-, DMA- und Interruptdienste müssen ersetzt werden.
- [`Windows-driver-samples`](https://github.com/microsoft/Windows-driver-samples)
  enthält aktuelle WDK-Beispiele für Windows 11 und ist keine direkte
  WfW-3.11-Treibervorlage.
- Die historische [Microsoft-DDK-Dokumentation zu WfW und Windows 95](https://techshelps.github.io/MSDN/WIN95DDK/HTML/S2D16.HTM)
  erläutert Unterschiede zwischen NDIS 3.0, NDIS 3.1 und NT-Miniports.
- Lokal ist Open Watcom unter `/opt/watcom` vorhanden. Das Nachbarprojekt
  `/home/mario/mars/vsbvxd/vxd` enthält einen Linux-Build für einen Windows-3.x-VxD
  mit Watcom und JWasm/JWlink; es ist eine mögliche Buildreferenz, keine
  NDIS-Implementierung. Bei Codeübernahme sind die jeweiligen Lizenzen zu beachten.

## Lokale DDKs

Die heruntergeladenen Windows-95- und Windows-3.0-DDKs sowie das
WfW-3.1-SDK und das NT-3.51-DDK wurden untersucht. Das WfW-3.1-SDK
enthält Anwendungsbeispiele, aber keine NDIS-MAC-Entwicklungsdateien.
Das Windows-95-DDK enthält vollständige NDIS-3-MAC-Beispiele, aber auch
Win95-spezifische Aufrufkonventionen und Bibliotheken. Pfade, Prüfsummen und
Ergebnisse des Assembler-Probeaufrufs stehen in
[`docs/ddk-review.md`](docs/ddk-review.md).

## Entwicklungsschritte

1. Die WfW-NDIS-3.0-DDK-Schnittstelle und ein damaliges MAC-Treiberbeispiel
   beschaffen und prüfen: Registrierung, Aufrufkonventionen, Strukturen und Dienste.
2. Einen minimalen `.386`-Treiber bauen und das Laden mit Diagnoseausgabe unter
   WfW 3.11 nachweisen. Interruptcode und seine Daten müssen im Speicher bleiben.
3. Die Zielkarte erkennen, PCI-Ressourcen auswerten, MMIO abbilden und MAC/PHY
   initialisieren. Nur die bestätigte Chipvariante zunächst freigeben.
4. DMA-Puffer und RX/TX-Ringe implementieren; danach Interruptbehandlung und
   NDIS-Anfragen, Paketfilter und Sende-/Empfangspfade anschließen.
5. Auf dem Zielrechner Linkaufbau, ARP, Ping, Dateitransfer und wiederholtes
   Starten/Beenden von Windows prüfen. Offloads und Jumbo Frames zunächst auslassen.

## Aktueller Prüfstand

Schritte 1–4 sind umgesetzt. Die WfW-NDIS-Schnittstelle wurde aus den
WfW-Originaldateien und einem PCI-Busmaster-MAC von 1998 rekonstruiert:
[`docs/wfw-ndis-abi.md`](docs/wfw-ndis-abi.md). Der Treiber besteht aus
folgenden Teilen:

- `src/adapter.c`: Bindungen mit eigenen Paketfiltern und Multicast-Listen,
  Sendewarteschlange, Loopback, Empfang mit TransferData, OIDs und Reset.
  Als Vorbild diente LANCE aus dem NT-3.1-DDK.
- `src/nic.c`: `AddAdapter`/`RemoveAdapter`, ISR und DPC mit
  Tagged-Statusblock, Link-Abfrage im Sekundentakt. Bei `System_Exit` werden
  Interrupts und DMA gestoppt.
- `src/bcm5722_*.c`: Reset, PHY, Ringe und Start der Hardware.

Hosttests und der WfW-Lauf ohne Karte bestehen; Details stehen in
[`docs/runtime-tests.md`](docs/runtime-tests.md).

Der Resetpfad und Deskriptorlayouts wurden aus dem vom Nutzer genannten
[`chris1111/BCM5722D`](https://github.com/chris1111/BCM5722D) nach C portiert.
Mit `[BCM57DBG] ResetTest=1` (oder `=2` mit DMA-Loopback) in der SYSTEM.INI
läuft vor der Anmeldung zusätzlich die Diagnose. Herkunft und Lizenz:
[`docs/hardware-port.md`](docs/hardware-port.md), GPL-2.0-or-later für die
adaptierten Hardwareteile, Lizenztext in `COPYING`.

Als Nächstes folgen der Test auf echter Hardware (Link, ARP, Ping,
Dateitransfer, wiederholtes Starten und Beenden von Windows) und eine
`OEMSETUP.INF` für die WfW-Netzwerkeinrichtung. Die geplante Proxmox-VM mit
durchgereichtem BCM5722 ist noch nicht verfügbar.

Die maßgebliche Hardwarebeschreibung ist
[`5722-PG101-R` vom 15.10.2007](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html).
Weitere Referenzen: [`docs/hardware-references.md`](docs/hardware-references.md).
