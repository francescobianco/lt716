# lt716

Controllo di uno smartwatch/braccialetto **LT716** (protocollo FitPro, chip Telink) da Linux via
Bluetooth LE, senza l'app ufficiale.

## Le band

Due LT716, stesso chip, stesso nome BLE `716` e schermo diverso. Stato al 2026-11-24:

| | Prima band | Seconda band |
|---|---|---|
| MAC di fabbrica (firmware originale e aggiornatore) | `FF:FF:CF:10:70:E9` | `FF:FF:5F:12:99:71` |
| MAC con MorseOS 0.6.2+ | `A4:C1:38:10:70:E9` | `A4:C1:38:12:99:71` (atteso, vedi sotto) |
| Nome per `MAC=` / `DUMPER_MAC=` | `band1`, `band1-stock` | `band2`, `band2-stock` |
| Firmware originale | `V15381` (perso, poi `V10712` di e-lab) | `V18185` (perso) |
| Firmware ora | MorseOS 0.6.2 | l'ultimo installato è disptest v7, con l'indirizzo fisso `A4:C1:38:71:6D:D0` |
| Display | ST7735S, DC PC1, retroilluminazione PB3 | GC9107, DC PC6, retroilluminazione PA2, alimentazione PA0 |

Il MAC di fabbrica sta in flash a `0x7f100`, nel settore di calibrazione. MorseOS 0.6.2 lo legge
(senza scrivere) e usa il prefisso Telink `A4:C1:38` più i suoi ultimi 3 byte: ogni unità ha
un indirizzo suo, diverso da quello dell'aggiornatore. Sulla seconda band la posizione
`0x7f100` non è ancora stata verificata; se lì non c'è nulla, resta l'indirizzo fisso.
dumper e disptest usano ancora l'indirizzo fisso `A4:C1:38:71:6D:D0` (`fixed`), quindi con
entrambe le band accese non si sa quale risponde: tenerne accesa una sola.

| Campo | Valore |
|---|---|
| Stringa funzioni | `001000010000011` (`2A28`, un carattere per flag), uguale sulle due band |
| Chip | Telink **TLSR8232**, flash 512 KB (MID `0x134051`); PnP ID `248A:8266` è il default dell'SDK |
| Display | TFT 0,96" 80×160 rettangolare (non il 128×128 quadrato della variante documentata da e-lab) |
| App ufficiale | FitPro (`cn.xiaofengkj.fitpro`) 2.10.10 |

Oltre alla UART espone un HID "Consumer Control" (report ID 2, un codice a 16 bit da 0 a 0x28C):
Linux lo vede come `/dev/input/eventN` col nome `716`.

### Seconda band (2026-11-23)

È arrivato un secondo LT716 col firmware di fabbrica `V18185`, poi sovrascritto (vedi sotto).

| Campo | Valore |
|---|---|
| Nome BLE | `716`, uguale alla prima band: si distinguono solo dal MAC |
| MAC | `FF:FF:5F:12:99:71` |
| Manufacturer data | chiave `0x344C`, valore = il MAC (`ff ff 5f 12 99 71`) |
| Appearance | `0x0180` (Generic Watch) |
| Servizi | Telink OTA `00010203-0405-0607-0809-0a0b0c0d1912`, UART `6e400001-b5a3-f393-e0a9-e50e24dcca9d` |
| Firmware | **`V18185`**, versione che non abbiamo da nessuna parte |
| VID:PID | `248a:8266` |
| Stringa funzioni | `001000010000011`, uguale alla prima band |
| Impostazioni | tutte di fabbrica (profilo 30 anni/170 cm/70 kg, obiettivo 10000, notifiche spente) |

Cosa è successo nei primi tentativi, con dongle e band vicini:

- Gli annunci arrivano di rado e a fatica, a circa −96 dBm (la prima band, nella stessa stanza, si vede
  ogni ~10 s allo stesso livello): la ricezione del dongle sembra il limite.
- Un `bluetoothctl pair` si è collegato e ha risolto i servizi (vedi sopra), ma il pairing
  non è stato completato; i tentativi successivi sono rimasti fermi su "Attempting to pair".
- `MAC=FF:FF:5F:12:99:71 ./lt716 info` (che non richiede il pairing) è scaduto in fase di
  scansione/connessione. **Sulla band non è stato scritto nulla.**

Più tardi, con il controller interno del portatile (`hci0`) e una scansione solo LE
(`menu scan` → `transport le`): la band si vede a **−58 dBm**, ma manda pochi annunci
(3 in 60 s; una scansione normale di 25 s non l'ha trovata). Senza pairing `info` e
`settings` hanno risposto subito (valori nella tabella sopra): il firmware gira e risponde
come deve. Non è un controllo byte per byte della flash, che via BLE si potrebbe fare solo
col dumper, cioè con un OTA che sovrascriverebbe il `V18185`.

Il `V18185` non è sul server FitPro (vedi [Server FitPro](#server-fitpro)); per salvarlo
restavano SWire (aprendo la band) o un dumper scritto in `0x20000` al posto dell'aggiornatore.
Allora si credeva che la prima band non funzionasse più, quindi non c'era un'unità su cui
provare prima. In realtà era nell'aggiornatore (vedi [Prima band: ritrovata](#prima-band-ritrovata-2026-11-24)).

**2026-11-23: si è scelto di rinunciare al `V18185`.** `./lt716 ota --enter-updater` ha
avviato l'aggiornatore (annunci molto più frequenti, ~3 al secondo; manca `2A28`), poi
`firmware/battmon` (8612 B) è stato scritto in `0x0` in 6,9 s. Subito dopo la band non annuncia
più, come ci si aspetta da battmon, che non usa la radio. **Il `V18185` è sovrascritto e perso.**
L'aggiornatore della seconda band in `0x20000` non è stato toccato.

battmon ha girato 10 minuti (poi è tornato da solo all'aggiornatore) ma lo schermo è rimasto nero:
il display di questa band è diverso. Dump completo (due copie identiche) in
`files/firmware/band2_flash_{1,2}.bin`: il banco 0 è cancellato per intero, restano le risorse
del `V18185` (diverse da quelle della prima band) e l'aggiornatore (diverso da quello della
prima band, 0x8d74 byte).

#### Hardware della seconda band (trovato con `firmware/disptest` v4–v6)

| Funzione | Pin | Prima band |
|---|---|---|
| Tasto touch | PC2, attivo alto | uguale |
| Motore | PA5, attivo alto | uguale |
| Alimentazione del display | **PA0**, alto = acceso | non c'è |
| DC del display | **PC6** | PC1 |
| MOSI / CLK / CS / RST | PC3 / PC5 / PA1 / PA6 | uguale |
| Controller | **GC9107**, RDDID `00 91 07` | ST7735S |
| Geometria | 80×160, colonna 24, riga 0, MADCTL `0x00` (tasto in basso, ordine RGB), senza INVON | colonna 26, riga 1, MADCTL `0xC8`, INVON |
| Retroilluminazione | **PA2**, alto = acceso (flottante = debole) | PB3 |
| LED verde del sensore | **PC7**, attivo basso (MorseOS 0.8.11, `/health/pin`, 2026-11-26) | PC7 è il clock I2C dell'HRS3300 |
| Sensore dei passi | **PA4**: interruttore di vibrazione, chiuso a massa a riposo (dal firmware originale, 2026-11-26) | PA4 con interrupt sui due fronti |
| Sensore del battito | **nessuno**: solo il LED su PC7. Il firmware originale, se l'HRS3300 non risponde, inventa il battito (70–84) e fa lampeggiare il LED (vedi IDEE.md) | HRS3300 su PA4/PC7, percorso vero |
| Alimentazione del touch (?) | uno di PB4–PB7, PC0, PC4, PC7: portati tutti bassi, PC2 è rimasto alto 10 s e disptest è uscito da solo all'aggiornatore | |

Senza PA0 alto il pannello non risponde: l'ID letto è `00 00 00`. Con DC sul pin sbagliato
il pannello prende anche i pixel come comandi: si accende (SLPOUT/DISPON passano), mostra il
vecchio contenuto della sua RAM (la schermata "OTA" del `V18185`, specchiata) e si scurisce man
mano, perché i colori finiscono nei registri di tensione.

Come ci si è arrivati:
- i firmware di fabbrica (`V17899`, `V08928`) non hanno un init fisso. Leggono l'ID del controller
  (RDDID `0x04`, SPI software su PC3/PC5, un clock a vuoto, 24 bit) e scelgono fra ST7735S,
  GC9106, GC9107 e due controller "FF A5" (tipo NV3023). disptest v5 fa lo stesso;
- `dumper pin` + `dumper panel auto` su ogni pin, alto e basso: l'ID compare solo con PA0
  alto, e sparisce con PC6 alto (quindi PC6 è DC);
- la batteria scarica (dopo 6 OTA) faceva riavviare la band in continuazione: una volta messa in
  carica, la v6 ha disegnato la cornice.

Comandi di disptest (`./lt716 dumper ...`): `reg <addr> [n]`, `pin <PA0..PC7>
float|up|down|low|high`, `watch [s]` (bit d'ingresso che cambiano), `panel [0..5|auto]`
(ID e variante di init), `lcdpins <dc> <cs> <rst> <bl> <pwr>`. Watchdog di 4 s dalla v4.

### Prima band: ritrovata (2026-11-24)

La prima band, creduta rotta, era solo nell'**aggiornatore**: battmon, dopo 10 minuti lontano
dal caricatore, ci torna da solo, e l'aggiornatore tiene lo schermo spento e ignora il tasto.
Nelle scansioni del 2026-11-23 il "716" a `FF:FF:CF:10:70:E9` era lei. Collegandosi si vedono
solo GAP, GATT, Device Information e il servizio OTA, senza UART: è l'aggiornatore.

Poi, tutto via OTA verso l'aggiornatore:
1. battmon aggiornato, ma compilato per la seconda band: neve colorata, poi schermo nero (i
   comandi finivano al pannello come dati e la retroilluminazione PB3 restava spenta).
   Nessun danno.
2. battmon con i pin della prima band: schermata della batteria corretta.
3. MorseOS 0.6.1 col rilevamento della band: INFO mostra `BAND 1 ST7735`. Un primo OTA si è
   fermato al 67 % (collegamento perso, l'aggiornatore cancella e resta); il secondo è passato.
   Dopo un riavvio la band è tornata nell'aggiornatore: il contatore di avvii (3 avvii di fila
   senza 15 s sani) ha fatto il suo lavoro, probabilmente per la batteria. In carica, reinstallato.
4. MorseOS 0.6.2 con il MAC per unità: risponde a `A4:C1:38:10:70:E9` (batteria 93 %).

Un OTA di MorseOS (69,7 KB, 4350 pacchetti) richiede circa 3 minuti con questo adattatore.

### Backup cifrato dei dump

`files/firmware/` è fuori da git. Una copia cifrata (tar.gz + gpg AES256, con `SHA256SUMS`
dentro) è in `files/firmware-backup.tar.gz.gpg`; la password non è nel repo. Contiene i dump
della **prima band** (flash completa del 2026-11-16, copie `a` e `b`; banco 0 e aggiornatore letti
via SWire). I dump della seconda band (`band2_flash_{1,2}.bin`) e le immagini del server
(`files/firmware/server/`) sono stati fatti su un altro PC e **non sono nell'archivio**. Per ripristinarlo:

```shell
gpg -d files/firmware-backup.tar.gz.gpg | tar xzf - -C files
(cd files/firmware && sha256sum -c SHA256SUMS)
```

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
DUMPER_MAC=band1 ./lt716 fs ls                     # MorseOS 0.8+: i file della band
DUMPER_MAC=band1 ./lt716 fs cat /log/battery.csv   # una serie in CSV
DUMPER_MAC=band1 ./lt716 fs write /morse/speed 12  # una configurazione
DUMPER_MAC=band1 ./lt716 fs mount ~/band           # la band come cartella (FUSE), Ctrl+C smonta
MAC=band1 ./lt716 alarms                           # MorseOS 0.8.4+: le sveglie da alarms.txt
echo "07:00 MON TUE WED THU FRI" > ~/band/clock/alarms   # o una alla volta dal mount
./lt716 dumper info|dump|assets <src>|updater|reboot   # parla col nostro firmware dumper
```

Variabili d'ambiente: `MAC`, `DUMPER_MAC`, `RETRIES` (default 5), `HCI` (default `hci0`).
`MAC` e `DUMPER_MAC` accettano anche un nome: `band1`, `band2` (MorseOS 0.6.2+), `band1-stock`,
`band2-stock` (firmware originale o aggiornatore), `fixed` (dumper, disptest, MorseOS fino a 0.6.1).
Senza `MAC` si parla con la prima band all'indirizzo di fabbrica.

```shell
MAC=band1-stock ./lt716 ota firmware/morseos/_build/morseos.ota.bin --yes   # verso l'aggiornatore
MAC=band1 ./lt716 settime                   # MorseOS sulla prima band
DUMPER_MAC=band1 ./lt716 dumper updater     # MorseOS -> aggiornatore
```

I comandi del firmware FitPro di fabbrica (gruppo `12` ecc.) MorseOS li capisce solo in parte:
`settime`, `notify`, `find`, `battery`. Il resto va col firmware originale.

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

Cosa è stato fatto sulla prima band il 2026-11-06:

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
l'aggiornatore. Si annuncia come `LT716-DUMP` all'indirizzo fisso `A4:C1:38:71:6D:D0` e vibra 300 ms all'avvio;
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
firmware/build.sh battmon -B           # battmon, riconosce la band all'avvio (vedi "Una build per ogni band")
```

### battmon

Micro firmware senza Bluetooth (`firmware/battmon/`, ~10 KB, `-Os`): mostra le tensioni di
batteria e caricatore, quanto si è mossa la batteria dall'avvio e il tempo rimasto. Pensato per
caricare una band con la batteria troppo bassa per un OTA, perché non accende la radio.

- Schermo spento dopo 30 s senza tocchi; un tocco lo riaccende.
- Sul caricatore resta acceso. Lontano dal caricatore, dopo 10 minuti torna da solo
  all'aggiornatore; prima vibra due volte e scrive "UPDATER / SCHERMO SPENTO / NON E MORTA / SOLO BLE".
- Tasto tenuto 3 s: torna subito all'aggiornatore (due vibrazioni).
- Stesse protezioni di MorseOS 0.5.7: watchdog e contatore di avvii.

Le build con `BAND=1` e `BAND=2` sono state provate sulle band; la build automatica (predefinita)
usa lo stesso codice di rilevamento di MorseOS, ma su battmon non è ancora stata provata.

### Display della prima band (trovato con `firmware/disptest`)

Per la seconda band vedi [Hardware della seconda band](#hardware-della-seconda-band-trovato-con-firmwaredisptest-v4v6).
Il pannello 0,96" della prima band è un ST7735S 80×160 sui pin documentati da e-lab (MOSI PC3, CLK PC5, DC PC1,
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
`LT716`, `LH716`) risponde `{"success":true,"data":null}`. Nel 2026-11 la stessa risposta
è arrivata per `V18185` con 84 combinazioni di nome (`716`, `LT716`, `LH716`, `716:g:h:l`...),
versione e `platform` 0/1/2.

L'endpoint `/api/v1/config/app/upgrade?name=<nome>` (stesso token) invece restituisce un pacchetto:

| `name` | `version` | Pacchetto (`app.bin` dentro lo zip) | Stringhe nell'immagine |
|---|---|---|---|
| `716` | `V07178` | `res.jusonsmart.com/bin/202208/V08928_A08161706_LT716_V5.2_JQGB_M6_FIP.zip` | `V08928`, `LT716(G)`, 2022-08 |
| `LT716` | `V15850` | `res.jusonsmart.com/0ta/LT716/V17899_A10171436_LT716_V5.3_KM_M4_INNOVA.zip` | `V17899`, `LT716(D)`, 2023-10 |
| `LH716` | `V36944` | `res.jusonsmart.com/0ta/LH316/V36785_OTA_1120.zip` (per l'LH316, non scaricato) | |

Il campo `version` non coincide con la versione dentro l'immagine. Le due immagini LT716 sono
in `files/firmware/server/` (fuori da git, con `SHA256SUMS`). Nessuna delle due è il `V18185`
della seconda band. `V17899` è la più vicina come numero, ma è una variante `(D)` mentre la
prima band è `(G)`: **non va flashata** senza aver capito quale variante è la seconda band.

## MorseOS

Firmware nostro per l'LT716 (`firmware/morseos/`, versione 0.6.2): il tasto touch si usa in Morse.
Gira su entrambe le band con la stessa immagine (vedi [Una build per ogni band](#una-build-per-ogni-band)).
Roadmap e idee: [`firmware/morseos/IDEE.md`](firmware/morseos/IDEE.md).

- **Quadrante**: ora grande `00:00` a 7 segmenti, sotto `SAB 7`, in alto `BT` e batteria, in fondo
  il segno del tocco. Schermo spento dopo 15 s; il tocco che lo riaccende mostra solo il quadrante.
- **Accesso**: la parola Morse **TAN** (`- .- -.`) apre il menu. Il decodificatore impara il ritmo
  (medie separate di punto e linea), chiude la lettera dopo 600 ms, riconosce TAN appena completa.
- **Menu a riquadri**: due box per schermata, quello in alto è selezionato; tocco veloce scorre,
  tenuto (la barra si riempie in 800 ms) apre. MENU: INFO, STRUMENTI, IMPOSTAZIONI, TORNA;
  STRUMENTI: PRATICA (lettera da scrivere col suggerimento, OK/NO, punti; 3 s premuto = esci)
  e SENSORI (se abilitato nel bundle, non fa la scansione sulla band 2).
  INFO: versione, band rilevata (`BAND 1 ST7735`), batteria, Bluetooth, tempo acceso, ritmo Morse.
  IMPOSTAZIONI: velocità Morse 1–16, vibrazione.
- **Da PC** (`MAC=band1` o `band2`, dalla 0.6.2; prima l'indirizzo fisso `A4:C1:38:71:6D:D0`):
  `./lt716 settime | notify | find | battery`, `DUMPER_MAC=band1 ./lt716 dumper touchlog`.
- **Uscita verso l'aggiornatore**:
  - dal PC: `DUMPER_MAC=band1 ./lt716 dumper updater`;
  - dal tasto: tienilo premuto 60 s (schermo rosso; 10 s fino alla 0.8.3), **rilascia e dai 3 tocchi brevi** (< 400 ms
    ciascuno, entro 5 s). Qualsiasi altra cosa annulla;
  - da sola: 3 avvii di fila senza 60 s di funzionamento sano (15 s fino alla 0.8.0) (watchdog di 4 s dalla 0.5.7).
  Nell'aggiornatore lo schermo è spento e il tasto non fa nulla: la band sembra morta, ma si
  annuncia col MAC di fabbrica e accetta un OTA.

### Moduli e manifest

```
firmware/morseos/
├── bundle.ini        manifest: [apps], [features] (y/n), [hardware] band (auto/1/2)
├── core/system       BLE, GATT, comandi dumper, aggiornatore, emergenza, MAC, main loop
├── core/ui           framework (mos.h): app, menu generati dal bundle, input, schermo
├── lib/              display (con la tabella delle band), font, morse, rtc, battery, fitpro, i2cprobe
└── apps/             face, info, practice, settings, sensors, message
```

Ogni modulo ha un `module.ini` (`type`, `depends`, `sources`, `menu`, `label`).
`firmware/tools/bundle.py` (lanciato da `build.sh`) risolve le dipendenze, scarta le librerie che
nessun modulo selezionato usa (insieme ai loro servizi GATT e voci di menu), genera `_gen/`
(`bundle.h`, `bundle.mk`, tabella delle app) e dopo la build stampa flash e RAM per modulo.

```shell
firmware/build.sh morseos       # -> firmware/morseos/_build/morseos.ota.bin
DUMPER_MAC=band1 ./lt716 dumper updater                    # dal firmware in esecuzione all'aggiornatore
MAC=band1-stock ./lt716 ota firmware/morseos/_build/morseos.ota.bin --yes
```

### Una build per ogni band

Le due band note differiscono solo nei collegamenti dello schermo. Il driver
(`lib/display/st7735.c`, copiato in `firmware/battmon/`) ha una tabella di board, ciascuna con i
suoi pin e l'ID del controller che la identifica:

| Band | Controller (RDDID) | DC | Retroilluminazione | Alimentazione |
|---|---|---|---|---|
| 2 | GC9107 (`00 91 07`) | PC6 | PA2 | PA0 |
| 1 | ST7735S (qualsiasi ID, è la riserva) | PC1 | PB3 | nessuna |

**Rilevamento automatico** (predefinito): al primo `lcd_init()` il firmware prova la band 2 e
legge l'ID del pannello. Se risponde `00 91 07` la tiene, altrimenti rilascia quei pin e passa
alla band 1. L'ordine non è casuale: i pin della band 2 sono stati pilotati per minuti sulla band 1
(battmon per la band 2, 2026-11-24) senza danni, mentre quelli della band 1 sulla band 2 non sono
mai stati provati, e così non vengono mai toccati. INFO mostra `BAND 1 ST7735` o `BAND 2 GC9107`.
SENSORI sulla band 2 non fa la scansione (i suoi pin possono alimentare l'IC del touch).

La scelta si fissa in `bundle.ini` (`[hardware] band = auto | 1 | 2`) o al volo:

```shell
firmware/build.sh morseos -B            # band dal bundle.ini (auto)
firmware/build.sh morseos -B BAND=1     # solo prima band, nessuna prova all'avvio
firmware/build.sh battmon -B BAND=2     # battmon: stessa variabile, 0 = auto (predefinito)
```

`-B` ricompila tutto: va usato quando si cambia `BAND`, perché make non vede il cambio di define.
Una nuova band si aggiunge con una riga nella tabella `boards[]`, prima della riserva.

## File

| Percorso | Contenuto |
|---|---|
| `lt716` | script di controllo |
| `alarms.txt` | sveglie applicate da `./lt716 alarms` |
| `files/protocol-notes.md` | protocollo completo ricavato dall'APK |
| `firmware/dumper/` | sorgenti del firmware dumper |
| `firmware/disptest/` | firmware di prova del display: pin, ID del pannello, varianti di init, pattern |
| `firmware/battmon/` | micro firmware senza Bluetooth: tensioni di batteria e caricatore, poi aggiornatore |
| `firmware/morseos/` | MorseOS: core, librerie e app a moduli, manifest `bundle.ini` |
| `firmware/tools/bundle.py` | risolutore del manifest MorseOS e report delle dimensioni |
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
