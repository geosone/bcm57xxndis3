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

`src/platform.c` schließt die Resetfunktion an die WfW-Plattform an: MMIO über
die `_MapPhysToLinear`-Abbildung, PCI-Konfiguration über Mechanismus 1 mit
echten 16-Bit-Zugriffen (kein Lesen-Ändern-Schreiben des Dwords, das RW1C-
Statusbits nebenan löschen würde) und Wartezeiten über den PIT-Kanal 0. Dessen
Reloadwert gehört Windows; bei einem Überlauf zählen nur die sicher vergangenen
Takte, eine Wartezeit wird dadurch nie kürzer als verlangt.

Der Diagnose-VxD ruft den Reset **nur** mit `ResetTest=1` im Abschnitt
`[BCM57DBG]` der SYSTEM.INI auf. Dann setzt er bei Bedarf die PCI-Memory-/
Busmaster-Bits, führt einen Reset aus und protokolliert Ergebnis,
Host-Control und MAC-Register. Die MAC-Engines bleiben aus, DMA wird nicht
programmiert. Voraussetzung bleibt ein Adapter, den kein anderer Treiber
(z. B. `B57.dos`) benutzt. Ohne den Schalter bleibt der Diagnosepfad rein lesend.

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

`src/bcm5722_phy.c` portiert aus `PHY.cpp` den MDIO-Zugriff (mit Abschalten
des Auto-Pollings), PHY-ID, PHY-Reset mit Auto-MDIX und Ethernet@Wirespeed,
die Autonegotiation (10/100/1000, symmetrische Pause) sowie die Link-Auswertung
mit Flusssteuerung, MAC-Portmodus, Duplex und TX-Längen. Nur der Gigabit-PHY
des BCM5722; die BCM5906-Pfade und die Jitter-/Trim-Korrekturen anderer Chips
entfallen. Korrigiert gegenüber der Vorlage: Die Wirespeed-Einstellung
verknüpfte Fehlercodes mit `&` und verlor Fehler; bei Halbduplex blieben alte
Flusssteuerungsbits stehen; ein Timeout beim PHY-Reset bricht ab, bevor weitere
Register beschrieben werden. `tests/test_phy.c` simuliert MI-Schnittstelle und
PHY, einschließlich hängendem Bus, festhängendem Resetbit und aller
Geschwindigkeits-/Duplexkombinationen.

Mit `ResetTest=1` folgen auf einen erfolgreichen Chipreset PHY-ID, PHY-Reset,
Autonegotiation und bis zu 5 s Linkabfrage; das Protokoll nennt Geschwindigkeit,
Duplex und Flusssteuerung. Die MAC-Engines bleiben dabei aus.

DMA (Schritt 3): `src/platform.c` beschafft feste Seiten über `_PageAllocate`
und ermittelt physische Adressen über `_CopyPageTable`. Nur der Ringblock
(44 KiB: Senden, Standard-Empfang, Rückgabe, Statusblock) muss zusammenhängend
sein; nicht zusammenhängende Versuche werden bis zu achtmal wiederholt und
danach freigegeben. Die Paketpuffer liegen zu zweit in einer Seite.
`src/bcm5722_ring.c` hält 64 Empfangs- und 32 Sendepuffer; die 512er-Ringe
des Chips verweisen zyklisch darauf (Slot s nutzt Puffer s mod n), sodass der
Standardring nie umgeschrieben werden muss. Gesendet wird per Kopie in einen
Sendepuffer, aufgefüllt auf 60 Byte. `bcm5722_start()` und `bcm5722_stop()`
in `src/bcm5722_hw.c` portieren `initializeAdapter`, `stopAdapter` und
`configureMACAddress`; korrigiert ist der Backoff-Startwert, in den die Vorlage
`mac[4]` doppelt und `mac[5]` gar nicht einrechnete.

`ResetTest=2` führt nach PHY-Setup einen DMA-Test im internen MAC-Loopback aus:
8 Frames an die eigene Adresse müssen unverändert zurückkommen. Das benötigt
weder Kabel noch Gegenstelle.

`tests/test_ring.c` lässt 2000 Frames durch einen simulierten Chip laufen
(alle Ringindizes laufen mehrfach über) und prüft Sendegrenzen,
Empfangsfreigabe und Fehlerfälle; `tests/test_start.c` prüft Ringadressen,
Statusblock, MAC-Adresse, die Reihenfolge der Engine-Freigaben sowie
Stoppsequenz und Timeouts.

`tests/test_platform.c` prüft Config-Adressen und Zugriffsbreiten sowie die
Wartezeit gegen einen simulierten PIT mit verschiedenen Reloadwerten und
Abfrageabständen.

Noch anzuschließen: Hardwareinterrupt über VPICD mit Statusblock-Tag und
IRQ-Quittierung sowie die NDIS-Pfade (Open/Close, Send, Empfangsanzeige mit
TransferData, Anfragen, Paketfilter, Loopback) nach dem Vorbild von LANCE aus
dem NT-3.1-DDK.
Die geplante Proxmox-VM mit BCM5722-Passthrough steht noch nicht zur Verfügung.

Interrupts und Filter (Schritt 4) stammen nicht aus BCM5722D, sondern aus
dem Programmierhandbuch und der Registerbedeutung in Linux `tg3`; Code
wurde nicht übernommen. Der Treiber nutzt den Tagged-Statusblock, den der
Host-Control-Wert 0x29a einschaltet:

- **ISR:** Sie beansprucht den Interrupt nur bei neuem Tag oder bei aktivem
  INTA (PCI-State 0x70, Bit 1) und maskiert dann über Mailbox 0x204 = 1.
- **DPC:** Er quittiert mit `Tag << 24`.
- **Filter:** Die Multicast-Hash-Register 0x470–0x47C nehmen jeden
  Multicast an; Promiscuous ist Bit 8 in RX_MODE.

Ringblock-Speicher: Zuerst wird `_PageAllocate` mit PageContig versucht,
wie NDIS.386 für gemeinsamen Speicher. Nur wenn das scheitert, greifen die
Wiederholungen mit gewöhnlichen Seiten.
