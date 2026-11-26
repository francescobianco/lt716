# MorseOS — idee e roadmap

Raccolta delle idee man mano che emergono. Stato: ✅ fatto, 🔨 in corso, 📌 in coda, 💤 più avanti.

## Paradigmi di UI (da riusare)

- **Tieni premuto per confermare, con barra di carica sul bersaglio** (0.5.1): mentre il tasto è
  premuto, lungo il bordo basso dell'elemento a fuoco si riempie una barra verde; piena = azione
  eseguita subito (con una vibrazione breve), rilascio prima = barra azzerata e nessuna azione.
  Solo un tocco davvero breve (< 0,3 s) fa l'azione "leggera" (es. passare al box successivo).
  Dà il senso che la pressione sta agendo proprio su quel bersaglio. Da usare per ogni
  conferma: aprire una voce, giocare una mossa a scacchi, disconnettere il Bluetooth, cancellare.
- **Freccia indietro** (0.5.4): nei menu TORNA e INDIETRO sono una freccia, non un testo.
- **Il tocco che sveglia lo schermo non conta**: mostra solo il quadrante.
- **Niente latenze né lampi** (0.5.3): lo schermo si prepara al buio (il pannello tiene
  l'immagine, si spegne solo la retroilluminazione); il quadrante si aggiorna anche a schermo
  spento, quindi il risveglio è solo accendere la luce. I disegni completi scrivono ogni zona una
  volta sola (niente cancellazione a nero di tutto lo schermo prima di ridisegnare).
- **Stato del link in alto a sinistra** (0.5.3): disconnesso = niente; connesso = prime 3 lettere
  del nome del dispositivo (letto via GATT, `0x2A00`) + 4 barrette di segnale come i telefoni.
- **Uscita da uno strumento**: tenere premuto 3 s, con vibrazione.
- **Rettangolo guida (tutor)** (0.5.2): mentre premi, un rettangolo bianco largo come un punto;
  quando la pressione supera la soglia del punto si allunga a linea. Al rilascio diventa giallo.
- **Pressione prolungata = reset dell'input** (0.7.0, sostituisce la raffica di 4 tocchi della
  0.5.2): oltre la soglia (3 × punto, tra 1,2 e 2,5 s) l'input si azzera subito, con due
  micro-vibrazioni e un segnale visivo mentre il tasto è ancora giù; la digitazione riprende
  solo dopo il rilascio, così prolungare la pressione non assume altri significati.
  Dalla 0.8.2 niente scritta RESET: la riga del testo diventa rossa e una spazzola bianca la
  attraversa da destra a sinistra come un tergicristallo, lasciandola nera (`mos_wipe`).
- **Menu a tre riquadri** (0.7.0): il fuoco parte dal primo, scende a ogni tocco, resta al centro
  mentre la lista scorre e arriva al terzo solo sull'ultima voce; un tocco sull'ultima torna in
  cima. Se non serve scorrere si ridisegnano solo i due contorni. Barra di scorrimento a destra
  con la posizione della voce a fuoco. Menu principale a icone (`lib/icons`).
- **Emergenza** (0.5.2; 60 s dalla 0.8.4, prima 10 s): 60 s premuto = schermo rosso; poi
  rilascia e 3 tocchi brevi entro 5 s. Un contatto fisso non può farla scattare; tasto premuto
  > 90 s (prima 30 s) = ignorato finché non si rilascia (evento `S` nel touchlog).

## Fatto

- ✅ **Input Morse sul tasto touch** (0.1–0.2): decodificatore adattivo (medie separate di
  punto e linea), fine lettera dopo 600 ms, annulla con linea > 2,5 s, filtro < 30 ms,
  `lt716 dumper touchlog` per vedere ogni tocco.
- ✅ **Accesso con TAN** (`- .- -.`): riconosciuto appena la N è completa (0.2).
- ✅ **Quadrante** (0.3): ora grande 7 segmenti, `SAB 7`, BT, batteria, segno del tocco,
  spegnimento dopo 15 s, il tocco che sveglia mostra solo il quadrante.
- ✅ **Menu a riquadri** (0.3): due box per schermata, tocco veloce scorre, lungo conferma.
- ✅ **Orologio, batteria, FitPro su UART** (0.3): `lt716 settime / notify / find`.
- ✅ **STRUMENTI > PRATICA** (0.4): lettera da scrivere col suggerimento, OK/NO, punti e serie.
- ✅ **Emergenza a prova di bambino** (0.4): 10 s = schermo rosso "RILASCIA PER ANNULLARE",
  20 s = aggiornatore del produttore. Sostituita dalla 0.5.2: 10 s premuto, rilascia, 3 tocchi
  brevi entro 5 s (vedi sopra).
- ✅ **Framework a moduli + manifest `bundle.ini`** (0.5): `core/`, `lib/`, `apps/`, ogni modulo con
  `module.ini` (tipo, dipendenze, sorgenti, menu, etichetta); `tools/bundle.py` risolve le
  dipendenze, scarta le librerie che nessuno usa, genera `bundle.h`, `bundle.mk` e la tabella
  delle app da cui nascono i menu (un menu vuoto non compare). Report di flash/RAM per modulo.

- ✅ **Velocità di input 1–16** (0.5.2, 16 livelli da 0.5.4): `lt716 dumper speed N`, soglia del
  punto da 1000 ms a 150 ms in progressione geometrica (~12 % per livello), fine lettera = 2×,
  annulla = 5× (min 1,5 s); default 9 (364 ms), un po' più veloce del vecchio 7. Testo digitato sul quadrante grande e bianco.

- ✅ **Risparmio energetico** (0.5.4): CPU in sospensione a schermo spento (risveglio dal tasto
  PC2), display in SLPIN (immagine conservata), annuncio ogni 500 ms, connessione 30–50 ms con
  latenza 4 (veloce solo per dump/scritture/OTA), TX 3,3 dBm. `lt716 dumper battery` per mV e
  deriva dell'orologio. Da verificare con misure nel tempo.

- ✅ **MENU > IMPOSTAZIONI** (0.6.0): lista di proprietà con valore (VELOCITA 1–16,
  VIBRAZIONE SI/NO, INDIETRO). Tap = proprietà successiva; tenuto (barra verde) = modifica,
  poi i tap cambiano il valore e un altro tenuto conferma; tenuto su INDIETRO = menu. Il menu
  principale segue l'ordine del bundle: INFO, STRUMENTI, IMPOSTAZIONI, TORNA.
- ✅ **Seconda band** (0.5.8–0.5.11): display GC9107 (alimentazione PA0, DC PC6, retroilluminazione
  PA2, MADCTL `0x00`), indicatore Li-ion con fine carica, animazione di carica.
- ✅ **Una sola immagine per tutte le band** (0.6.1): tabella `boards[]` nel driver del display,
  rilevamento dall'ID del pannello all'avvio, `[hardware] band = auto|1|2` nel bundle o
  `make BAND=`. INFO mostra la band. Provato sulla prima band; sulla seconda ancora da provare.
- ✅ **MAC per unità** (0.6.2): `A4:C1:38` + gli ultimi 3 byte del MAC di fabbrica letto a
  `0x7f100` (prima band `A4:C1:38:10:70:E9`). Da verificare sulla seconda band.
- ✅ **MorseOS 0.7.0**: quadrante con cifre a font (Ubuntu Condensed Bold antialiasato, 4 bit,
  `tools/mkassets.py`) e due punti che lampeggiano ogni secondo; il vecchio orologio a segmenti
  resta scegliendo `[options] clock = segments` in `bundle.ini` (scelta di build, non a runtime).
  Icone 32×32 per INFO, STRUMENTI, GIOCHI, IMPOSTAZIONI e la freccia indietro. Menu a tre
  riquadri, reset con pressione prolungata. **GIOCHI > RIFLESSI**: tempo di reazione, gioco
  segnaposto.
- 🔨 **File della band** (0.8.0, da provare sulla band): quello che la band sa e permette di
  configurare, visto dal PC come piccoli file di testo, con la sensazione di `/proc` e `/sys`
  senza copiarne la struttura. `./lt716 fs ls|cat|write`, `./lt716 fs mount <dir>` (FUSE, binding
  ctypes a libfuse3 in `lt716fs.py`, nessun pacchetto da installare). Sulla band
  `core/system/files.c`: tabella fissa `{percorso, leggi, scrivi}`, un valore per file, serie in
  CSV (`/log/battery.csv`, un campione ogni `/log/battery.interval` minuti, 48 campioni).
  Compromessi: niente creazione/cancellazione/permessi, cartelle solo nei nomi, il contenuto
  non è mai in un buffer (ogni pacchetto rigenera il testo e tiene la sua finestra di 16 byte),
  tutto in RAM. Protocollo in `files.h` (comandi 40–42 sulla caratteristica del dumper).
  `/sys/stack_free`: lo stack libero è dipinto all'avvio, il file dice quanto non è mai stato
  toccato. **La RAM è il limite** (16 KB: 6,8 KB di ram_code della libreria Telink, 2,3 KB
  di code BLE e stack IRQ dell'SDK, il resto bss + stack). Stack libero: 0.7.0 ~1200 B,
  0.8.0 ~1080 B, **0.8.1 ~1816 B** grazie a: campioni del log da 4 byte (orario ricavato
  all'uscita da uptime + orologio, % dalla curva), FitPro che tiene solo i primi 181 byte di un
  pacchetto ma lo conta fino in fondo, testo del messaggio 170 B, voci del menu da 2 byte,
  scritture degli asset del dumper (comandi 10–13, `page_buf` 256 B) dietro
  `[features] assetwrite`, spente.
- ✅ **Asset compressi** (0.8.3): cifre e icone in RLE con il livello nel codice
  (`00nnnnnn` sfondo, `01nnnnnn` pieno, `1vvvvnnn` sfumatura v), 5790 → 3038 B senza perdita,
  decodificati direttamente nella finestra SPI (`lcd_blit_rle`). Provati e scartati: letterali
  impacchettati (3921 B, i bordi sono gruppi di 1–2 pixel), XOR con la riga sopra (aiuta le
  cifre, peggiora le icone), 2 bit (−0,7 KB in più ma bordi meno morbidi).
- ✅ **RAM, 0.8.8**: circa 350 B di stack in più. Buffer più corti (log batteria 24 campioni =
  12 h, log del link 16 eventi, scansione BT 6 dispositivi, riga scritta nei file 39 byte, testo
  delle notifiche 120 caratteri: −272 B di bss) e i pacchetti delle risposte 31/33 fuori da
  `main()`, il cui frame sta in fondo a ogni catena di chiamate (172 → 92 B). Misurato: `-Os`
  riduce la catena più profonda solo del 15 % (384 → 324 B), il resto dello stack è della
  libreria Telink.
- 📌 **`-Os` sul codice MorseOS** (misurato 2026-11-25): il progetto compila senza `-O`, cioè
  `-O0`. `make APP_OPT="-Os -fno-strict-aliasing"` solo sulle nostre sorgenti: bin da 83,5 a
  70,0 KB (−13,5 KB); i driver SDK restano `-O0` come provati (con `-Os` anche loro: altri
  −4,3 KB, ma `flash.c` gira in RAM con tempi precisi: no). Da flashare da solo, dopo la 0.8.3,
  rileggendo `/sys/stack_free` (gli inline possono cambiare l'uso dello stack).
- 📌 **Ancora RAM, ma toccando lo stack BLE** (da provare in un flash a parte): coda TX
  `MYFIFO_INIT(blt_txfifo, 40, 16)` → 8 voci = −320 B (meno notifiche in coda: dump più lento).
  ram_code non si riduce: è tutto codice di libreria (radio, sospensione, flash).
- ✅ **Guardia a 60 s** (0.8.1): l'avvio è "sano" dopo 60 s di ciclo principale invece di 15; la
  CPU non va in sospensione finché non lo è, così i 60 s sono tempo reale.
- 📌 **File in flash**: impostazioni e serie che sopravvivono al riavvio, in un settore libero,
  in una versione a parte (regole dell'incidente 0.5.5). Poi **/log/heart.csv** ogni 30 minuti
  quando il sensore del battito funziona, con lo stesso meccanismo di `/log/battery.csv`.
- 📌 **dumper e disptest** usano ancora l'indirizzo fisso `A4:C1:38:71:6D:D0`: portarli al MAC
  per unità e al driver con la tabella delle band.

## In corso

- ✅ **Watchdog + contatore di riavvii** (0.5.7, 3 avvii di fila senza 60 s sani (15 s fino alla 0.8.0; la CPU non va in
  sospensione finché l'avvio non è sano, così sono 60 s veri) →
  aggiornatore). Motivo e regole in [`INCIDENTE-0.5.5.md`](INCIDENTE-0.5.5.md): la 0.5.5 ha
  bloccato la prima band (`pm_get_32k_tick()` nel ciclo principale, nessun watchdog). Il
  2026-11-24 è scattato davvero sulla prima band (batteria), riportandola all'aggiornatore.
- 📌 **Orologio in sospensione**: oggi resta indietro mentre la CPU dorme; da risolvere senza
  `pm_get_32k_tick()` (es. risincronizzazione a ogni connessione).

- 🔨 **STRUMENTI > SENSORI** (0.5.4): I2C software (open drain, pull-up, sola lettura) su ogni
  coppia di PA3/PA4/PC4; elenca chi risponde e legge l'ID dei sensori noti. Anche via BLE:
  `lt716 dumper sensors`. Dal firmware originale: sensore del battito **HRS3300** a `0x44`
  (ID `0x21` nel registro `0x00`), su **I2C software SDA = PA4, SCL = PC7** (il pad SWS),
  pull-up 10k: ricavato disassemblando lo start I2C del firmware originale (0.5.5 prova PC7 e
  sblocca il bus con 9 impulsi di clock se SDA è basso).

## In coda

- 📌 **GIOCHI > SCACCHI** (0.6): scacchiera 80×80, caselle 10 px, sprite 8×8 dei pezzi, motore
  alfa-beta (0x88, 3–4 semimosse + catture), mosse in Morse: casella d'arrivo (`e4`), se ambigua
  chiede la partenza; lettere inglesi dei pezzi (`Nf3`). Libreria `chess_engine` separata.
- 🔨 **STRUMENTI > BLUETOOTH** (0.8.3, da provare sulla band): dispositivi in zona, dal più
  forte, nome (o fine dell'indirizzo) e 4 tacche; scansione passiva della libreria Telink fra un
  advertising e l'altro e nei vuoti della connessione (come il campione SDK
  `TEST_SCAN_IN_ADV_AND_CONN_SLAVE_ROLE`), senza ruolo master. Accesa solo mentre lo strumento
  è aperto o per N secondi con `/bt/scan`; `/bt/devices` in CSV. Niente cambia all'avvio:
  le funzioni della libreria (disassemblate, tutte semplici scritture) si chiamano solo alla
  prima scansione. Niente sospensione mentre scansiona. Lib `blescan`.
- 📌 **App BLUETOOTH, il resto** (0.7): stato del link (indirizzo, segnale, durata), dispositivi in zona
  (scansione in advertising e in connessione), disconnetti, "connetti a…" come periferica:
  white list + advertising diretto verso il dispositivo scelto (la libreria non ha il ruolo
  master). Libreria `ble_scan`.
- ✅ **Come il firmware originale "misura" il battito** (2026-11-26, V10712, risalendo da
  `15 0E`): la macchina a stati della misura `0x92a6` sceglie una durata a caso (`timer % 5 +
  20` s) e poi, secondo un flag (campo 28 della struttura a `0x80b014`):
  - **flag 0, sensore presente**: il compito periodico `0x4ee0` legge C0 e C1 dall'HRS3300 e li
    passa alla libreria `0x18344` (come `Hrs3300_alg_send_data`); ogni 24 campioni chiede il
    risultato (`0x17f68`), con stato 5 fa la media di 5 battiti → campo 26 = **battito vero**;
    pressione da un'altra libreria (`0x1892c`);
  - **flag 1, sensore assente**: **battito = timer di sistema % 15 + 70** (70–84), pressione
    `% 28 + 60` / `% 45 + 90`, SpO2 `% 5 + 90`: valori inventati.
  Il flag lo accende l'init del sensore (`0x4e24`) quando l'ID `0x21` non risponde in 5 letture.
  Nell'immagine D (V17899) lo stesso flag fa anche **lampeggiare PC7** (`0x4b98`): è il LED verde
  della seconda band. Quindi la seconda band (LED su PC7, nessun HRS3300) col suo firmware
  originale mostrava un battito **finto**, con il LED che lampeggiava per scena; la prima band
  (HRS3300 su PA4/PC7) ha il percorso vero.
- 🔨 **SALUTE > BATTITO** (0.8.7, da provare sulla band): menu SALUTE con icona del cuore. Lettore
  semplice che mostra i passaggi: LED (C0), AMB (C1), DC (media ~1,6 s), AC (DC − C0), PEN
  (pendenza su 100 ms), SOG (metà dei picchi recenti di PEN), IBI, BPM (media di 4 IBI fra 300 e
  1500 ms), onda AC che scorre con i battiti in rosso. I battiti si cercano sulla **pendenza**:
  la salita del battito è molto più rapida della deriva della luce quando la band si muove, che
  sbilancia l'AC (provato su segnale sintetico con deriva e rumore: entro 1–2 BPM da 50 a 140).
  HRS3300 a `0x44` su I2C software SDA/SCL = PA4/PC7 (dalla prima band; sulla seconda è
  un'ipotesi): si tocca solo all'apertura, linee alte a riposo e ID `0x21` prima di scrivere,
  spento 1 s dopo l'uscita. `/health/hr_pins` per provare altri pin, `/health/hr_state`.
  **Seconda band: "LINEE BASSE"** (2026-11-26): con la pull-up PA4 o PC7 si leggono bassi a
  riposo. Ipotesi: sensore non alimentato (un chip spento tiene le linee basse con i diodi di
  protezione); sulla seconda band portare basso uno fra PA4, PA7, PB1, PB2, PC7 accende il LED
  verde: PA7 è il candidato per l'alimentazione (attivo basso). 0.8.8: `/health/hr_power`
  (pin da portare basso mentre il sensore è in uso) e `/health/hr_scan` (scrivere `1`: livelli
  a riposo dei pin ammessi e ricerca degli indirizzi dei sensori soliti su ogni coppia).
  Prove 0.8.8: a riposo **A4 e B5 bassi**, tutti gli altri alti; nessun dispositivo; nessun pin
  attivo basso cambia qualcosa. **Immagini del server disassemblate** (V08928 G, V17899 D, come
  la V10712 della prima band): stessa tabella dell'HRS3300, I2C software SDA = PA4, SCL = PC7,
  **nessun pin di alimentazione**; PA4 è **condiviso** con l'I2C hardware (`AS_I2C_MSD` su PA4,
  `AS_I2C_MCK` su PA3), quasi certamente l'accelerometro. 0.8.9: sblocco del bus (fino a 9
  impulsi di clock su PC7, poi PA3) se SDA è basso, esito in `/health/hr_scan`.
  Prova 0.8.9: **sblocco fallito**, PA4 resta basso: è tenuto basso dalla scheda, la seconda
  band ha probabilmente una scheda diversa da G/D/V10712 (anche il display è diverso). 0.8.10:
  sulla sola seconda band la scansione prova anche PB0, PB3, PC1 (sulla prima sono del display).
  Prova 0.8.10: niente su nessuna coppia. **Il LED verde lampeggia durante le scansioni**: è su
  un pin del microcontrollore (attivo basso), non pilotato da un HRS3300. Nuova ipotesi: sensore
  **analogico** sulla seconda band, LED su un pin e fotodiodo letto con l'ADC; B5 (pin ADC) basso
  a riposo = fotodiodo al buio. 0.8.11: `/health/pin` (`A7 0|1|z`) e `/health/adc` (tensione su
  PA7/PB0..PB7) per trovare il pin del LED e quello del fotodiodo.
  **Trovato (2026-11-26): il LED verde della seconda band è su PC7, attivo basso** (lampeggi
  contati a occhio: 5 lampeggi = PC7, confermato). Sulla prima band PC7 è il clock I2C
  dell'HRS3300: le due schede sono diverse. B5 (578 mV a riposo) non cambia col LED acceso al
  polso. Prossimo: LED acceso al polso, misurare tutti i pin ADC per trovare il fotodiodo.

- 📌 **Accelerometro**: scansione I2C (sola lettura) su PA3/PC4, interrupt su PA4; identificare il
  chip (SC7A20, STK8321, DA213, QMA7981…), poi X/Y/Z a schermo, **contapassi**, **alza il polso
  per accendere**, sonno.
- 🔨 **Sveglie** (0.8.4, da provare sulla band): fino a 8, ora + giorni. Dal PC con
  `./lt716 alarms` (alarms.txt, FitPro `12 02`; `--show` rilegge con `18`) o con il file
  `/clock/alarms` (`07:00 MON FRI` aggiunge/cambia, `-07:00` toglie, vuoto = tutte via, più
  righe in una scrittura = una per volta). Quando suona: schermo acceso, SVEGLIA e l'ora, una
  vibrazione ogni 1,5 s anche con VIBRAZIONE = NO, un tocco la ferma, al massimo 60 s.
  **Solo RAM**: si perdono al riavvio e suonano solo con l'orologio impostato: sono il primo
  motivo per salvare in flash.
- 🔨 **Campanella delle sveglie** (0.8.6): nel quadrante, fra il link e la batteria, una
  campanella gialla 10×10 finché c'è almeno una sveglia.
- 🔨 **STRUMENTI > DISTANZA e log del link** (0.8.6, da provare): niente accelerometro (chip non
  ancora identificato) né contapassi, quindi la distanza è stimata dall'RSSI medio del link
  con il modello log-distanza `d = 10^((RSSI_1m − RSSI)/(10 n))`: calibrazione a 1 m tenendo
  premuto nello strumento (o `/bt/rssi_1m`), `n` in `/bt/path_n10` (25 = 2,5). Indicativa:
  corpo, muri e orientamento la cambiano anche del doppio. Il dato più solido è `/bt/range`:
  l'RSSI medio prima di una perdita per timeout (motivo 0x08 = fuori portata; 0x13 = il PC ha
  chiuso, non conta). Log `/bt/link.csv` (connessioni, perdite col motivo, un campione ogni
  `/bt/link.interval` s), 32 eventi in RAM. Prova: allontanarsi dal PC finché si stacca, poi
  rileggere `/bt/range` e `/bt/link.csv`. Lib `linklog`.
- 🔨 **STRUMENTI > LUCE** (0.8.12, da provare): schermo bianco con retroilluminazione al
  massimo e LED verde (PC7, solo seconda band); tap: acceso → SOS in Morse (unità 200 ms,
  schermo e LED insieme) → spento → acceso; pressione lunga: esce. Lo schermo non si spegne
  mentre è aperta. Lib `greenled`: il LED si spegne da solo 300 ms dopo l'ultima richiesta.
- 🔨 **Microfirmware di scoperta "diag"** (`diag.ini`, `BUNDLE=diag.ini firmware/build.sh
  morseos -B BAND=2`): il nucleo sicuro di MorseOS più `lib/hwscan`: profilo di ogni pin
  (pull-down/pull-up: flottante, tenuto alto, tenuto basso) e I2C completo (indirizzi
  0x08–0x77 su ogni coppia di pin che può stare alta, registri 0x00/0x01/0x0F di chi risponde),
  una coppia per giro del ciclo. `echo 1 > ~/band/hw/scan`, poi `cat`.
- **Ricerca (2026-11-26)**: il package ET24 della LT715 (e-lab) ha solo 16 GPIO, senza PA0, PA2,
  PA7, PB0, PB6, PB7, PC0, PC6: la seconda band (PA0, PA2, PC6 per il display) ha un package
  più grande, come la band **M6** di rbaron (display RS su PC6, retroilluminazione PA2), il cui
  sensore del battito è "most likely fake". Ipotesi: sulla seconda band il "battito" è solo il
  LED su PC7. davidgiven/telinkdebugger (SWS con un Pico) è provato sulle LT716.
- 🔨 **Il contapassi è un interruttore di vibrazione su PA4, non un accelerometro** (trovato il
  2026-11-26 risalendo nel firmware originale V10712 dalla risposta FitPro `15 0D`): passi =
  struttura a `0x80b014`+0x205, scritti da `0x3d8`; il compito periodico `0x2b4` somma i passi
  nuovi di `0x15a04` (libreria dei passi, `0x15640`–`0x15b60`); `0x675c` la inizializza:
  interrupt su PA4 (`0x66e8`, pull-up) con gestore `0x64e0` che inverte la polarità a ogni fronte
  e avvisa la libreria (`0x158f4`), parametri `4, 4, 30, 3`. Sulla seconda band PA4 "tenuto
  basso" = interruttore chiuso a massa a riposo. 0.8.13: `/health/motion` (livello e fronti di
  PA4, letto dal ciclo principale) per vederlo reagire; poi il contapassi sopra.
  **Confermato sulla seconda band** (0.8.13): a riposo livello 0 e 0 fronti; ~10 s di scosse =
  976 fronti (il contatto rimbalza, ~100 fronti/s). Il contapassi dovrà raggruppare i rimbalzi in
  colpi e contare solo quelli a cadenza di camminata, con l'interrupt anche a CPU addormentata.
  **0.8.14, contapassi**: PA4 sveglia la CPU (livello di risveglio sempre opposto a quello
  attuale: una sveglia per cambio, mai sveglia fissa); colpo = fronte dopo ≥150 ms di quiete;
  passo = colpo a 250–2000 ms dal precedente, contati dopo 4 di fila (le scosse isolate no);
  totale del giorno sul quadrante, `/health/steps`, `/health/step_cfg`, FitPro `15 0D` (risposta
  `15 0C` come l'originale; distanza 70 cm e 0,04 kcal a passo, stime). Simulato: 50 passi → 50,
  30 lenti → 30, colpi isolati e scuotimento → 0. Da tarare con una camminata vera.
- ✅ **Tutto il quadrante in Ubuntu Condensed Bold** (0.8.5): data e lettere digitate a 20 px,
  nome del dispositivo connesso a 10 px, proporzionali e antialiasati come le cifre (lib
  `aafont`, glifi in RLE da `tools/mkassets.py`, 4,75 KB).
- ✅ **Seconda band su MorseOS 0.8.4** (2026-11-26), immagine `BAND=2` (pin fissi, niente
  rilevamento): MAC per unità `A4:C1:38:12:99:71` verificato.
- ✅ **Puntini più lenti** (0.8.3): accesi 1 s, spenti 1 s (prima mezzo secondo ciascuno).
- ✅ **Freccia indietro curva** (0.8.3): icona a U come "torna", al posto della freccia dritta.
- ✅ **Fine lettera più rapida** (0.8.2): pausa di fine lettera 1,5 × soglia del punto invece di
  2 × (alla velocità 9: 546 ms invece di 728), come nel Morse standard (3 unità contro una
  soglia di 2). Regolabile dal vivo: `/morse/letter_ms` (150–3000 ms, `auto` = dalla velocità).
- 📌 **Tuning Morse**: ritoccare soglie col touchlog quando l'utente è "meno arrugginito".

- 📌 **Impostazioni salvate in flash**: oggi VELOCITA e VIBRAZIONE tornano a 9 / SI a ogni
  riavvio. Una scrittura in flash è codice rischioso: da sola, in una versione a parte.
- 📌 **Fascia più chiara in alto** (righe 0–32) sulla seconda band, solo dentro MorseOS:
  disptest è uniforme (nero, bianco, grigio, scacchiera, anche dopo SLPIN/SLPOUT e scritture in
  sleep). Da capire se dipende dall'animazione della batteria, dalle letture ADC su PB1/PB2 o
  dal contenuto del quadrante.
- 📌 **Indagare lo spegnimento spontaneo**: due volte la band è finita nell'aggiornatore; il
  touchlog con l'evento `S` dirà se il tasto resta "premuto" da solo.

## Più avanti

- 💤 **STRUMENTI > TASTIERA**: Morse → tastiera Bluetooth HID verso PC/telefono. Codici speciali
  già nel decodificatore: `..--` spazio, `.-.-` invio, `----` cancella. Abbinamento avviato
  dall'host, chiavi nel settore libero `0x74000`; dopo il primo abbinamento dump di verifica.
- 💤 **Dispositivi abbinati** nell'app Bluetooth (arrivano con l'abbinamento della tastiera).
- 💤 **Comandi Morse diretti dal quadrante** (es. T ora, S cronometro, F trova telefono, N ultima
  notifica).
- 💤 **Sensore del battito**: la band dichiara "heart=1" ma il pin non è ancora noto.
