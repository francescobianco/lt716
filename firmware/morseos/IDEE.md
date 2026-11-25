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
- **Emergenza** (0.5.2): 10 s premuto = schermo rosso; poi rilascia e 3 tocchi brevi entro 5 s.
  Un contatto fisso non può farla scattare; tasto premuto > 30 s = ignorato finché non si
  rilascia (evento `S` nel touchlog).

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
- 📌 **SALUTE > BATTITO**: HRS3300 (init del firmware originale: `16←78`, `17←0D`, `0C←4E`/`6E`,
  `02..07,0B←00`; acceso `01←D0`, spento `01←00`). Onda PPG grezza che scorre (canale C0 =
  luce del LED riflessa), C1 = luce ambiente, segno che pulsa a ogni battito, BPM dai picchi,
  campioni anche via BLE.
- 📌 **Accelerometro**: scansione I2C (sola lettura) su PA3/PC4, interrupt su PA4; identificare il
  chip (SC7A20, STK8321, DA213, QMA7981…), poi X/Y/Z a schermo, **contapassi**, **alza il polso
  per accendere**, sonno.
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
