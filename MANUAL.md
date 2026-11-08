# Manuale di build e flashing

Guida operativa per **Lysis Epub Reader V1.0 Beta** su M5Stack Paper S3.

## 1. Prerequisiti

- **M5Stack Paper S3**
- Cavo USB **dati** (non solo alimentazione)
- microSD formattata **FAT32**
- Python 3.10 o successivo
- PlatformIO Core **oppure** VS Code con estensione PlatformIO
- `esptool.py` incluso nell'ambiente PlatformIO; in alternativa installabile con `pip install esptool`

Verifica gli strumenti:

```bash
python --version
pio --version
```

## 2. Ottenere il codice

```bash
git clone <url-della-repository> LysisEpubReader
cd LysisEpubReader
```

La repository è già completa: `src/`, `lib/`, `boards/`, `tools/`, `platformio.ini` e `partitions.csv`.

## 3. Preparare la microSD

1. Formatta la microSD in **FAT32**.
2. Crea la cartella `books/` nella radice.
3. Copia dentro i file `.epub`.
4. Espelli la microSD in modo sicuro e inseriscila nel Paper S3.

Struttura consigliata:

```text
/
└── books/
    ├── libro-1.epub
    ├── libro-2.epub
    └── ...
```

La cartella `.state/` viene creata automaticamente dal firmware per stato, segnalibri e impostazioni.

## 4. Compilare il firmware

Dalla radice della repository:

```bash
pio run -e m5stack-papers3
```

Output principali:

- `.pio/build/m5stack-papers3/firmware.bin` — applicazione
- `lysis-reader-m5stack-papers3.bin` — immagine merged da 8 MB, pronta per il flash completo

Se vuoi pulire la build:

```bash
pio run -e m5stack-papers3 -t clean
pio run -e m5stack-papers3
```

## 5. Collegare la board

1. Collega il Paper S3 al computer con il cavo USB dati.
2. Apri il gestore porte:
   - Windows: Gestione dispositivi → Porte (COM e LPT)
   - Linux: `ls /dev/ttyACM*` oppure `ls /dev/ttyUSB*`
   - macOS: `ls /dev/cu.usbmodem*`
3. Annota la porta assegnata.

Esempio Windows: `COM3`.

## 6. Flash con PlatformIO

Sostituisci `COM3` con la tua porta:

```bash
pio run -e m5stack-papers3 -t upload --upload-port COM3
```

Il comando compila e scrive il firmware. Attendi il messaggio di successo e il riavvio della board.

Se la porta è occupata:

```bash
pio device monitor --port COM3
```

Chiudi il monitor con `Ctrl+C` e ripeti l'upload.

## 7. Flash dell'immagine merged

L'immagine merged contiene bootloader, tabella partizioni e applicazione:

```bash
esptool.py --chip esp32s3 -p COM3 -b 921600 write_flash 0x0 firmware/lysis-reader-m5stack-papers3.bin
```

> Il comando cancella l'intero contenuto della memoria flash del microcontrollore. Non è necessario per i normali aggiornamenti via PlatformIO, ma è utile per un ripristino completo.

## 8. Primo avvio

All'accensione appare la schermata di avvio. Dopo pochi secondi si apre la libreria.

- Tocca un libro per aprirlo.
- Tocca il lato sinistro o destro della pagina per cambiare pagina.
- Scorri verso l'alto nella lettura per aprire il menu.
- Scorri verso il basso per tornare alla libreria.
- Tocca la barra superiore per capitoli, Go To pagina o segnalibro.

## 9. Modalità USB Disk

1. Dalla libreria scorri verso l'alto per aprire il menu.
2. Seleziona **USB disk**.
3. Tocca lo schermo per avviare la modalità disco.
4. Copia i libri nella microSD dal computer.
5. Espelli il volume in modo sicuro.
6. Tocca lo schermo per uscire e riavviare la board.

Non scollegare il cavo durante la scrittura dei file.

## 10. Ricerca e navigazione

- La ricerca usa parole intere: la parola cercata non viene trovata dentro parole composte.
- Dai risultati puoi aprire la pagina trovata oppure tornare alla pagina da cui hai iniziato la ricerca con **Back**.
- **Go To pagina** permette di saltare a una pagina del libro.
- **Chapters** apre l'elenco dei capitoli e dei segnalibri.

## 11. Aggiornamento del firmware

Per aggiornare:

```bash
git pull
pio run -e m5stack-papers3 -t upload --upload-port COM3
```

Per un ripristino completo, usa l'immagine merged aggiornata con il comando del paragrafo 7.

## 12. Risoluzione dei problemi

### La porta non compare

- Verifica che il cavo sia dati.
- Prova un'altra porta USB.
- Installa i driver USB seriali richiesti dal sistema.
- Su Windows controlla Gestione dispositivi.

### Upload fallito o porta occupata

```bash
pio device list
```

Chiudi monitor seriali, IDE o programmi che usano la porta e riprova.

### La microSD non viene letta

- Verifica FAT32.
- Rimuovi e reinserisci la card.
- Prova una card più piccola o di marca nota.
- Ricrea la cartella `books/`.

### La board si riavvia o non mostra la libreria

- Controlla che il file EPUB non sia corrotto.
- Prova un libro diverso.
- Rifai il flash merged completo.

### Compilazione fallita

```bash
pio run -e m5stack-papers3 -t clean
pio run -e m5stack-papers3
```

Controlla di avere una connessione disponibile per scaricare le dipendenze PlatformIO.

### Immagini non visualizzate

Il firmware supporta JPEG e PNG. Alcuni formati (per esempio GIF animati) possono non essere decodificati: è una limitazione del decoder, non un errore della libreria.

## 13. Note di sicurezza

- Non estrarre la microSD durante scritture o aggiornamenti.
- Non interrompere il flash merged.
- Usa solo firmware e librerie provenienti da questa repository.

## 14. Licenza

Vedi [LICENSE](LICENSE).
