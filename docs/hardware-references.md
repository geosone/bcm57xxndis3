# Broadcom-Hardwarereferenzen

## Passende Referenz: BCM5722

Gefunden am 22. Oktober 2026:

**Broadcom BCM5722 Programmer’s Guide – Host Programmer Interface Specification
for the NetXtreme and NetLink Family of Highly Integrated Media Access
Controllers**, Dokument **5722-PG101-R**, **15. Oktober 2007**.

Die [Onlinefassung bei ManualsLib](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html)
ist eine Wiedergabe des Broadcom-Originaldokuments, kein allgemeines Datenblatt.
Die Onlinefassung umfasst 593 Viewer-Seiten. Viewer-Seiten und gedruckte
Seitennummern unterscheiden sich; daher folgende Links verwenden:

- [Geltungsbereich, Viewer-Seite 60](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html?page=60).
- [Initialisierung, ab Viewer-Seite 141](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html?page=141).
- [Interruptablauf, Viewer-Seite 242](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html?page=242).
- [PCI-/Controllerregister, ab Viewer-Seite 245](https://www.manualslib.com/manual/3455622/Broadcom-Bcm5722.html?page=245).

Ein geprüfter direkter PDF-Download liegt lokal noch nicht vor. Die probierte
Broadcom-URL `https://docs.broadcom.com/doc/5722-PG101-R` liefert keinen Download.
Die Dokumentidentität und die genannten Kapitel wurden online geprüft.

## Vom Nutzer gelieferte Vergleichsdokumente

| Lokale Datei | Dokument | Datum |
| --- | --- | --- |
| `/tmp/5718-PG107-R.pdf` | BCM5718 Family Programmer’s Guide, 5718-PG107-R | 17.07.2013 |
| `/tmp/635285684907972088.pdf` | BCM57785 Family Programmer’s Guide, 57785-PG106-R | 21.02.2014 |
| `/tmp/635079297638080909.pdf` | BCM5725/BCM5762/BCM57767 Programmer’s Guide, 5725_5762_57767-PG101-R | 27.06.2013 |

SHA-256:

```text
e375c07dffd664a63df617d899e5171afe731e9c27dd5d1eeaa39b40fc7eb94b  5718-PG107-R.pdf
29345595377f87ca5304c7fc173b0a6c1bb80457f050e42e680861e5f5932c86  635285684907972088.pdf
aaa085622a70bda4a7e7c0d83d9a13577d5f3b2bfdb4366d19a4da911d9581bb  635079297638080909.pdf
```

Die Dateien wurden mit `pdftotext -layout` unter `/tmp/bcm57xx-ddk-review/`
ausgewertet. Sie erklären verwandte Hardware; ihre Reset-, Firmware-, Ring- und
Statusblockdetails dürfen nicht ungeprüft auf BCM5722 übertragen werden.

### Einordnung des BCM5725-Handbuchs

Die neu gelieferte PDF umfasst 683 Seiten. Auf Seite 43 nennt der Abschnitt
„Purpose and Audience“ ausdrücklich BCM5725, BCM5762 und BCM57767.
Der Text enthält keine Nennung von BCM5722. Auf Seite 44 verweist die
Referenzliste für frühere Controller auf ein separates `57XX-PG1xx-R`.

Die gemeinsamen Konzepte sind als Vergleich nutzbar. Eine vollständige
Register- oder Initialisierungskompatibilität mit BCM5722 wird dadurch nicht
belegt. Ein konkretes Beispiel steht auf Seite 124: Für diese neuere Familie
wird der RCB der Receive-Producer-Ringe für Jumbo Frames erweitert.
Diese Erweiterung darf nicht pauschal auf den älteren BCM5722 übertragen werden.

Für die Implementierung verwenden wir das BCM5722-Handbuch als maßgebliche
Beschreibung, das BCM5725-Handbuch für ergänzende Erklärungen und den Abgleich
mit `tg3` bzw. `BCM5722D` für tatsächlich angewandte Abläufe.

## Weitere Implementierungsreferenz

[`adlan/BCM5722D`](https://github.com/adlan/BCM5722D) ist ein quelloffener
Mac-OS-X-Treiber. Seine README nennt ausdrücklich BCM5722, das obige
Programmierhandbuch sowie Linux `tg3` und FreeBSD `if_bge` als Grundlagen.
Damit ist er eine zusätzliche, auf diese Chipgruppe begrenzte Vergleichsquelle.
Die Betriebssystemanbindung ist für WfW neu zu erstellen. Das Projekt verwendet
GPL Version 2. Inzwischen wurde Hardwarecode aus dem vom Nutzer genannten
[Fork von chris1111](https://github.com/chris1111/BCM5722D) portiert; Herkunft,
Änderungen und Grenzen stehen in [hardware-port.md](hardware-port.md).

## Zu bestätigende Hardware

Erstes Ziel bleibt der vom Nutzer unter Win9x erprobte BCM5722 (`14e4:165a`).
Die vorhandene Win9x-INF nennt BCM5714, aber keinen BCM5718. Ob derselbe Binärtreiber
auch BCM5718 nach einem INF-Patch bedienen kann, ist damit weder bestätigt noch
ausgeschlossen und wurde nicht getestet.
