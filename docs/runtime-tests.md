# Diagnosebuild: Prüfstand

Stand: 22. Oktober 2026. Artefakt: `build/BCM57DBG.386`.
Es handelt sich um einen Diagnosebuild, noch ohne Paketübertragung.

## Hosttests und Dateiformat

```sh
make check LINK386=/home/mario/mars/jwlink-fork/build/jwlinkLR/jwlink
```

Bestanden: PCI-Erkennung und BAR-Auswertung, bytegepackte NDIS-Callback-Tabelle,
Anmelde-/Abmelde-Lebenszyklus mit Fehlerbehandlung sowie simulierte
Hardware-Resetsequenz und Timeout-Fälle. Die LE-Prüfung bestätigt DDB,
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
BCM57DBG: VMM version raw=0000030b
BCM57DBG: NdisGetVersion raw=00000300
BCM57DBG: AddAdapter: hardware path not enabled
BCM57DBG: NdisRegisterMac status=c0000001
BCM57DBG: BCM5722 14e4:165a not found
```

`NdisInitializeWrapper` und `NdisRegisterMac` erreichen damit die echten WfW-
NDIS-Dienste und den C-Callback. Weil `AddAdapter` noch keinen betriebsbereiten
Hardwareadapter bereitstellt, weist er die Anmeldung zurück. `c0000001` ist
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

Auf dem Entwicklungsrechner fehlt die echte BCM5722-Karte. Der lesende MMIO-
Probelauf, Reset/PHY, DMA, Hardwareinterrupts und Paketübertragung sind noch
nicht auf realer Hardware geprüft. Der Resetcode ist an die WfW-Plattform
angeschlossen, lief aber bisher nur gegen simulierte Register; ohne
`ResetTest=1` schreibt der Diagnosebuild keine NIC-Register.
