# Hardwareport aus BCM5722D

## Herkunft und Lizenz

Vorrangige Hardwarevorlage auf Wunsch des Nutzers:
[`chris1111/BCM5722D`](https://github.com/chris1111/BCM5722D),
Commit `c3e483fddbb139e73cfec38d01d405567fb35d85`.
Der lokale unveränderte Referenzklon liegt unter
`/tmp/bcm57xx-ddk-review/BCM5722D-chris1111`.
Der Fork basiert auf Adlan Razalans BCM5722D und nennt Änderungen von long1eu.

`src/bcm5722_hw.c` und `.h` sind aus `Private.cpp` und `HW.h` angepasst.
Die übernommenen Teile tragen Adlan Razalans Copyright und stehen unter
GPL-2.0-or-later; der Lizenztext liegt in `COPYING`. IOKit- und Apple-Header
werden nicht benötigt oder mitgeliefert. Der optionale NDIS-CPU-Patch für die
Testkopie stammt aus JHRobotics/patcher9x; dessen MIT-Lizenz steht separat in
`LICENSES/patcher9x-MIT.txt`.

## Bereits portiert

- TX-Deskriptor (16 Byte) und RX-Deskriptor (32 Byte), mit Größenprüfungen.
- BCM5722-Reset mit Interruptmaskierung, NVRAM-Arbitrierung, Warm-Reset-
  Mailbox, PCIe-Reset-Workaround und Bootcode-Handshake.
- Betriebssystemunabhängige Aufrufe für MMIO, PCI-Konfiguration und Wartezeiten.
- Auswertung der PCI-BARs im Diagnosepfad; keine Kürzung von Adressen über 4 GiB.
- Lesender MMIO-Probelauf mit Abgleich der ASIC-Kennung zwischen PCI und MMIO.

Die C-Resetfunktion wird mitgebaut, aber vom Diagnose-VxD **nicht aufgerufen**.
Die konkreten Schreib- und Zeitdienste der WfW-Plattform sind ihr noch nicht
angeschlossen. Voraussetzung für den späteren Aufruf sind ein exklusiv
übernommener, ruhender Adapter, gültiges MMIO und eingeschaltete PCI-Memory-/
Busmaster-Bits. Der aktuelle Diagnosepfad aktiviert diese Bits nicht selbst.

## Änderungen gegenüber der Vorlage

Der erste Hardwarepfad akzeptiert nur `14e4:165a`. Die Besonderheiten der
BCM5906-Familie und zusätzlicher im Mac-Fork genannter Geräte sind ausgeklammert.

NVRAM- und Firmware-Timeouts werden als Fehler zurückgegeben. In der Vorlage
ignoriert der Resetpfad den Rückgabewert von `lockNVRAM`; auch nach Ablauf der
Bootcode-Warteschleife meldet er Erfolg. Die Portierung beendet diese Fälle
stattdessen mit explizitem Status, bevor die abschließende TLP-Konfiguration
als erfolgreich behandelt wird. MMIO-Readbacks sorgen an den kritischen Stellen
für die Abarbeitung gepufferter Schreibzugriffe.

## Tests und nächste Hardwarearbeit

`tests/test_hw.c` prüft mit simulierten Registern die Reset-Reihenfolge,
PCI-Voraussetzungen, NVRAM-Timeout, Firmware-Timeout und Deskriptor-Layouts.
Das belegt die Softwareverträge, nicht die elektrische Funktion auf einer Karte.

Noch zu portieren bzw. anzuschließen: WfW-Schreibzugriffe und Zeitdienste,
PHY-/MDIO-Initialisierung, DMA-Allokation und -Adressübersetzung,
RX-/TX-Ringbetrieb, IRQ-Quittierung und die vollständigen NDIS-Paketpfade.
Die geplante Proxmox-VM mit BCM5722-Passthrough steht noch nicht zur Verfügung.
