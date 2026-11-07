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
- **Raffica di tocchi = cancella** (0.5.2): 4 tocchi qualsiasi in meno di 0,7 s (quasi il ritmo
  massimo di un dito) cancellano subito quello che si è scritto. Conta la frenesia, non il
  timing dei punti: deve durare molto meno dell'azzeramento automatico. Due micro-vibrazioni.
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
  20 s = aggiornatore del produttore.
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

## In corso

- ⚠️ **Prima di tutto, sul doppione: watchdog + contatore di riavvii** (3 blocchi di fila →
  aggiornatore). Motivo e regole in [`INCIDENTE-0.5.5.md`](INCIDENTE-0.5.5.md): la 0.5.5 ha
  bloccato la prima band (`pm_get_32k_tick()` nel ciclo principale, nessun watchdog).
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
- 📌 **App BLUETOOTH** (0.7): stato del link (indirizzo, segnale, durata), dispositivi in zona
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
- 📌 **Tuning Morse**: ritoccare soglie col touchlog quando l'utente è "meno arrugginito".

- 📌 **Velocità salvata in flash** (oggi torna a 7 a ogni riavvio) e **impostabile dalla band**
  (menu IMPOSTAZIONI > VELOCITA, con la barra di carica per confermare).
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
