# Diagnosebuild: Prüfstand

Stand: 24. Oktober 2026. Artefakt: `build/BCM57DBG.386`.
Der Build enthält den vollständigen NDIS-Pfad. Ohne Karte lehnt `AddAdapter`
ab; auf echter Hardware ist der Paketbetrieb noch nicht geprüft.

## Hosttests und Dateiformat

```sh
make check LINK386=/home/mario/mars/jwlink-fork/build/jwlinkLR/jwlink
```

Bestanden: PCI-Erkennung und BAR-Auswertung, bytegepackte NDIS-Strukturen,
Anmelde-/Abmelde-Lebenszyklus mit `AddAdapter` innerhalb der Anmeldung und
Fehlerbehandlung, simulierte Hardware-Resetsequenz, Timeout-Fälle, PHY, Ringe
und Tagged-Status-Interrupts. Dazu kommt die Bindungsschicht (`test_adapter`):

- Medienwahl, Paketfilter und Multicast-Listen pro Binding
- TransferData über Pufferketten
- Sendewarteschlange mit erhaltener Reihenfolge und Abbruch beim Schließen
- verzögertes Loopback, auch bei Sendeaufrufen aus einer Indication heraus
- OIDs und Reset
- keine NDIS-Aufrufe bei gesperrten Interrupts

Neun Mutationsproben (unter anderem falsches Tag in der ISR, Loopback-Zelle
zu früh freigegeben, Senden an der Warteschlange vorbei) schlagen alle fehl. Die LE-Prüfung bestätigt DDB,
Exportordinal, 32-Bit-Objekte, MAC-Initialisierungsreihenfolge und NDIS-Dienstaufruf.
Die Binärdatei benötigt keine externen C-Laufzeitbibliotheken.

## WfW-Grundtest ohne Netzwerk

Die Basis ist `/home/mario/mars/ahciaspi/build/win311/win311.img`, DOS/WfW in
`C:\WINDOWS`, FAT bei Byte 32256. Das Original bleibt unverändert; jeder Lauf
verwendet eine neue Kopie unter `/tmp/bcm57xx-wfw-*`.

```sh
make smoke LINK386=/home/mario/mars/jwlink-fork/build/jwlinkLR/jwlink \
  WFW_IMAGE=/home/mario/mars/ahciaspi/build/win311/win311.img
```

Ohne konfigurierte Netzwerkumgebung ist `NdisGetVersion=0` erwartbar. Der
Grundtest prüft Laden des VxD, die fehlende BCM5722-Karte, Start einer Win16-
Anwendung und die saubere Rückkehr zu DOS. VMM meldet Version `0x030b`.

## NDIS mit eingerichteter Testumgebung

Nur `NDIS.386` in die ursprünglich netzwerklose Installation zu kopieren war
nicht ausreichend. Die reproduzierbare Testumgebung richtet in der Kopie
IFSHLP, NET START, NE2000, NetBEUI und die zugehörigen INI-Einträge ein. QEMU
emuliert eine NE2000 als Referenzadapter; sein Netzwerk ist eingeschränkt.
Der BCM5722-Diagnosepfad bleibt davon getrennt und findet keine Zielkarte.

```sh
make network-fixture WFW_MEDIA=/home/mario/Iso/MSDOS-WfW/WfW NDIS_CPU_FIX=1
make smoke LINK386=/home/mario/mars/jwlink-fork/build/jwlinkLR/jwlink \
  WFW_IMAGE=/home/mario/mars/ahciaspi/build/win311/win311.img \
  NETWORK_DIR=build/wfw-network
```

Die Microsoft-Dateien werden ausschließlich aus lokalen Medien unter `build/`
expandiert. `NDIS_CPU_FIX=1` wendet den bekannten, signaturgeprüften
CPU-Kalibrierungspatch auf diese neu erzeugte Testkopie an. Ohne Patch hing der
Netzwerktest beim Start; mit Patch erreichte er unseren Callback und DOS.
Die Originalmedien bleiben unverändert.

Nachgewiesener Ablauf:

```text
BCM57DBG: init
BCM57DBG: VMM version raw=0000030b
BCM57DBG: NdisGetVersion raw=00000300
BCM57DBG: ResetTest=00000001
BCM57DBG: BCM5722 14e4:165a not found
BCM57DBG: AddAdapter name=BCM5722
BCM57DBG: AddAdapter: BCM5722 not found
BCM57DBG: NdisRegisterMac status=c0000001
```

Die Testkopie enthält in `PROTOCOL.INI` die Abschnitte aus
`inf/OEMSETUP.INF` (`[BCM5722] DriverName=B57$`, `[BCM57DBG]
Adapters=BCM5722`). `AddAdapter name=BCM5722` belegt, dass NDIS die Liste
gelesen hat; ohne sie stünde dort der MAC-Name `BCM57DBG`.

`NdisInitializeWrapper` und `NdisRegisterMac` erreichen damit die echten WfW-
NDIS-Dienste und den C-Callback. Die Diagnose läuft vor der Anmeldung, weil
`AddAdapter` die Karte in Betrieb nimmt. Ohne BCM5722 weist `AddAdapter`
die Anmeldung zurück. `c0000001` ist
hier der erwartete Fehler, **keine erfolgreiche MAC-/Protokollbindung**. Die
Wrapperressourcen werden freigegeben, die Win16-Testanwendung startet und
Windows kehrt zu DOS zurück.

## ABI-Befund

Die WfW-3.11-Implementierung erwartet eine bytegepackte MAC-Tabelle von 54 Byte;
Name liegt bei Offset 46. Dieser Befund wurde mit dem DDK-Aufbau und den
Zugriffen in der vorhandenen WfW-NDIS-Implementierung abgeglichen und durch den
real erreichten `AddAdapter`-Callback bestätigt. Die Watcom-Größenprüfung und
Host-Offsettests verhindern ein Zurückfallen auf das falsche 56-Byte-Layout.

## SYSTEM.INI-Schalter

Der Smoke-Test trägt `[BCM57DBG] ResetTest=1` ein und erwartet
`BCM57DBG: ResetTest=00000001` im Protokoll. Das belegt den VMM-Dienst
`Get_Profile_Decimal_Int`. Ohne BCM5722 erreicht der Lauf den Resetpfad nicht.

## Ausstehend

Auf dem Entwicklungsrechner fehlt die echte BCM5722-Karte. QEMU emuliert
keinen BCM57xx. Folgende Teile liefen bisher nur gegen Simulationen und sind
auf realer Hardware noch nicht geprüft:

- der MMIO-Probelauf
- Reset und PHY, DMA
- `AddAdapter` mit Hardwareinterrupts
- der Paketbetrieb

Für den Hardwaretest wird die Karte über `inf/OEMSETUP.INF` installiert.

## Installation mit WfW-Setup (24.10.2026)

In einer frischen Kopie des Testimages lief das deutsche „Netzwerk-Setup“
(`WINSETUP.EXE /Z`) per QMP-Tastatureingaben und Bildschirmfotos:
Microsoft Windows Network installieren, „Nicht aufgeführter oder
aktualisierter Netzwerkadapter“, Pfad `C:\BCMDISK` mit `make disk`.

- Setup zeigt „Broadcom NetXtreme BCM5722 Gigabit Ethernet“ an, bindet NetBEUI
  und IPX/SPX und kopiert `BCM57DBG.386` ohne Nachfrage nach
  `C:\WINDOWS\SYSTEM`. Mit `ndis3=7:` fragte es nach „WfW-Diskette 7“.
- `PROTOCOL.INI`: `[BCM5722] DriverName=B57$`, `[BCM57DBG]
  Adapters=BCM5722`, `BINDINGS=BCM5722` bei NetBEUI, NWLINK und MS$NDISHLP.
- SYSTEM.INI: `netcard=bcm57dbg.386`, `netmisc=ndis.386,ndis2sup.386`.
- `IFSHLP.SYS` fehlte in der CONFIG.SYS und wurde für den Start ergänzt,
  ebenso die CPU-gepatchte `NDIS.386` aus der Testumgebung.

Der Neustart dieser Installation lieferte:

```text
BCM57DBG: AddAdapter name=BCM5722
BCM57DBG: AddAdapter: BCM5722 not found
BCM57DBG: NdisRegisterMac status=c0000001
```

Die Kette INF → Setup → PROTOCOL.INI → `AddAdapter` ist damit geschlossen.
