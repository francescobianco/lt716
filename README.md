# lt716

Controllo di uno smartwatch/braccialetto **LT716** (protocollo FitPro, chip Telink) da Linux via
Bluetooth LE, senza l'app ufficiale.

## Dispositivo

| Campo | Valore |
|---|---|
| Nome BLE | `716` |
| MAC | `FF:FF:CF:10:70:E9` (public) |
| Firmware | in origine `V15381`, ora `V10712` (vedi [Flash](#flash-del-firmware)) |
| Stringa funzioni | `001000010000011` (`2A28`, un carattere per flag) |
| Chip | Telink **TLSR8232**, flash 512 KB (MID `0x134051`); PnP ID `248A:8266` è il default dell'SDK |
| Display | TFT 0,96" rettangolare (non il 128×128 quadrato della variante documentata da e-lab) |
| App ufficiale | FitPro (`cn.xiaofengkj.fitpro`) 2.10.10 |

Oltre alla UART espone un HID "Consumer Control" (report ID 2, un codice a 16 bit da 0 a 0x28C):
Linux lo vede come `/dev/input/eventN` col nome `716`.

## Uso rapido

```shell
./lt716 info                    # nome, firmware, funzioni, batteria
./lt716 battery                 # batteria (0x2A19)
./lt716 settime                 # imposta l'orologio all'ora locale
./lt716 find                    # vibrazione "trova braccialetto"
./lt716 alerts on               # abilita le notifiche sulla band (necessario la prima volta!)
./lt716 notify ciao             # notifica WhatsApp (max 300 byte UTF-8)
./lt716 notify -t telegram "Mario: ciao"   # altri tipi: sms qq wechat facebook twitter skype ...
./lt716 call "Mario Rossi"      # chiamata in arrivo, chiusa dopo 10 s
./lt716 call -s 30 3331234567   # chiamata che squilla 30 s
./lt716 alarms                  # programma le sveglie da alarms.txt (max 8 orari)
./lt716 alarms --show           # sveglie memorizzate sulla band
./lt716 camera on               # modalità scatto remoto (gli scatti si vedono con "live")
./lt716 goal 10000              # obiettivo passi
./lt716 profile age=40 height=178 weight=75 gender=male unit=km
./lt716 lift on 07-23           # alza il polso per accendere lo schermo, fascia oraria
./lt716 dnd on 23-07            # non disturbare
./lt716 lang it                 # lingua del display
./lt716 settings                # legge e decodifica le impostazioni della band
./lt716 steps                   # totali di oggi: passi, distanza, calorie
./lt716 measure hr              # misura battito (anche bp, spo2) e stampa i risultati
./lt716 live                    # eventi in tempo reale per 60 s (0 = fino a Ctrl+C)
./lt716 reset --yes             # ripristino impostazioni di fabbrica
./lt716 raw cd00051a01020000    # pacchetto grezzo sulla UART, stampa le risposte

# firmware (vedi "Flash del firmware")
./lt716 ota <immagine.bin>              # verifica l'immagine e costruisce i pacchetti (dry run)
./lt716 ota <immagine.bin> --yes        # OTA Telink vera
./lt716 ota --enter-updater             # entra nell'aggiornatore del produttore (dopo ~40 s)
./lt716 dumper info|dump|assets <src>|updater|reboot   # parla col nostro firmware dumper
```

Variabili d'ambiente: `MAC`, `RETRIES` (default 5), `HCI` (default `hci0`).

Lo script è in Python e parla con BlueZ via D-Bus (dipendenze: `python3-dbus`, `python3-gi`):
si connette da solo se serve (e cerca il dispositivo se BlueZ non lo conosce), scrive a pezzi da
20 byte come l'app, aspetta l'ACK della band per ogni comando e conferma ogni pacchetto ricevuto.

**Le notifiche non comparivano** finché gli interruttori per app (`1A 04`) erano tutti a zero:
la band ignora `notify`/`call` per le app disattivate. `./lt716 alerts on` li accende tutti.

## Verificato sulla band

`info`, `battery`, `settime`, `find`, `alerts`, `notify`, `call`, `alarms` (scrittura, rilettura,
cancellazione), `camera`, `goal`, `profile`, `lift`, `dnd`, `lang`, `settings`, `steps`, `raw`.
Con il firmware `V15381`; `info`, `settings`, `find`, `alerts`, `settime` anche col `V10712`.
`measure hr` ha ricevuto l'ACK ma nessun risultato in 40 s (band non indossata). `live` e la decodifica dello storico
(passi, sonno, battito) seguono l'APK ma non sono ancora stati visti arrivare.

## Protocollo

Riferimento completo, ricavato dall'APK: [`files/protocol-notes.md`](files/protocol-notes.md).
In breve:

Servizio Nordic UART `6e400001-b5a3-f393-e0a9-e50e24dcca9d`: scrittura su `6e400002`, notifiche
su `6e400003`. Batteria sul servizio standard `180F`/`2A19`.

```
CD <len u16 BE = totale-3> <gruppo> 01 <cmd> <len dati u16 BE> <dati>     comando / dati
DC 00 05 <gruppo> <cmd> 00 xx <01 = ok>                                   ACK della band
DC 00 05 <gruppo> 01 <len totale ricevuto u16 BE> 01                      ACK dell'app
```

| Gruppo | Uso |
|---|---|
| `12` | impostazioni e notifiche (app → band) |
| `13` / `14` | bind / unbind |
| `15` | dati sport: tempo reale, riepilogo, storico passi/sonno/battito |
| `18` | lettura sveglie |
| `1A` | lettura impostazioni per chiave |
| `1C` | eventi dalla band: trova telefono, scatto, musica, risposta/rifiuto chiamata |
| `1D` | ripristino di fabbrica |

### Comandi principali (gruppo `12`)

| Comando | Cmd | Dati |
|---|---|---|
| Ora | `01` | u32 BE: `sec \| min<<6 \| ora<<12 \| giorno<<17 \| mese<<22 \| (anno-2000)<<26` |
| Sveglie | `02` | 5 byte ciascuna (vedi sotto), lista completa, vuota = cancella |
| Obiettivo passi | `03` | u32 BE (la band lo rilegge come `00` + u24 LE) |
| Profilo | `04` | u32 BE: `maschio<<31 \| età<<24 \| altezza<<15 \| peso<<5 \| unità` (1 km, 2 mi) |
| Interruttori notifiche | `07` | un byte per app, tanti quanti ne riporta `1A 04` (11 qui) |
| Alza per vedere | `09` | `<on> <inizio min u16> <fine min u16>` |
| Trova braccialetto | `0B` | `01` |
| Fotocamera | `0C` | `01` / `00` |
| Chiamata | `11` | `<stato> 00 <nome UTF-8>`: 1 squilla, 2 risposta, 0 fine |
| Notifica | `12` | `<tipo> 00 00 <testo UTF-8>`, max 300 byte |
| Non disturbare | `14` | come `09` |
| Lingua | `15` | codice (it = 18, en = 1, …: tabella nello script) |
| Misura | `24` | `<tipo> <on>`: 0 battito, 1 pressione, 2 SpO2 |

Tipi notifica: `01` SMS, `02` QQ, `03` WeChat, `04` Facebook, `05` Twitter, `06` Skype, `07` Line,
`08` WhatsApp, `09` KakaoTalk, `0A` Snapchat, `0B` TikTok, `10` Instagram, `11` LinkedIn,
`12` Telegram, `13` OK.ru, `14` VK, `15` TenChat, `16` Viber, `17` Messenger, `18` Zalo.

### Sveglie

40 bit BE per sveglia: `(anno-2000)<<34 | mese<<30 | giorno<<25 | ora<<20 | min<<14 | 1<<11 | giorni`,
giorni: bit 0 = lunedì … bit 6 = domenica. Esempio 07:30 lun–ven:

```
CD 00 0A 12 01 02 00 05 6A CC 77 88 1F
```

`alarms.txt` ha lo stesso formato di `../id115`, ma qui gli orari distinti sono al massimo 8.

## Chip e OTA

Il chip è un **Telink TLSR8232** (non un 8266: `248A:8266` è il PnP ID di default dell'SDK).
Lo conferma il progetto [FitPro-LT715-TLSR8232](https://github.com/e-labInnovations/FitPro-LT715-TLSR8232),
che ha documentato l'hardware dell'LT715/LT716 e pubblicato un dump del firmware `LT716(G) V10712`.
Il dump coincide col nostro 716 in report HID, PnP ID, stringa funzioni e nome corto `716`.

### Mappa della flash (512 KB)

| Indirizzo | Contenuto |
|---|---|
| `0x00000` | firmware principale (banco 0) |
| `0x20000` | **aggiornatore del produttore** (36 KB): solo GAP, GATT, PnP e OTA, schermo spento |
| `0x29000`–`0x6ffff` | risorse: font latino e CJK, stringhe, grafica 1 bpp (layout diverso per versione) |
| `0x7a000` | flag del produttore (`0x7affc` = "modalità OTA") |
| `0x7d000`–`0x7e000` | dati utente |
| `0x7f000` | calibrazione del quarzo |

### Protocollo OTA (dall'SDK Telink nell'APK, `com.telink.ota`)

Caratteristica `00010203-0405-0607-0809-0a0b0c0d2b12`, scritture senza risposta:

```
00 FF                                   prepara
01 FF                                   avvia
<indice u16 LE> <16 byte, pad FF> <CRC16 LE>   dati, CRC-16/MODBUS sui primi 18 byte
  ... ogni 8 pacchetti una lettura della caratteristica (controllo di flusso)
02 FF <ultimo indice u16 LE> <~indice u16 LE> <CRC16 LE>   fine
```

### Regole dell'immagine (da `otaWrite` in `liblt_5316.a`)

- applicazione Telink `KNLT`: magic a +8, lunghezza a +0x18, ultimi 4 byte = `~crc32` del resto;
- **lunghezza ≡ 4 (mod 16)**: il binario va riempito di `FF` fino a un multiplo di 16 prima di
  aggiungere il CRC, altrimenti l'OTA fallisce dopo 8 pacchetti;
- byte 6–7 = `5D 02` (`FLAG_FW_CHECK`, algoritmo 2): la band verifica il CRC32 prima di attivare
  l'immagine;
- al primo errore (CRC di un pacchetto, pacchetto perso) la band cancella quanto scritto e si
  riavvia: il client vede l'errore ATT `0x0e` alla lettura successiva.

`firmware/telink_fw.py` produce immagini conformi; `./lt716 ota` rifiuta quelle che non lo sono.

### Come aggiorna il firmware del produttore

1. Il firmware principale riceve `00 FF`/`01 FF` (callback a `0x3a08` nel `V10712`): mostra la
   schermata "OTA", **azzera i 4 byte del proprio magic a `0x8`**, mette `0x7affc` = 1 e aspetta
   i dati per 40 s.
2. Se arrivano dati, li scrive in `0x20000` (sopra l'aggiornatore). Se non arriva nulla, al
   timeout non cancella niente e si riavvia: la ROM trova `0x0` non valido e avvia
   l'**aggiornatore** a `0x20000`.
3. L'aggiornatore riceve il nuovo firmware, lo scrive in `0x0`, verifica il CRC e lo avvia;
   lui resta intatto in `0x20000`.

La via sicura è quindi: `./lt716 ota --enter-updater` (passo 2 senza dati), poi
`./lt716 ota <immagine> --yes` verso l'aggiornatore. L'aggiornatore resta come rete di sicurezza.

## Flash del firmware

Cosa è stato fatto su questa band il 2026-11-06:

1. Un primo tentativo di OTA del dumper verso il firmware originale si è fermato dopo 8 pacchetti
   (immagine senza padding a 16 byte); il firmware ha azzerato il proprio magic ed è ripartito
   l'aggiornatore. Scambiato per il firmware in modalità OTA, l'aggiornatore ha ricevuto il
   `V10712` di e-lab: **il codice del `V15381` originale è stato sovrascritto ed è perso**.
2. Il `V10712` gira ma lo schermo era bianco: le risorse in `0x29000`+ erano quelle del
   `V15381`, con un layout diverso (solo 0–20 % dei byte uguali).
3. Col padding corretto il **dumper** (`firmware/dumper/`) è stato installato tramite
   l'aggiornatore: ha letto due volte l'intera flash con risultato identico (backup in
   `files/firmware/`, fuori da git).
4. Il dumper v2 ha scritto le risorse del `V10712` nella zona `0x29000`–`0x6ffff`, con rilettura
   di ogni pagina e un dump finale: zona identica alla sorgente, 0 byte cambiati fuori.
5. Tramite l'aggiornatore è stato rimesso il `V10712`. Funziona tutto (BLE, notifiche, sensori),
   ma è il firmware della variante con display quadrato 128×128: sul nostro 0,96" rettangolare si
   vede solo una porzione dell'immagine.

### Il dumper

Firmware nostro (SDK 8232 di e-lab, base `examples/ble_adv`), installato e tolto tramite
l'aggiornatore. Si annuncia come `LT716-DUMP` (`A4:C1:38:71:6D:D0`) e vibra 300 ms all'avvio;
schermo spento.

| Comando (`./lt716 dumper ...`) | Effetto |
|---|---|
| `info` | versione, registro di avvio `0x63e`, ID flash, flag dei due banchi |
| `dump [file]` | legge 512 KB in ~35 s, ripete i blocchi mancanti |
| `assets <src.bin> [cur.bin]` | scrive `0x29000`–`0x6ffff` da `src` (solo settori diversi), verifica e rilegge tutto |
| `updater` | azzera il proprio magic a `0x8` e riavvia nell'aggiornatore |
| `reboot` | riavvio |

Il firmware rifiuta da sé ogni scrittura fuori dalla zona risorse.

```shell
firmware/build.sh dumper        # SDK e-lab (commit cebf23e) in Docker -> _build/lt716_dumper.ota.bin
```

### Display (trovato con `firmware/disptest`)

Il pannello 0,96" è un ST7735S 80×160 sui pin documentati da e-lab (MOSI PC3, CLK PC5, DC PC1,
RST PA6, CS PA1, retroilluminazione PB3 attiva alta; tasto touch PC2 attivo alto):

| Parametro | Valore |
|---|---|
| finestra | 80×160 verticale, offset colonna 26, riga 1 |
| MADCTL | `0xC8` (MY \| MX \| BGR): tasto in basso, come il firmware originale |
| inversione | INVON |

`firmware/disptest` disegna cornice, barre RGB, una freccia verso l'alto e l'indicatore del tasto;
i parametri si cambiano al volo con `./lt716 dumper lcd <col> <row> <w> <h> <madctl> <inv>`.
Tasto tenuto 10 s = ritorno all'aggiornatore senza Bluetooth.

### Server FitPro

L'app chiede il firmware a
`https://tomato.gulaike.com/api/v1/config/app?name=716&type=1&version=V15381&platform=1`, con
un token fisso nell'APK e header `country: foreign`. Per `716` (anche con versioni più vecchie,
`LT716`, `LH716`) risponde `{"success":true,"data":null}`: nessun firmware pubblicato.

## MorseOS

Firmware nostro per l'LT716 (`firmware/morseos/`, versione 0.3): il tasto touch si usa in Morse.

- **Quadrante**: ora grande `00:00` a 7 segmenti, sotto `SAB 7`, in alto `BT` (verde se connesso)
  e batteria, in fondo il segno del tocco (giallo mentre premi). Lo schermo si spegne dopo 15 s;
  il tocco che lo riaccende mostra solo il quadrante.
- **Accesso**: la parola Morse **TAN** (`- .- -.`) apre il menu. Il decodificatore impara il ritmo
  di chi lo usa (medie separate di punto e linea), chiude la lettera dopo 600 ms di pausa e
  riconosce TAN appena è completa; linea oltre 2,5 s = annulla.
- **Menu a riquadri**: due box per schermata, quello in alto è selezionato. Tocco veloce: il box
  di sotto sale e appare il successivo (a giro). Tocco lungo (> 600 ms, breve vibrazione):
  conferma. Voci: INFO (versione, batteria, BT, accensione, tempi Morse), TORNA.
- **Da PC** (indirizzo `A4:C1:38:71:6D:D0`): `MAC=A4:C1:38:71:6D:D0 ./lt716 settime | notify | find`
  (protocollo FitPro su UART: ora, trova braccialetto, notifiche e chiamate mostrate a schermo),
  `./lt716 dumper touchlog` per vedere ogni tocco, lettera e parola.
- **Sicurezza**: come il dumper risponde a `./lt716 dumper updater`; in più tasto tenuto 10 s =
  ritorno all'aggiornatore senza Bluetooth.

Nota: la build installata è la 0.3 con un byte sbagliato nell'UUID del servizio UART
(`…dcca9e`); il sorgente è già corretto (`…dcca9d`), quindi i comandi `lt716` sulla UART
funzionano dalla prossima build.

```shell
firmware/build.sh morseos       # -> firmware/morseos/_build/morseos.ota.bin
./lt716 dumper updater          # dal firmware in esecuzione all'aggiornatore
./lt716 ota firmware/morseos/_build/morseos.ota.bin --yes
```

## File

| Percorso | Contenuto |
|---|---|
| `lt716` | script di controllo |
| `alarms.txt` | sveglie applicate da `./lt716 alarms` |
| `files/protocol-notes.md` | protocollo completo ricavato dall'APK |
| `firmware/dumper/` | sorgenti del firmware dumper |
| `firmware/disptest/` | firmware di prova del display (geometria, colori, orientamento) |
| `firmware/morseos/` | MorseOS: quadrante, input Morse, menu, FitPro su UART |
| `firmware/build.sh`, `firmware/telink_fw.py` | build in Docker e post-processing dell'immagine (padding, `5D 02`, CRC32) |
| `files/apk/` | APK FitPro e decompilato jadx (esclusi da git, vedi sotto) |
| `files/firmware/` | dump della flash e immagini (esclusi da git) |
| `firmware/vendor/` | clone dell'SDK e-lab usato dalla build (escluso da git) |

L'APK (91 MB) e il decompilato (250 MB) non sono nel repository. Per rigenerarli:

```shell
mkdir -p files/apk
curl -L -o files/apk/fitpro-2.10.10.apk \
  'http://imtt.dd.qq.com/sjy.00022/sjy.00004/16891/apk/7055EFE5113B027C48ACAE896AC63356.apk?fsname=cn.xiaofengkj.fitpro_2.10.10.apk'
md5sum files/apk/fitpro-2.10.10.apk   # 7055efe5113b027c48acae896ac63356
jadx --no-res -d files/apk/fitpro-2.10.10.apk.jadx files/apk/fitpro-2.10.10.apk
```

## Vedi anche

`../id115`: script analogo per un ID115 Plus (protocollo Yoho Sports, servizio `cc00`).
