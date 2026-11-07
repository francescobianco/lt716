# Incidente MorseOS 0.5.5 — band bloccata (2026-11-07)

## In breve

Dopo l'OTA della 0.5.5 la band si è avviata, ha disegnato il quadrante e si è fermata: schermo
acceso e immobile, tasto senza effetto, nessun segnale Bluetooth. Non si può recuperare senza
aprirla (SWire). La 0.5.4, installata subito prima, funzionava.

## Cronologia

1. 0.5.4: risparmio energetico, la CPU va in sospensione a schermo spento.
2. Misura con `lt716 dumper battery`: l'orologio della band non avanzava durante la sospensione
   (il contatore di sistema a 16 MHz si ferma mentre la CPU dorme).
3. 0.5.5: l'orologio passa al timer a 32 kHz letto con `pm_get_32k_tick()` della libreria Telink,
   chiamata a ogni giro del ciclo principale.
4. OTA della 0.5.5 riuscita (CRC verificato dall'aggiornatore), riavvio.
5. Nessun annuncio Bluetooth, né di MorseOS né dell'aggiornatore; scansioni ripetute (fino a 60 s,
   con pacchetti duplicati) trovano solo dispositivi lontani o di terzi.

## Causa

`pm_get_32k_tick()` (in `liblt_5316.a`, oggetto `pm_5316.o`), disassemblata dopo l'incidente:

```
scrive 0x20 in 0x80074a
imposta il bit 0x20 di 0x80074b
attende in un ciclo finché l'hardware non segnala il bit 0x20 di 0x80074b
legge 0x800744
```

È una funzione interna della gestione energetica, pensata per essere usata dalla libreria quando
il timer a 32 kHz è già stato preparato. Chiamata dal ciclo principale al primo giro, l'attesa non
termina mai.

## Perché non c'era via d'uscita

- Il ciclo principale contiene tutto: radio (`blt_sdk_main_loop`), lettura del tasto, comando
  Bluetooth `05 UPDT` e uscita di emergenza col tasto. Bloccato lui, sono bloccate tutte.
- Nessun watchdog attivo: nessun riavvio automatico.
- La ROM avvia sempre l'immagine valida in `0x0`: a ogni riaccensione (anche dopo batteria
  scarica) riparte la stessa 0.5.5 e si blocca di nuovo.
- Resta solo l'SWire (pad SWS sulla scheda), cioè aprire la band.

## Errori di metodo

1. Usata una funzione della libreria **senza leggerne prima il codice**.
2. Il codice nuovo girava **subito all'avvio**, prima che le vie d'uscita fossero disponibili.
3. Il firmware **non aveva una rete di sicurezza** contro i blocchi (watchdog).
4. La modifica sembrava piccola ed è stata flashata **senza un passo di prova separato**.

## Regole da qui in avanti

1. **Funzioni di libreria**: prima di usare una funzione di `liblt_5316.a` che non sia già usata
   dagli esempi dell'SDK nello stesso contesto, disassemblarla e controllare che non contenga attese
   senza limite. Le funzioni della gestione energetica (`pm_*`, `cpu_sleep_*`) non si chiamano dal
   ciclo principale.
2. **Watchdog obbligatorio**: ogni firmware attiva il watchdog hardware e lo azzera solo nel ciclo
   principale. Un contatore che sopravvive al riavvio conta i riavvii del watchdog: dopo 3 di fila
   il firmware va da solo all'aggiornatore del produttore (come `05 UPDT`). Il contatore si azzera
   dopo un periodo di funzionamento sano. Da implementare e provare per primo, da solo.
3. **Codice nuovo non all'avvio**: una funzionalità nuova e rischiosa parte disattivata e si attiva
   con un comando Bluetooth dopo l'avvio; solo quando ha funzionato diventa automatica.
4. **Un cambiamento rischioso per volta**: niente flash che mescolano una novità rischiosa con altre
   modifiche.
5. **Prima di ogni flash**: elenco di cosa cambia, cosa può bloccare l'avvio, e verifica che le due
   vie d'uscita (`05 UPDT`, tasto) restino raggiungibili.

## Stato dei sorgenti

- `lib/rtc/rtc.c` è stato riportato al conteggio sul contatore di sistema della 0.5.4 (nessuna
  chiamata a `pm_get_32k_tick()`). La deriva dell'orologio in sospensione resta un problema aperto,
  da risolvere in altro modo (per esempio risincronizzando l'ora a ogni connessione).
- Le versioni 0.5.2–0.5.5 non erano ancora in git al momento dell'incidente (ultimo commit: 0.5.1,
  `b08ee96`). La 0.5.4 differisce dalla 0.5.5 solo per l'orologio e per l'aggiunta di PC7 e dello
  sblocco del bus nella ricerca dei sensori.

## La band bloccata

Recuperabile via SWire (Blue Pill con USB2Swire, procedura in
`firmware/vendor/FitPro-LT715-TLSR8232/docs/flashing.md`). Backup completo della sua flash in
`files/firmware/LT716_flash.bin` e successivi.

## Il doppione in arrivo

Prima di toccarlo: rileggere questo file, implementare e provare il watchdog con il contatore di
riavvii, e solo dopo il resto. Da ricordare che il doppione arriverà col firmware originale, che
non abbiamo: conviene leggerne prima tutta la flash.
