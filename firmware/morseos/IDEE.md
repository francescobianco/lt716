# MorseOS — idee e roadmap

Raccolta delle idee man mano che emergono. Stato: ✅ fatto, 🔨 in corso, 📌 in coda, 💤 più avanti.

## Paradigmi di UI (da riusare)

- **Tieni premuto per confermare, con barra di carica sul bersaglio** (0.5.1): mentre il tasto è
  premuto, lungo il bordo basso dell'elemento a fuoco si riempie una barra verde; piena = azione
  eseguita subito (con una vibrazione breve), rilascio prima = barra azzerata e nessuna azione.
  Solo un tocco davvero breve (< 0,3 s) fa l'azione "leggera" (es. passare al box successivo).
  Dà il senso che la pressione sta agendo proprio su quel bersaglio. Da usare per ogni
  conferma: aprire una voce, giocare una mossa a scacchi, disconnettere il Bluetooth, cancellare.
- **Il tocco che sveglia lo schermo non conta**: mostra solo il quadrante.
- **Uscita da uno strumento**: tenere premuto 3 s, con vibrazione.
- **Emergenza**: 10 s avviso rosso "RILASCIA PER ANNULLARE", 20 s azione.

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

## In corso

- 🔨 nessuna: prossima è la 0.6 (scacchi).

## In coda

- 📌 **GIOCHI > SCACCHI** (0.6): scacchiera 80×80, caselle 10 px, sprite 8×8 dei pezzi, motore
  alfa-beta (0x88, 3–4 semimosse + catture), mosse in Morse: casella d'arrivo (`e4`), se ambigua
  chiede la partenza; lettere inglesi dei pezzi (`Nf3`). Libreria `chess_engine` separata.
- 📌 **App BLUETOOTH** (0.7): stato del link (indirizzo, segnale, durata), dispositivi in zona
  (scansione in advertising e in connessione), disconnetti, "connetti a…" come periferica:
  white list + advertising diretto verso il dispositivo scelto (la libreria non ha il ruolo
  master). Libreria `ble_scan`.
- 📌 **Accelerometro**: scansione I2C (sola lettura) su PA3/PC4, interrupt su PA4; identificare il
  chip (SC7A20, STK8321, DA213, QMA7981…), poi X/Y/Z a schermo, **contapassi**, **alza il polso
  per accendere**, sonno.
- 📌 **Tuning Morse**: ritoccare soglie col touchlog quando l'utente è "meno arrugginito".

## Più avanti

- 💤 **STRUMENTI > TASTIERA**: Morse → tastiera Bluetooth HID verso PC/telefono. Codici speciali
  già nel decodificatore: `..--` spazio, `.-.-` invio, `----` cancella. Abbinamento avviato
  dall'host, chiavi nel settore libero `0x74000`; dopo il primo abbinamento dump di verifica.
- 💤 **Dispositivi abbinati** nell'app Bluetooth (arrivano con l'abbinamento della tastiera).
- 💤 **Comandi Morse diretti dal quadrante** (es. T ora, S cronometro, F trova telefono, N ultima
  notifica).
- 💤 **Sensore del battito**: la band dichiara "heart=1" ma il pin non è ancora noto.
