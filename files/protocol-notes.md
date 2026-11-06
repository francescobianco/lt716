# FitPro 2.10.10 – Protocollo BLE UART (reverse engineering)

Fonte: solo il codice decompilato (jadx) di `cn.xiaofengkj.fitpro` 2.10.10, package `xfkj.fitpro`.
Nessun dispositivo è stato toccato. I punti marcati **[INCERTO]** sono dedotti/ipotizzati e vanno verificati sul bracciale.

File principali:

| File | Ruolo |
|---|---|
| `bluetooth/SendData.java` | costruzione di tutti i comandi |
| `bluetooth/revData/BaseReceiveData.java` | riassemblaggio + parsing di tutte le risposte, generazione ACK |
| `service/BaseLeService.java` | GATT callback reali, sequenza di init, scrittura (il `bluetooth/BluetoothLeService.java` è usato solo per l'OTA) |
| `bluetooth/CommandPool.java` | coda di scrittura, chunking a 20 byte |
| `service/NotifyService.java` (`sendNotifyPush`, decompilato con `--show-bad-code` + smali) | push notifiche |
| `service/CallService.java`, `service/SmsService.java` | chiamate / SMS |
| `utils/WeatherProxy.java`, `utils/LanguageUtils.java` | meteo, codici lingua |

Convenzioni: tutti i valori multi-byte sono **big endian** salvo dove indicato **LE** (alcuni campi usano `ByteUtil.shortToByte`, che è little endian).

---

## 0. GATT

| Servizio / caratteristica | UUID | Uso |
|---|---|---|
| UART service | `6e400001-b5a3-f393-e0a9-e50e24dcca9d` (da `res/values/strings.xml`, `ble_main_service_uuid`; `uuid2` = stesso) | |
| UART write (app→band) | `6e400002-b5a3-f393-e0a9-e50e24dcca9d` | tutti i comandi `CD …` e gli ACK `DC …` |
| UART notify (band→app) | `6e400003-b5a3-f393-e0a9-e50e24dcca9d` | CCCD 0x2902 ← `01 00`. L'app lo abilita solo se `properties == 0x10` (NOTIFY puro) |
| UART write "AliPay" | `6e400005-…` | solo AliPay, chunk da 100 byte (ignorabile) |
| Battery | `0000180f` / `00002a19` | read + notify (vedi §6) |
| Device Info | `0000180a`: `2a26` firmware rev (stringa), `2a28` "funzioni" (stringa di flag), `2a2a` nome (usato per capire la piattaforma OTA), `2a27` hardware rev (stringa) | |

---

## 1. Framing

### 1.1 Pacchetto comando / dati (entrambe le direzioni)

```
CD  LL LL  GG  01  KK  DL DL  <data…>
│   │      │   │   │   │
│   │      │   │   │   └ u16 BE lunghezza di <data>
│   │      │   │   └ key / comando
│   │      │   └ versione protocollo, sempre 0x01
│   │      └ gruppo (command id)
│   └ u16 BE = lunghezza totale - 3  (= 5 + len(data))
└ header 0xCD
```

Verificato in `SendData.getProtocol()`. Varianti:

* `getNoValueProtocol(g,k)` → `CD 00 05 GG 01 KK 00 00` (data vuota, 8 byte).
* `SwitchProtocol(g,k,v)` → `CD 00 06 GG 01 KK 00 01 vv`.
* Pacchetti "corti" a 5 byte senza key: `CD 00 02 GG 01` (bind 0x13, unbind 0x14, leggi allarmi 0x18, leggi tutte le impostazioni 0x19, reset 0x1D).

### 1.2 ACK

**ACK band → app** (risposta ai comandi): primo byte `0xDC`. L'app usa solo `[3]` = gruppo, `[4]` = key e l'**ultimo byte**: `01` = OK, altro = errore (in caso di errore l'app ripristina il valore precedente salvato con `setSendBeforeValue`). Formato tipico `DC 00 05 GG KK 00 xx 01` (come già noto).

**ACK app → band**: l'app risponde **a ogni pacchetto `CD` completo ricevuto, di qualunque gruppo**, immediatamente (fuori coda), con:

```
DC 00 05 GG 01 TT TT 01
            │  └ u16 BE = lunghezza TOTALE del pacchetto ricevuto (= LL LL + 3)
            └ gruppo del pacchetto ricevuto
```
(`SendData.getReturnAck(group, [len_hi, len_lo])`, chiamato in `BaseReceiveData.setOnData`.)
Esempio: ricevuto `CD 00 0E 15 01 0C 00 09 …` (17 byte) → l'app scrive `DC 00 05 15 01 00 11 01`.

Nota: l'ACK non contiene la key; è identificato solo da gruppo + lunghezza.

### 1.3 Riassemblaggio in ricezione (`BaseReceiveData.setOnData`)

Per ogni notifica su 6e400003:
1. se il primo byte è `DC` → è un ACK, gestito subito (buffer azzerato);
2. se il primo byte è `CD` **oppure la notifica è > 120 byte** → il buffer viene azzerato (inizio nuovo pacchetto);
3. la notifica viene accodata al buffer;
4. `expected = u16 BE buf[1..2]`; se `len(buf) - 3 < expected` → incompleto, attende (e mette in pausa la coda di invio: `Constant.isExecute = false`);
5. se `len(buf) - 3 == expected` e `buf[0] == CD` → pacchetto completo: invia ACK, dispatch su `(buf[3], buf[5])`, riabilita la coda dopo 800 ms. Se la lunghezza eccede, il pacchetto viene scartato.

Bug da tenere presente: un frammento di continuazione che inizi per caso con `DC` o `CD` rompe il riassemblaggio (l'app non lo gestisce).

### 1.4 Scrittura / frammentazione (`CommandPool` + `BaseLeService.commandPoolWrite`)

* `getMtu()` è hard-coded a **20**: ogni pacchetto viene tagliato in pezzi da 20 byte (`n = len/20 + 1` pezzi; se `len` è multiplo di 20 l'ultimo pezzo è vuoto e viene scartato). Nessun header di frammentazione: i pezzi sono semplicemente consecutivi, il bracciale riassembla con il campo lunghezza.
* Ogni pezzo è un comando separato in una coda FIFO; il thread della coda gira ogni 100 ms, scrive un pezzo e attende `onCharacteristicWrite` (write type default della caratteristica, cioè **write with response**) prima del successivo; timeout 5 s, max 10 tentativi.
* La coda è in pausa mentre si sta ricevendo un pacchetto multi-notifica e per 800 ms dopo ogni pacchetto completo; per 15 s dopo `15/07` (inizio storico) e fino a 1 s dopo `15/08` (fine storico).
* Il comando "imposta ora" viene ricostruito al momento dell'invio (ora corrente).
* Nessuna richiesta di MTU più grande. Per un CLI: scrivere a pezzi da 20 byte con risposta, uno alla volta, è il comportamento sicuro.

---

## 2. Gruppi (command id)

| Gruppo | Nome (Profile.java) | Direzione |
|---|---|---|
| `0x12` | Setting | app→band (ACK `DC`) |
| `0x13` | Binding | app→band `CD 00 02 13 01` |
| `0x14` | StrappedEquipment (unbind) | app→band `CD 00 02 14 01`; la risposta 0x14 fa azzerare i passi in app |
| `0x15` | Sport | entrambe |
| `0x18` | AlarmLists | app `CD 00 02 18 01` → band risponde gruppo 0x18 (key ≠ 0, probabilmente 0x10) |
| `0x19` | OtherSetInfo | app `CD 00 02 19 01` → band risponde gruppo 0x19 (probabilmente key 0x11) |
| `0x1A` | SetInfoByKey (get) | app `CD 00 05 1A 01 KK 00 00` → band `CD … 1A 01 KK DL DL data` |
| `0x1B` | TestMode | non usato |
| `0x1C` | DeviceControlApp | band→app (e app→band 0x1C/0x10 timestamp, 0x1C/0x0F sonno) |
| `0x1D` | DeviceReset | app→band `CD 00 02 1D 01` (**distruttivo**) |
| `0x1F`/`0x20` | quadranti (upload / read info) | |
| `0x22`/`0x23` | invio file | |
| `0x24` | voice chat | |

---

## 3. Notifiche (gruppo 0x12, key 0x11 / 0x12)

`SendData.getSendPushRemindValue(i, payload)` → key `0x12` se `i == 1` (messaggio), `0x11` se `i == 0` (chiamata).
Unico chiamante: `NotifyService.sendNotifyPush(pkg, text, callState)` (il metodo non è decompilato da jadx in modalità normale; ricostruito con `--show-bad-code` e verificato sullo smali).

### 3.1 Messaggi – `12 / 12`

```
data = TT 00 00 <testo UTF-8>
```
* `TT` = tipo app (tabella sotto), seguito da **due byte 0x00** (significato ignoto, sempre 0) **[INCERTO: forse riservati]**.
* Testo:
  * notifiche Android: `title + ":" + text + subText` (ognuno solo se non vuoto);
  * SMS (`SmsService`): `mittente(o nome contatto) + ":" + corpo`.
* Troncamento: `getSubString(text, 300)` — massimo 300 "unità", dove un carattere ASCII vale 1 e un carattere non-ASCII vale 2 (non è un limite in byte: 150 caratteri CJK = 450 byte UTF-8). Poi codifica **UTF-8**, nessun terminatore.
* Nessun chunking applicativo: un solo pacchetto `CD`, spezzato a 20 byte dal livello di scrittura (§1.4).
* Un pacchetto viene inviato solo se lo switch relativo è "1" (default "1") e se il pacchetto Android è riconosciuto; **le app non in tabella NON vengono inviate** (nessun tipo "generico/altro" in questa versione).
* Antirimbalzo di 50 ms per WhatsApp, Line, Skype.

| `TT` | App | package Android |
|---|---|---|
| `0x01` | SMS | (SmsService, `app.mms`) |
| `0x02` | QQ | com.tencent.mobileqq |
| `0x03` | WeChat | com.tencent.mm |
| `0x04` | Facebook | com.facebook.katana |
| `0x05` | Twitter / X | com.twitter.android |
| `0x06` | Skype | com.skype.raider |
| `0x07` | Line | jp.naver.line.android |
| `0x08` | WhatsApp (+ Business) | com.whatsapp, com.whatsapp.w4b |
| `0x09` | KakaoTalk | com.kakao.talk |
| `0x0A` | Snapchat | com.snapchat.android |
| `0x0B` | TikTok | com.zhiliaoapp.musically, com.ss.android.ugc.trill |
| `0x0C`–`0x0F` | non usati dall'app **[INCERTO: potrebbero esistere nel firmware]** | |
| `0x10` | Instagram | com.instagram.android |
| `0x11` | LinkedIn | com.linkedin.android |
| `0x12` | Telegram | org.telegram.messenger |
| `0x13` | OK.ru | ru.ok.android |
| `0x14` | VK | com.vkontakte.android |
| `0x15` | TenChat | ru.gostinder |
| `0x16` | Viber | com.viber.voip |
| `0x17` | Messenger | com.facebook.orca (solo se il bracciale riporta ≥ 20 switch push, altrimenti inviato come Facebook `0x04`) |
| `0x18` | Zalo | com.zing.zalo (solo se il bracciale riporta 21 switch push) |

Esempio WhatsApp "Mario:Ciao!":
```
CD 00 13 12 01 12 00 0E 08 00 00 4D 61 72 69 6F 3A 43 69 61 6F 21
```

### 3.2 Chiamate – `12 / 11`

```
data = SS 00 <nome o numero UTF-8>
```
| `SS` | Stato (da `TelephonyManager.CALL_STATE_*`) |
|---|---|
| `0x01` | squilla (chiamata in arrivo) |
| `0x02` | offhook (risposta / in corso) |
| `0x00` | idle → **fine chiamata / riagganciata** |

* Testo = nome contatto se trovato, altrimenti numero. Se il bracciale supporta "voice play" (flag 2A28 pos. 26) il testo è `nome + "/" + numero` (nome può essere vuoto → `/numero`).
* Inviato solo se il testo non è vuoto (quindi anche per la fine chiamata serve il numero).
* Stesso troncamento 300 / UTF-8. Lo switch `CALLState` viene ignorato se il bracciale dichiara "chiamata sempre attiva" (flag 2A28 pos. 10).

Esempi:
```
squilla   CD 00 12 12 01 11 00 0D 01 00 4D 61 72 69 6F 20 52 6F 73 73 69   ("Mario Rossi")
fine      CD 00 12 12 01 11 00 0D 00 00 4D 61 72 69 6F 20 52 6F 73 73 69
```
Il bracciale può poi chiedere all'app di rispondere (`1C/09`) o riagganciare (`1C/07`).

---

## 4. Impostazioni (gruppo 0x12)

### 4.1 Ora – `12 / 01`
u32 BE: `sec | min<<6 | hour<<12 | day<<17 | month<<22 | (year-2000)<<26` (ora locale). Verificato.
Es. 2026-11-06 14:30:15 → `CD 00 09 12 01 01 00 04 6A CC E7 8F`.

### 4.2 Allarmi – `12 / 02`

Ogni allarme = **5 byte** (valore a 40 bit BE), concatenati; l'app manda sempre **la lista completa** (max **8** allarmi, limite della UI). Lista vuota → `CD 00 05 12 01 02 00 00` (cancella tutti).

| bit (39 = MSB) | campo |
|---|---|
| 39–34 (6) | anno − 2000 (data di creazione) |
| 33–30 (4) | mese |
| 29–25 (5) | giorno |
| 24–20 (5) | ora |
| 19–14 (6) | minuto |
| 13–11 (3) | `num` – l'app mette sempre **1** **[INCERTO: probabilmente flag "abilitato" o contatore; mai letto in ricezione]** |
| 10–8 (3) | 0 |
| 7–0 (8) | giorni settimana |

Byte giorni (da `MyPopupWin`/`AlarmActivity`): `bit0 = Lun, bit1 = Mar, bit2 = Mer, bit3 = Gio, bit4 = Ven, bit5 = Sab, bit6 = Dom, bit7 = 0`. Default UI = Lun–Ven = `0x1F`. Con `0x00` presumibilmente allarme singolo alla data indicata **[INCERTO]**.

Esempio 07:30 Lun–Ven, creato 2026-11-06:
```
CD 00 0A 12 01 02 00 05 6A CC 77 88 1F
```

**Lettura**: app → `CD 00 02 18 01`. Risposta gruppo `0x18` (key ≠ 0): `datalen` = u16 BE byte 6–7, poi `datalen/5` record da 5 byte con lo stesso layout (la app legge anno, mese, giorno, ora, minuto, giorni; ignora i bit 13–8).

### 4.3 Obiettivo passi – `12 / 03`
u32 BE passi (default 5000). Es. 8000: `CD 00 09 12 01 03 00 04 00 00 1F 40`.

Altri obiettivi: `12 / 22` tempo attività: u16 minuti **LE** (`shortToByte`; in lettura key 17 è letto BE → **[INCERTO]** endianness reale); `12 / 23` ore in piedi: 1 byte.

### 4.4 Utente – `12 / 04`
u32 BE: `gender<<31 | age<<24 | height_cm<<15 | weight_kg<<5 | unit`

| campo | bit | valori |
|---|---|---|
| gender | 31 (1) | 1 = maschio (default), 0 = femmina |
| age | 30–24 (7) | anni (valido 6–127) |
| height | 23–15 (9) | cm (100–250, default 170) |
| weight | 14–5 (10) | kg (30–180, default 65) |
| unit | 4–0 (5) | 1 = km/metrico, 2 = miglia/imperiale (`DISTANCE_UNIT_ME`) **[INCERTO sul nome]** |

Es. M, 30 anni, 175 cm, 70 kg, km → `CD 00 09 12 01 04 00 04 9E 57 88 C1`.

### 4.5 Sedentarietà – `12 / 05` (8 byte)
| byte | campo |
|---|---|
| 0 | pausa pranzo (siesta) on/off — non avvisa durante la pausa |
| 1 | abilitato on/off |
| 2–3 | soglia, u16 BE, sempre 150 **[INCERTO: forse passi minimi]** |
| 4 | intervallo in unità da 15 min (UI: valore×15 min, default 4 = 60 min; range valido in lettura 1–10) |
| 5 | ora inizio (0–23, default 8) |
| 6 | ora fine (0–23, default 22) |
| 7 | ripetizione giorni: `binary("0"+"1111111")` = `0x7F` sempre (la UI non lo modifica) **[INCERTO ordine bit; probabilmente come allarmi]** |

Es. `CD 00 0D 12 01 05 00 08 00 01 00 96 04 08 16 7F`.

### 4.6 Mano – `12 / 06`
1 byte `HANDState` (switch "lr_hand_wearing": 0 = default/sinistra?, 1 = attivato) **[INCERTO quale mano]**.

### 4.7 Switch notifiche push – `12 / 07`
Un byte (0/1) per app, in quest'ordine:

| idx | app | idx | app |
|---|---|---|---|
| 0 | Chiamate | 11 | LinkedIn* |
| 1 | SMS | 12 | Snapchat* |
| 2 | WeChat | 13 | TikTok* |
| 3 | QQ | 14 | Telegram* |
| 4 | Facebook | 15 | OK.ru* |
| 5 | Twitter | 16 | VK* |
| 6 | Skype | 17 | TenChat* |
| 7 | Line | 18 | Viber* |
| 8 | WhatsApp | 19 | Messenger* |
| 9 | KakaoTalk | 20 | Zalo* |
| 10 | Instagram | | |

\* inviati solo se il bracciale, leggendo la key 4 (§5), ha riportato almeno quel numero di byte: l'app invia **tanti byte quanti ne ha ricevuti** (11 minimo, 21 massimo). Per un CLI: leggere prima `1A/04`, poi riscrivere lo stesso numero di byte.

### 4.8 Promemoria dispositivo – `12 / 08` (4 byte)
`[vibrazione(SHOCK), alza-per-accendere(BRIGHT), monitor sonno(SLEEP), HR automatico(HEART)]`, ciascuno 0/1.

### 4.9 Accensione al polso – `12 / 09` (5 byte)
`[on/off, inizio u16 BE minuti dalla mezzanotte, fine u16 BE minuti]` (UI in ore intere ×60; default 8:00–22:00).
Es. on 8–22: `CD 00 0A 12 01 09 00 05 01 01 E0 05 28`.

(Il codice usa un trucco con `algorismToHEXString`: risultato sempre 2 byte BE.)

### 4.10 Pair – `12 / 0A`
`CD 00 06 12 01 0A 00 01 02` — primo comando dopo il connect ("android 蓝牙不配对" = Android senza pairing classico). Valore 2 fisso.

### 4.11 Trova bracciale – `12 / 0B`
`CD 00 06 12 01 0B 00 01 01` (l'app manda solo `01`).

### 4.12 Modalità fotocamera – `12 / 0C`
`… 0C 00 01 01` entra, `… 00 01 00` esce. Il bracciale poi manda `1C/02` (scatta) e `1C/04` (uscita).

### 4.13 Altri switch on/off a 1 byte
| key | significato |
|---|---|
| `0D` | HR "sport" receive on/off (non chiamato) |
| `0E` | BP receive on/off (non chiamato) |
| `10` | dati di test on/off |
| `18` | misura "tre-in-uno" (vecchio, `MeasureActivity`) on/off |
| `1A` | ECG on/off |
| `19` | entra in OTA (`CD 00 05 12 01 19 00 00`) — **non usare** |

### 4.14 Sonno – `12 / 0F`, Non disturbare – `12 / 14`
Stesso layout di `12/09`: `[on/off, inizio u16 BE min, fine u16 BE min]`. Default sonno 6–22 (sic), DND 22–8.

### 4.15 Lingua – `12 / 15`
`CD 00 06 12 01 15 00 01 LL`. Tabella completa (`LanguageUtils.getLanguageCode` + `BaseLeService`):

| LL | lingua (codice Android) | LL | lingua |
|---|---|---|---|
| 0 | cinese semplificato (zh, zh-cn, Locale.CHINA) | 21 | ucraino (uk) |
| 1 | inglese (en; **default** per lingue non mappate) | 22 | hindi (hi) |
| 2 | cinese tradizionale (zh-tw, zh-hk, Locale.TAIWAN) | 23 | finlandese (fi) |
| 3 | arabo (ar) | 24 | croato (hr) |
| 4 | ceco (cs) | 25 | norvegese (nb) |
| 5 | tedesco (de) | 26 | danese (da) |
| 6 | spagnolo (es) | 27 | svedese (sv) |
| 7 | francese (fr) | 28 | coreano (ko) |
| 8 | giapponese (ja) | 29 | ungherese (hu) |
| 9 | malese (ms) | 30 | greco (el) |
| 10 | olandese (nl) | 31 | persiano (fa) |
| 11 | polacco (pl) | 32 | rumeno (ro) |
| 12 | portoghese (pt) | 33 | bulgaro (bg) |
| 13 | russo (ru) | 34 | sloveno (sl) |
| 14 | slovacco (sk) | 35 | "liv" (livone?) **[INCERTO]** |
| 15 | thai (th) | 36 | estone (et) |
| 16 | turco (tr) | 37 | lituano (lt) |
| 17 | vietnamita (vi) | 38 | serbo (sr) |
| **18** | **italiano (it)** | 39 | lettone (lv) |
| 19 | filippino (tl, fil) | 40 | ebraico (iw) |
| 20 | indonesiano (id, in) | | |

Italiano: `CD 00 06 12 01 15 00 01 12`.

### 4.16 HR automatico – `12 / 16` (8 byte)
`[on/off, sleep-assist on/off, intervallo u16 BE minuti, inizio u16 BE min, fine u16 BE min]`. Opzioni UI intervallo: 2, 60, 90, 120 min (default 2). Es. on, ogni 2 min, 6–22: `CD 00 0D 12 01 16 00 08 01 00 00 02 01 68 05 28`.

### 4.17 Misura on-demand – `12 / 24` (2 byte)
`[tipo, on/off]`: tipo `00` = HR, `01` = pressione, `02` = SpO2. Es. avvia HR: `CD 00 07 12 01 24 00 02 00 01`; stop: `… 00 00`. Risultati in §7.4. La UI annulla dopo 60 s. Il bracciale può segnalare uscita dalla misura con `1C/05`/`1C/08` (HR), `1C/06` (BP), `1C/0D` (SpO2).

### 4.18 Unità temperatura – `12 / 21`
1 byte: `0` = °C, `1` = °F. Lettura con key 15 (§5).

### 4.19 Meteo

Quale versione: carattere 13 della stringa 2A28 (`show_weather`): `0` = nessun meteo, `1` = v1, `2` = v2.

**v1 – `12 / 20`** (4 byte): `[t_min (int8), t_max (int8), condizione, unità]`. Temperature già convertite in °F se unità = 1.
Es. −3/12 °C sereno: `CD 00 09 12 01 20 00 04 FD 0C 01 00`.

**v2 – `12 / 28`**:
```
LL <località UTF-8, LL byte> N  { giorno } × N
giorno (20 byte):
  0-3   data: u32 BE del numero decimale yyyyMMdd (es. 20261106 = 0x013528F2)
  4     giorno della settimana (Calendar: 1=Dom … 7=Sab)
  5     t_min (int8)
  6     t_max (int8)
  7     condizione giorno
  8     condizione notte
  9     unità temp (0 °C / 1 °F)
  10    indice UV
  11    umidità %
  12    visibilità (km, troncata a byte)
  13    vento giorno (velocità, byte)
  14    vento notte
  15    precipitazioni (float troncato a byte)
  16-17 alba,  minuti dalla mezzanotte, u16 **LE**
  18-19 tramonto, u16 **LE**
```

Codici condizione (mappa `getWeatherStatusByCode` dai codici QWeather/HeFeng; i nomi sono la mia interpretazione dei codici QWeather **[INCERTO]**):

| cod | QWeather | significato | cod | QWeather | significato |
|---|---|---|---|---|---|
| 1 | 100 | sereno | 11 | 404–406 | nevischio / pioggia+neve |
| 2 | 104 | coperto | 12 | 500–502, 509–515 | nebbia / foschia |
| 3 | 101–103 (e default) | nuvoloso | 13 | 304, 313 | grandine / pioggia gelata |
| 4 | 305, 309 | pioggia debole | 14 | 503, 504, 507, 508 | sabbia / polvere |
| 5 | 306, 314, 399 | pioggia moderata | 15 | 200–204 | vento debole |
| 6 | 307,308,310–312,315–318 | pioggia forte / nubifragio | 16 | 205–208 | vento forte |
| 7 | 300–303 | rovesci / temporale | 17 | 209–211 | tempesta |
| 8 | 400, 407 | neve debole | 18 | 212 | tornado |
| 9 | 401, 408, 499 | neve moderata | 19 | 231 | tempesta tropicale |
| 10 | 402, 403, 409, 410 | neve forte | | | |

### 4.20 `12 / FE` e `12 / FF`
* `12 / FF` `[01]` = tipo telefono (1 = Android), inviato all'init: `CD 00 06 12 01 FF 00 01 01`.
* `12 / FE` `[01]` = "SettingHeartRate": builder presente ma **mai chiamato** **[INCERTO]**.
* `12 / FD` = shutdown (solo costante, nessun builder). `12 / F9` = "sleep begin" u16 **LE** (solo debug).

### 4.21 Altro (solo citati)
`12/13` sync contatti, `12/17` cancella contatti, `12/25` contatti SOS, `12/26`/`27` pagamenti.

### 4.22 Bind / unbind / reset
* bind: `CD 00 02 13 01` (inviato dopo il pair solo al primo collegamento a un nuovo MAC)
* unbind: `CD 00 02 14 01` → il bracciale risponde nel gruppo 0x14 (l'app azzera i passi)
* **reset di fabbrica: `CD 00 02 1D 01`** → risposta gruppo 0x1D. Distruttivo, non usare nei test.

---

## 5. Lettura impostazioni – gruppo 0x1A (get by key)

Richiesta: `CD 00 05 1A 01 KK 00 00`. Risposta: `CD LL LL 1A 01 KK DL DL <data>` (data da byte 8).

| KK | contenuto `data` | parser |
|---|---|---|
| 1 | utente: u32 come `12/04` (gender bit31, age 7, height 9, weight 10, unit 5) | saveUinfo |
| 2 | obiettivo passi u32 BE (accettato 1000–150000) | saveSteps |
| 3 | sedentarietà, 8 byte come `12/05` | saveLongSit |
| 4 | switch push, 11–21 byte 0/1 nell'ordine di §4.7 (la lunghezza dice quante app supporta) | savePushSwitches |
| 5 | sonno: `[on, start u16 min, end u16 min]` | saveSleepSwitch |
| 6 | 4 byte: `[mano, stato pairing, tipo lingua(?), vibrazione]` (accettato solo se tutti 0/1) | saveSwitchSet |
| 7 | alza-per-accendere `[on, start u16, end u16]` | saveBrightScreen |
| 8 | HR auto `[on, assist, freq u16, start u16, end u16]` | saveHeartAuto |
| 9 | DND `[on, start u16, end u16]` | saveDisturbSwitch |
| 10 | MAC Bluetooth classico, 6 byte → `AA:BB:CC:DD:EE:FF` (poi l'app tenta il bond BR/EDR) | |
| 11 | "测试指令" (test/debug), solo log | |
| 12 | codice nome BT classico: le cifre esadecimali lette come numero decimale (`Integer.valueOf(hex)`) | |
| 13 | contatti: `[n0, n1]` (n. contatti, max?) **[INCERTO]** | |
| 14 | info OTA (usato da UpdateOtaActivity) – raw da byte 7 | |
| 15 | unità temperatura (hex letto come decimale: `00`/`01`) | saveTempUnite |
| **16** | **info hardware**: `L1 <LED/schermo ASCII> L2 <G-sensor ASCII> L3 <sensore HR ASCII>` (tre stringhe con prefisso di lunghezza). **Non contiene firmware né modello.** | parseHardInfo |
| 17 | obiettivo tempo attività u16 BE | parseTargetSportTime |
| 18 | obiettivo ore in piedi, 1 byte | parseTargetStand |
| 19 | nome BT classico: `[b0 ?] <UTF-8 nome>` (byte 0 ignorato, forse lunghezza) | |
| 20 | info prodotto: `L1 <productInfo> L2 <productType>` (stringhe) | parseProductPicInfo |

Firmware / modello: vanno letti via GATT standard — `2A26` = versione firmware (stringa ASCII), `2A27` = hardware rev, `2A2A` = nome; `2A28` = stringa di flag di funzionalità (vedi §8.1).

### 5.1 Gruppo 0x19 – OtherSetInfo (tutte le impostazioni in un colpo)

Richiesta `CD 00 02 19 01`. Risposta ≥ 40 byte. `data` (da byte 8) contiene sezioni a lunghezza fissa, ognuna preceduta da 1 byte (presumibilmente il numero di key 1…9 **[INCERTO: l'app salta quel byte senza controllarlo]**):

| offset in data | len | sezione (come key) |
|---|---|---|
| 0 | 1 | (01) |
| 1 | 4 | utente (key 1) |
| 5 | 1 | (02) |
| 6 | 4 | passi (key 2) |
| 10 | 1 | (03) |
| 11 | 8 | sedentarietà (key 3) |
| 19 | 1 | (04) |
| 20 | 10 | switch push (solo i primi 10!) |
| 30 | 1 | (05) |
| 31 | 5 | sonno |
| 36 | 1 | (06) |
| 37 | 4 | switch set (key 6) |
| 41 | 1 | (07) |
| 42 | 5 | alza-per-accendere |
| 47 | 1 | (08) |
| 48 | 8 | HR auto |
| 56 | 1 | (09) |
| 57 | 5 | DND |

---

## 6. Batteria

**Nessun comando UART**: si usa il Battery Service standard.
* read di `0x2A19` (servizio `0x180F`) durante l'init; il valore (1 byte, %) è l'intero del byte;
* poi l'app abilita le notifiche su `0x2A19` (CCCD `01 00`) e aggiorna il livello a ogni notifica.

---

## 7. Gruppo 0x15 – Sport / dati

### 7.1 Comandi app → band
| comando | byte | note |
|---|---|---|
| real-time on/off `15/06` | `CD 00 06 15 01 06 00 01 01` / `…00` | inviato all'init (on se app in foreground) e ad ogni passaggio foreground/background |
| riepilogo giorno `15/0D` | `CD 00 06 15 01 0D 00 01 01` | all'init e ad ogni pull-to-refresh della home; risposta `15/0C` |
| storico `15/01` | `CD 00 06 15 01 01 00 01 01` | builder `getSportKeyGet` presente ma **mai chiamato** in 2.10.10. Lo storico sembra inviato spontaneamente dal bracciale (dopo connessione/ora) **[INCERTO quale sia il trigger]** |

### 7.2 Header comune delle risposte 0x15

```
CD LL LL 15 01 KK DL DL  D1 D0  XX  NN  <NN record>
                         └date┘ │   └ numero di record
                                └ ignoto [INCERTO]
```
Data (u16 BE, byte 8–9): `bit15 = 0/ignoto, bit14–9 = anno−2000, bit8–5 = mese, bit4–0 = giorno`. I record sono relativi a quel giorno.
Per **ogni** pacchetto completo l'app manda l'ACK `DC 00 05 15 01 <len> 01` (§1.2).

### 7.3 Key in ricezione

| KK | contenuto |
|---|---|
| `07` | inizio storico (l'app sospende la coda per 15 s) |
| `08` | fine storico |
| `02` | **passi storici**: NN × 8 byte (layout sport sotto) |
| `03` | **sonno**: NN × 4 byte |
| `04` | **HR**: NN × 5 byte |
| `05` | **pressione**: NN × 6 byte |
| `0B` | **real-time** (con `15/06` attivo): NN × 8 byte, stesso layout di `02` |
| `0C` | **riepilogo giornaliero** (risposta a `15/0D`) |
| `0E` | misura combinata HR+BP+SpO2: NN × 8 byte |
| `11` | temperatura |
| `12`/`13` | ECG inizio / fine (1 byte di stato / HR) |
| `14` | **SpO2**: NN × 5 byte |

**Record sport (8 byte = 64 bit, MSB first)** – usato da `02` e `0B`:

| bit (dal MSB) | len | campo |
|---|---|---|
| 0–11 | 12 | passi |
| 12–15 | 4 | minuti attivi(?) ("min") **[INCERTO]** |
| 16–31 | 16 | distanza (m; l'app mostra /1000 = km) |
| 32–42 | 11 | offset in slot da 15 min dalla mezzanotte (ora = offset·15 min) |
| 43–44 | 2 | modalità (probabilmente 1 cammino, 2 corsa, 3 bici **[INCERTO]**) |
| 45–63 | 19 | calorie (unità ignota, probabilmente cal o kcal **[INCERTO]**) |

Per `0B` (real-time) l'app salva questi valori come "passi/distanza/calorie di oggi". Con soli 12 bit per i passi il record real-time non può superare 4095 → **[INCERTO]**: per i totali giornalieri affidabili usare `15/0D` → `15/0C`.

**Riepilogo `0C`**: dopo la data (byte 8–9), da byte 10:
```
10-13  passi totali   u32 BE (l'app limita a 100000)
14-17  distanza       u32 BE metri
18-19  calorie        u16 BE
```
Esempio teorico 5432 passi, 3900 m, 210: `CD 00 11 15 01 0C 00 0C D1 D0 00 00 15 38 00 00 0F 3C 00 D2`.

**Sonno (4 byte)**: `[minuti dalla mezzanotte u16 BE][ignorato][tipo]`, tipo: 1 = leggero, 2 = profondo, 3 = sveglio, 4 = REM. Ogni record indica l'inizio di una fase; la durata è fino al record successivo.
(Gestito solo se il flag 2A28 `show_sleep` vale "0".)

**HR (5 byte)**: `[secondi dalla mezzanotte u32 BE][bpm]`.

**Pressione (6 byte)**: `[secondi u32 BE][v1][v2]`. L'app salva v1 come "LBlood" e v2 come "HBlood", ma il log li etichetta v1 = alta, v2 = bassa → **[INCERTO quale sia sistolica]** (probabilmente v1 = sistolica).

**Combinata `0E` (8 byte)**: `[secondi u32 BE][SpO2][BP bassa][BP alta][HR]`.

**SpO2 `14` (5 byte)**: `[secondi u32 BE][SpO2 %]`.

**Temperatura `11`**: data da byte 8: `YY MM DD hh mm ss` (6 byte binari, anno = 2000+YY), poi u32 BE temperatura ×10 (°C).

### 7.4 Misura on-demand: flusso
1. app `12/24 [tipo,01]` → ACK `DC …` dal bracciale (se errore: misura annullata);
2. il bracciale invia i risultati come `15/04` (HR), `15/05` (BP), `15/14` (SpO2) oppure `15/0E` (modelli "tre-in-uno");
3. app `12/24 [tipo,00]` per fermare; il bracciale può mandare `1C/05`/`08`/`06`/`0D`.

### 7.5 Quali pacchetti vanno ACKati
Tutti i pacchetti `CD` completi ricevuti (gruppi 0x15, 0x18, 0x19, 0x1A, 0x1C, …) ricevono `DC 00 05 GG 01 <lunghezza totale> 01`, subito dopo il riassemblaggio e prima del parsing. I pacchetti `DC` del bracciale **non** vengono ACKati. Non c'è nessun ACK "speciale" per lo storico: si suppone che il bracciale attenda l'ACK prima di mandare il pacchetto successivo **[INCERTO ma consistente con il design]**.

---

## 8. Gruppo 0x1C – controllo dal bracciale

| key | azione |
|---|---|
| 01 | trova telefono |
| 02 | scatta foto |
| 03 | apri fotocamera |
| 04 | esci fotocamera |
| 05 / 08 | esci misura HR |
| 06 | esci misura BP |
| 07 | riaggancia |
| 09 | rispondi |
| 0A / 0B / 0C | musica precedente / play-pausa / successiva |
| 0D | esci misura SpO2 |
| 0E | stop trova telefono |
| 0F | richiede ultimo risultato sonno (l'app risponde `1C/0F`, metodo non decompilato) |
| 10 | richiede timestamp → l'app risponde `1C/10`: `[unix time u32 BE][offset fuso raw in secondi i32 BE]` (senza ora legale) |
| 11 | AliPay |
| 12 | richiesta disconnessione BLE |

Esempio risposta timestamp: `CD 00 0D 1C 01 10 00 08 <t3 t2 t1 t0> <o3 o2 o1 o0>` (Italia: offset 3600 = `00 00 0E 10`).

---

## 9. Sequenza di connessione / init (BaseLeService)

1. `connectGatt(autoConnect=false, TRANSPORT_LE)`; quando connesso, attesa 800 ms, `discoverServices()`.
2. `onServicesDiscovered` – accoda (nell'ordine in cui il GATT elenca servizi/caratteristiche):
   * su 6e400002: **write `12/0A` pair** `CD 00 06 12 01 0A 00 01 02`; se è un bracciale nuovo, **bind** `CD 00 02 13 01`;
   * su 6e400003: abilita notifiche (CCCD `01 00`);
   * Device Info: read `2A26` (firmware), `2A28` (flag funzioni), `2A2A` (nome), `2A27` (hardware);
   * Battery: read `2A19`.
3. Alla lettura della batteria: abilita notify su `2A19`; write `15/06 [01]` (real-time on, se in foreground).
4. Alla lettura del firmware `2A26` (dopo eventuali whitelist):
   1. `12/01` ora (inviato **due volte**)
   2. `1A/0C` codice nome BT classico
   3. `12/15` lingua
   4. `12/FF [01]` tipo telefono Android
   5. `1A/0F` unità temperatura
   6. `1A/10` info hardware
   7. `1A/11` obiettivo tempo attività
   8. `1A/14` info prodotto
   9. `12/04` dati utente
   10. `12/03` obiettivo passi
   11. `15/0D [01]` riepilogo giorno
   12. `1A/0A` MAC BT classico
5. Alla lettura di `2A28`: se meteo supportato → sync meteo (`12/20` o `12/28`); se quadranti supportati → `20/02` info quadrante.
6. Ogni volta che si apre una pagina di impostazioni l'app rilegge la key relativa (`1A/xx`) e poi scrive.

### 9.1 Stringa funzioni `2A28` (ASCII, 1 carattere per flag)
| pos | chiave | pos | chiave |
|---|---|---|---|
| 0 | show_ota | 16 | show_adv |
| 1 | show_battery | 17 | show_remote_camera |
| 2 | show_heart | 18 | show_find_device |
| 3 | show_blood | 19 | show_hr_el |
| 4 | show_spo | 20 | show_long_duration_time |
| 5 | show_sleep (`0` = sonno supportato!) | 21 | show_disturn_mode |
| 6 | (non letto) | 22 | show_shake_mode |
| 7 | show_distance | 23 | show_contract_sos |
| 8 | show_wxsport | 24 | show_light_hand |
| 9 | show_voice | 25 | show_pay |
| 10 | default_open_call | 26 | show_voice_play (nome/numero nelle chiamate) |
| 11 | show_syncontract | 27 | bind_ble_hid |
| 12 | show_temp | 28 | show_he_suan |
| 13 | show_weather (0/1/2 = versione meteo) | 29 | support_alipay |
| 14 | show_clock_dial | 30–32 | function_config1..3 |
| 15 | show_more_notifi | | |

Semantica esatta di molti flag (0 = mostra o nascondi?) **[INCERTO]**: per `show_sleep` il supporto corrisponde a `'0'`, per `show_weather` a `>0`.

---

## 10. Riepilogo per un CLI Linux

1. Connettersi, abilitare notify su 6e400003 (e opzionalmente 2A19).
2. Inviare `CD 00 06 12 01 0A 00 01 02`, poi l'ora `12/01`.
3. Scrivere sempre a pezzi da 20 byte con risposta, un comando alla volta, attendendo l'ACK `DC` del bracciale (ultimo byte `01` = ok).
4. Riassemblare le notifiche con il campo lunghezza (byte 1–2 + 3) e rispondere ad ogni pacchetto `CD` completo con `DC 00 05 GG 01 <len_tot u16> 01`.
5. Notifica WhatsApp: `12/12` con `08 00 00` + UTF-8 (≤ 300 unità); chiamata: `12/11` con `01 00` + nome, fine con `00 00` + nome.
