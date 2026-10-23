# Prüfung der lokalen DDKs

Stand: 22. Oktober 2026. Originalarchive unverändert; Entpacken und Probeausgaben
unter `/tmp/bcm57xx-ddk-review/`.

## Archive und Inhalt

| Archiv unter `/tmp` | Befund |
| --- | --- |
| `Microsoft Windows 95 DDK.7z` | ISO mit NDIS-3-MAC-Beispielen, Headern und Bibliotheken |
| `Microsoft Windows NT 3.51 DDK (3.51.1057.1).7z` | ISO mit NT-NDIS-Headern, Miniports und einem untersuchten Full-MAC-Beispiel |
| `Microsoft Windows 3.0 DDK (6-1-1990) (5.25-1.2mb).7z` | Zehn FAT-Diskettenimages; kein NDIS-3-Beispiel in den Dateiverzeichnissen |
| `Microsoft Windows For Workgroups 3.1 SDK.7z` | Zwei FAT-Diskettenimages mit Anwendungs-SDK 3.10; keine NDIS-MAC-Entwicklungsdateien |

Zusätzlich vorhanden: `Microsoft Windows 3.0 SDK (1990) (5.25-1.2mb).7z`.
Dieses SDK wurde nicht weiter untersucht.

SHA-256 der untersuchten Originalarchive:

```text
e205d797b961392f29e8985931bb8229d5479a04c34e70cef9b4f30af4a4e136  Microsoft Windows 95 DDK.7z
1903cc7408cc97546deda33ac1626d2c8a4fce60d9da39b59406159c4e123f65  Microsoft Windows 3.0 DDK (6-1-1990) (5.25-1.2mb).7z
ef6160d9dd7f461aaf7a7abf70cf15f57af4ab73c4388517709dfe5bed73d401  Microsoft Windows For Workgroups 3.1 SDK.7z
b83bec15d1c428a6ba36ffdb22812b9a28dc3c01e0dc13611f7485a9f7a0f4b8  Microsoft Windows NT 3.51 DDK (3.51.1057.1).7z
```

## Nutzbare Windows-95-DDK-Referenzen

Die ISO wurde nach `/tmp/bcm57xx-ddk-review/win95-files/` entpackt.
Alle folgenden Pfade sind relativ zu diesem Verzeichnis.

| Pfad | Zweck |
| --- | --- |
| `NET/NDIS3/MAC/NE2000/` | Vollständiger Ethernet-MAC: Registrierung, Anfragen, Senden, Empfang, Reset |
| `NET/NDIS3/MAC/NE3200/` | Weitere MAC-Referenz für eine Busmaster-Karte |
| `NET/NDIS3/MAC/SONIC/` | Weitere Ethernet-MAC-Referenz |
| `NET/NDIS3/MAC/TESTPROT/` | Testprotokoll einschließlich Quellen und Beschreibung |
| `NET/NDIS3/SRC/NDISDEV.ASM` | VxD-Start, DDB, Prüfung von `NdisGetVersion`, Aufruf von `DriverEntry` |
| `NET/NDIS3/SRC/NDIS.MK` | Gemeinsamer Microsoft-Build für NDIS 3.1 |
| `NET/INC/NDIS.INC` | Assemblerdefinitionen einschließlich NDIS-Diensttabelle |
| `INC32/NDIS.H` | C-Schnittstelle für MACs und Protokolle |
| `NET/NDIS3/LIB/{RETAIL,DEBUG}/LIBNDIS.{LIB,CLB}` | Vorübersetzte Bibliotheken für unterschiedliche Objektformate |

`NE2000.C` registriert einen vollständigen MAC über `NdisInitializeWrapper` und
`NdisRegisterMac`. Die Callback-Tabelle umfasst Open/Close, Send, TransferData,
Reset, Request, Statistikabfragen, Add/RemoveAdapter und Unload. Das ist die
relevante Referenz für unsere Betriebssystemanbindung; die NE2000-Hardwarelogik
ist kein Broadcom-Treiber.

## Grenzen für eine WfW-Portierung

- `NDISDEV.ASM` besitzt einen Pfad ohne `CHICAGO`, der beim statischen
  `Device_Init` startet. Die dynamische Initialisierung ist durch `CHICAGO`
  geschützt. Dieser Quellpfad ist ein Anhaltspunkt, kein Kompatibilitätsnachweis.
- `NDIS.MK` definiert `CHICAGO` und `NDIS_STDCALL`, erzeugt `.VXD` und verwendet
  standardmäßig Microsofts COFF-Werkzeuge. Es ist kein fertiger WfW-Build.
- `INC32/NDIS.H` definiert `NDIS_STDCALL` selbst, falls das Symbol fehlt. Einfach
  `-DNDIS_STDCALL=0` hilft nicht: Der Header prüft mit `#ifdef`.
- Die mit Open Watcom `wlib` untersuchte Retail-`LIBNDIS.LIB` exportiert unter
  anderem `_NdisInitializeWrapper@16` und `_NdisRegisterMac@24`. Diese Bibliothek
  darf nicht ungeprüft als WfW-cdecl-Bibliothek eingesetzt werden.
- Die Header ordnen Initialisierungscode teilweise auslagerbaren Segmenten zu.
  Segmentregeln und DDB-Version müssen für das tatsächliche WfW-Ziel geprüft werden.
- Ein direkter JWasm-Probeaufruf für `NDISDEV.ASM`, ohne `CHICAGO` und ohne
  `NDIS_STDCALL`, scheitert in `INC32/VMM.INC` an `MakeCodeSeg`-Makros. Auch mit
  der originalen Option `-Zm` scheitert der Aufruf. Es entstand kein verwendbares
  Objekt und kein Treiber. Ausgabe: `probe/assemble.log` im Arbeitsverzeichnis.

## Windows-3.0-DDK

Alle zehn Diskettenimages konnten mit `7z l` gelesen werden. Vorhanden sind
unter anderem `NET/MSNET.ASM`, `LDOSNET/DOSNET.ASM`, `VNETBIOS/VNETBIOS.ASM` und
`INCLUDE/VMM.INC`. Diese Dateien betreffen andere Netzwerkschnittstellen bzw.
Virtualisierung; es wurde keine NDIS-3-MAC-Vorlage gefunden. Die Images wurden
nur inventarisiert, nicht vollständig inhaltlich analysiert.

## Windows-for-Workgroups-3.1-SDK

Das zusätzlich heruntergeladene Archiv wurde nach `wfw/` entpackt, beide
Diskettenimages anschließend nach `wfw-files/Disk01/` und `wfw-files/Disk02/`
unter dem Prüfverzeichnis. `Disk01/INSTALL.INF` bezeichnet das Paket ausdrücklich
als Windows for Workgroups Software Development Kit Version 3.10.

Enthalten sind Anwendungs-Header und Bibliotheken für NetAPI, WNet, NetBIOS,
NetDDE und Mail sowie Beispiele unter `SAMPLES/NETAPI`, `SAMPLES/NETDDE` und
`MAILEXT`. Debug-Dateien umfassen unter anderem `WDEB386.EX_`, `VREDIR.38_` und
`VSERVER.38_`. Diese Version-3.1-Dateien sind kein Ersatz für bestätigte
WfW-3.11-Debugkomponenten.

Die vollständige Dateiliste enthält weder `NDIS.H`/`NDIS.INC` noch `LIBNDIS`
oder MAC-Treiberquellen. Die komprimierten Nutzdateien wurden nicht ausgeführt
oder installiert. Das Paket schließt die fehlende WfW-NDIS-3-DDK-Referenz somit
nicht. Dateiinventar: `wfw-file-inventory.txt` unter dem Prüfverzeichnis.

## Windows-NT-3.51-DDK

Das neu hinzugekommene Archiv ist NT **3.51**, Build **3.51.1057.1**.
Die ISO liegt unter `nt351/`; der Netzwerk-Quellbaum und die freie i386-NDIS-
Bibliothek wurden nach `nt351-files/` unter dem Prüfverzeichnis entpackt.
Das gesamte ISO-Dateiinventar steht in `nt351-iso-list.txt`.

Relevante Pfade innerhalb der ISO:

- `DDK/SRC/NETWORK/INC/NDIS.H`: Schnittstellendefinitionen für Full MAC und
  Miniports. Der Header berücksichtigt NT-Typen und enthält einen
  `BINARY_COMPATIBLE`-Pfad für x86-Miniports zur Chicago-Kompatibilität.
- `DDK/SRC/NETWORK/NE2000/NE2000.C`: registriert über
  `NdisMInitializeWrapper` und `NdisMRegisterMiniport` einen **Miniport**.
  Dies ist somit ein anderes Treibermodell als das NE2000-Full-MAC-Beispiel
  im untersuchten Win95-DDK, obwohl beide NE2000 heißen.
- `DDK/SRC/NETWORK/NE3200/NE3200.C` und `SONIC/SONIC.C`: ebenfalls
  Miniportregistrierung. Die jeweiligen Header deklarieren Version 3.0.
- `DDK/SRC/NETWORK/IBMTOK/IBMTOK.C`: enthält weiterhin Full-MAC-Registrierung
  mit `NdisRegisterMac`; ein zusätzlicher Vergleich für die Callback-Schnittstelle.
- `DDK/LIB/I386/FREE/NDIS.LIB`: NT-Linkbibliothek, kein Nachweis für eine
  unter WfW verwendbare VxD-Bibliothek.
- `DDK/SRC/NETWORK/NT31BIN/README.TXT`: beschreibt Miniportbetrieb unter NT 3.1
  mit einem aktualisierten `NDIS.SYS` sowie unter Chicago. Die ursprüngliche
  NT-3.1-Version von `NDIS.SYS` unterstützt laut dieser Datei keine Miniports.
  Eine WfW-Kompatibilität wird dort nicht beschrieben.

Das Paket ist eine hilfreiche Vergleichsquelle für NDIS-Strukturen und
Treiberlogik. Die untersuchten Beispiele liefern jedoch keinen direkten
WfW-3.11-Build oder Ersatz für dessen Aufrufkonventionen und VxD-Startcode.
Es wurde kein NT-Treiber ausgeführt, installiert oder übersetzt.

## Windows-NT-3.1-DDK (Oktober 1994)

Archiv `~/Iso/Windows 3x/Microsoft Windows NT 3.1 Win32 SDK and DDK (Oct 1994)
(MSDN) (ISO).7z`, SHA-256 `033c6aaa6f187c2ceb45f3d2db3357fca2b5a33bc52b821be285c7397f398b44`; ISO
und Netzwerkquellen entpackt nach `/tmp/bcm57xx-ddk-review/nt31/`.

Anders als das NT-3.51-DDK enthält es **NDIS-3.0-Full-MAC-Treiber** derselben
Generation wie WfW 3.11:

- `DDK/SRC/NETWORK/LANCE/`: Busmaster-Ethernet mit Deskriptorringen,
  `NdisRegisterMac`, `LANCE_NDIS_MAJOR_VERSION 3`, `MINOR 0`; `LOOPBACK.C`
  zeigt die von NDIS 3.0 verlangte Rückgabe eigener Frames an Protokolle,
  `TRANSFER.C` das TransferData-Modell zu `NdisIndicateReceive`.
- `DDK/SRC/NETWORK/SONIC/`: weiterer Busmaster-Ethernet-MAC, ebenfalls 3.0.
- `DDK/SRC/NETWORK/INC/NDIS.H`: `NDIS_MAC_CHARACTERISTICS` mit genau den
  Feldern unserer 54-Byte-Tabelle; `EFILTER.H` die Ethernet-Filterbibliothek.
- `DDK/HLP/NETWORKS.HLP`: Referenz der NDIS-3.0-Schnittstelle.

Das ist die passende Vorlage für Sende-/Empfangspfad, Anfragen und Filter.
Die NT-Aufrufkonventionen (stdcall, NT-Typen) gelten für WfW nicht; die
Dienste werden weiter über unsere cdecl-Thunks angesprochen. Kein Quellcode
wurde in dieses Repository kopiert.

## WfW-eigene Befunde

Da kein WfW-NDIS-DDK vorliegt, wurden Layouts, Argumentreihenfolgen und das
Interrupt-, Timer- und Speicherverhalten aus `NDIS.386`, den MACs der
WfW-Disketten und einem PCI-Busmaster-MAC von 1998 bestimmt:
[`docs/wfw-ndis-abi.md`](wfw-ndis-abi.md).
