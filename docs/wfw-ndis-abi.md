# WfW-3.11-NDIS-ABI: Befunde aus den Binärdateien

Stand: 24.10.2026. Für WfW 3.11 gibt es kein auffindbares NDIS-3.0-DDK. Die
Schnittstelle wurde deshalb aus den WfW-Originaldateien rekonstruiert und mit
dem NT-3.1-DDK (`NDIS.H`, Beispiele LANCE und SONIC) abgeglichen. Alle
Angaben beziehen sich auf diese Binärdateien. Die Disassemblierung dient nur
der Schnittstellenklärung; es wurde kein Code übernommen.

## Untersuchte Dateien

| Datei | Herkunft | Art |
|---|---|---|
| `NDIS.386` | WfW-3.11-Diskette 8 (`NDIS.38_`, KWAJ) | NDIS-Wrapper, 81 Dienste |
| `NE2000.386` | ebenda | ISA-MAC mit Port-I/O |
| `NE3200.386` | ebenda | EISA-Busmaster-MAC |
| weitere 11 MACs | ebenda (`AM1500T`, `ELNK3`, `SMC8000W` …) | nur Dienstübersicht |
| `SMCND.386` | [SMC1211TX-Treiber, `WFW311/`](https://ftp.kmu.edu.tw/Win/driver/nic/Accton/SMC1211/WFW311/) (1998) | **PCI-Busmaster** (RTL8139-Klon) |

Die Disketten liegen unter `~/Iso/MSDOS-WfW/WfW-3.11-Disk*.img` und werden mit
`tools/expand_kwaj.c` (libmspack) entpackt. `NE2000.386` von Diskette 8 ist
bytegleich mit der Datei in `build/wfw-network/`.

## Aufrufe

- Alle Dienste sind cdecl mit Stapelargumenten; der Aufrufer räumt ab.
  Die MACs binden sie über `VxDJmp`-Stubs (`int 20h` + `dd 0028_8000h|n`).
- Die Argumentreihenfolge entspricht dem NT-3.1-`NDIS.H`. Bestätigt sind
  `NdisRegisterAdapter` (6), `NdisInitializeInterrupt` (10),
  `NdisAllocateSharedMemory` (5), `NdisSetTimer` (2) sowie
  `NdisQueryPacket`, `NdisQueryBuffer` und `NdisGetNextBuffer`. Die Ausgabezeiger
  von `NdisQueryPacket` dürfen NULL sein.
- Callbacks der MAC sind ebenfalls cdecl. NDIS führt vor jedem Aufruf `cld` aus.

## Strukturen

| Struktur | WfW-Layout | Beleg |
|---|---|---|
| `NDIS_MAC_CHARACTERISTICS` | bytegepackt, 54 Byte, Name bei 46 | NE2000 schreibt 0x36 Byte |
| `NDIS_REQUEST` | MacReserved[16], Typ @16, OID @20, Puffer @24, Länge @28, Written/Read @32, Needed @36 | Request-Handler von NE2000 |
| `NDIS_ADAPTER_INFORMATION` | NT-3.1-Layout bytegepackt: Kanal @0, Master @4, Dma32 @5, Activate @6, Typ @10, MapRegs @14, MaxMapping @18, Ports @22, Portdeskriptoren ab @26 (je 12 Byte); 38 Byte mit einem Port | NE2000, NE3200, SMCND; `NdisRegisterAdapter` in NDIS.386 |
| `NDIS_INTERRUPT` | 24 Byte: Flags (Wort), Kontext, ISR, DPC, VPICD-Handle, nächster | `NdisInitializeInterrupt` in NDIS.386 |
| `NDIS_TIMER` | 12 Byte: Handle, Funktion, Kontext | NE2000; wie Win95 ohne `M7` |
| `NDIS_PHYSICAL_ADDRESS` | 32 Bit | SMCND legt die Adressen im 4-Byte-Abstand ab |

## Verhalten von NDIS.386

**Anmeldung.** `NdisRegisterMac` ruft `AddAdapter` synchron auf; schlägt
er fehl, scheitert die Anmeldung mit `0xc0000001`. NE2000 übergibt als
MacMacContext die Adresse seiner MacHandle-Variablen und liest sie in
`AddAdapter`. NDIS hat sie zu diesem Zeitpunkt also schon gefüllt.

**Zeichenketten.** In WfW zählt `NDIS_STRING.Length` die abschließende Null
mit: NE2000 meldet „Ne2000“ mit Length=MaximumLength=7, SMCND ebenso.
`NdisOpenConfiguration` kopiert genau `Length` Byte als Abschnittsnamen. Ohne
mitgezählte Null fehlt das Ende des Namens, und die Abschnittssuche scheitert.

**`PROTOCOL.INI`.** NDIS liest `protocol.ini` bei seinem eigenen
`Device_Init` über VMM `OpenFile` (bei Misserfolg im Verzeichnis von
`WIN386.EXE`) und hält Abschnitte und Schlüssel in VMM-Listen. Die Suche
wandelt Abschnitts- und Schlüsselnamen in Großbuchstaben um. `NdisRegisterMac`
öffnet den Abschnitt mit dem MAC-Namen und liest `Adapters` als Liste. Für
jeden Eintrag ruft es `AddAdapter` mit diesem Namen auf und zählt die Erfolge.
Fehlt der Schlüssel, folgt ein einziger Aufruf mit dem MAC-Namen.

**`NdisRegisterAdapter`.** MacHandle und MacAdapterContext müssen ungleich
NULL sein, der Adaptername darf nicht leer sein. Die Adapterinformation darf
fehlen oder 0 Ports haben. Für jeden Port wird `*PortOffset = InitialPort`
gesetzt (identische Abbildung).

**Interrupts.** `NdisInitializeInterrupt` akzeptiert die Vektoren 0–15
(IRQ 2 wird zu 9) und ignoriert Mode und Level. Mit Shared=TRUE setzt NDIS
`VPICD_OPT_CAN_SHARE` und kettet weitere NDIS-Interrupts an denselben IRQ.
Der Hardware-Handler läuft so ab:

1. Für jedes Kettenglied ruft er `Isr(Context)` auf.
2. Liefert die ISR TRUE und ist noch kein DPC angemeldet, plant er
   `Dpc(ctx, ctx, ctx, ctx)` über `Schedule_Global_Event` ein.
3. Bei einem geteilten IRQ, den niemand beansprucht hat, endet er mit gesetztem
   Carry (weiter in der VPICD-Kette). Sonst sendet er **sofort**
   `VPICD_Phys_EOI`, also vor dem DPC.

Ein pegelgesteuerter PCI-Interrupt muss daher in der ISR an der Quelle
abgeschaltet werden.

**Timer.** `NdisSetTimer(Timer, ms)` tut nichts, solange der Timer noch
ansteht. Mit ms=0 läuft der Callback über `Schedule_Global_Event`, mit ms>0
über `Set_Global_Time_Out` (Interrupt-Zeit). Vor dem Aufruf von
`Funktion(0, Kontext)` setzt NDIS das Handle auf 0.
`NdisCancelTimer` ruft `Cancel_Time_Out` auf und eignet sich daher nur für
ms>0.

**Gemeinsamer Speicher.** `NdisAllocateSharedMemory` ruft intern
`NdisAllocateMemory` mit CONTIGUOUS|NONCACHED und der Höchstadresse 16 MB auf.
Das ergibt `_PageAllocate` mit `PageUseAlign|PageContig|PageFixed|PageLocked`
(0x8e). Höchstens 128 solcher Blöcke werden verwaltet. SMCND bezieht so
seinen zusammenhängenden RX-Ring (0x4650 Byte) und seine TX-Puffer.

## Was PCI-Treiber unter WfW tun (SMCND)

- Sie melden sich als ISA (`AdapterType=1`) mit `Master=0`, ohne DMA-Kanal und
  ohne Map-Register an; der I/O-Bereich wird als Port-Deskriptor angegeben.
- Den Interrupt registrieren sie als Shared mit Mode 1.
- DMA-Speicher beziehen sie über `NdisAllocateSharedMemory`.
- Zur Wahl unter mehreren Karten lesen sie `BusNo`/`DeviceNo` aus `PROTOCOL.INI`.

## Folgerungen für BCM57DBG

- `AdapterType=1`, 0 Ports (MMIO), Shared-Interrupt mit IRQ-Leitung aus dem
  PCI-Konfigurationsraum.
- Der Ringblock kommt aus einem eigenen `_PageAllocate` mit denselben Flags,
  aber ohne 16-MB-Grenze, da der BCM5722 bis 4 GB adressiert. Außerhalb der
  Init-Phase greift der bisherige Fallback mit gewöhnlichen Seiten.
- Die ISR maskiert über die Interrupt-Mailbox und gibt TRUE nur zurück, wenn
  der Statusblock ein neues Tag hat oder INTA aktiv ist.
- Loopback und Link-Abfrage laufen über 0-ms-Timer, also als Events. Ein
  1-s-Time-out stößt nur die Link-Abfrage an.
- Der MAC-Name `BCM57DBG` wird mit Length=MaximumLength=9 angemeldet;
  `PROTOCOL.INI` erhält `[BCM57DBG] Adapters=BCM5722` (siehe `inf/OEMSETUP.INF`).
- WfW hat keine Ethernet-Filterbibliothek; Paketfilter und Multicast-Listen
  pro Binding verwaltet `src/adapter.c`.
