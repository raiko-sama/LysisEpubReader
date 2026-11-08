# Lysis Epub Reader

**Firmware V1.0 Beta per M5Stack Paper S3** — un lettore EPUB offline, essenziale e pensato per lo schermo e-ink.

<p align="center">
  <img src="assets/boot-screen-small.png" width="270" alt="Schermata di avvio del Lysis Epub Reader">
</p>

<p align="center">
  <a href="assets/boot-screen-medium.png">Boot screen — formato medio</a>
</p>

## Panoramica

Lysis Epub Reader è un firmware autonomo per **M5Stack Paper S3** dedicato alla lettura di libri in formato EPUB. L'interfaccia è costruita per l'e-ink: testi grandi, refresh parziali, navigazione a gesti e menu contestuali, senza dipendenze da servizi online.

Il progetto include il lettore EPUB, l'impaginazione, la gestione della libreria su microSD, la ricerca nel testo, le impostazioni, l'orologio, la sveglia, il collegamento WiFi per la sincronizzazione NTP, la modalità USB Disk e la gestione della batteria.

## Funzionalità

- **Libreria locale** — scansione di `/sd/books`, ordinamento e copertine/titoli.
- **Lettore EPUB** — capitoli, pagine, segnalibri e avanzamento persistente.
- **Ricerca nel testo** — ricerca a parola intera, risultati con anteprima, salto alla pagina trovata e ritorno alla pagina di partenza.
- **Tipografia** — sei livelli di lettura: 80%, 100%, 120%, 140%, 160%, 200%; interlinea e margini regolabili.
- **Immagini** — copertine e immagini interne JPEG/PNG scalate nel rispetto delle proporzioni, con supporto ai riferimenti immagine SVG.
- **Interfaccia e-ink** — refresh parziale durante la lettura, full refresh alla prima apertura, cambio font e antighosting periodico.
- **Menu e navigazione** — gesti touch, Go To pagina, capitoli, segnalibri e ritorno alla libreria.
- **Orologio e sveglia** — sincronizzazione NTP opzionale via WiFi e sveglia locale.
- **USB Disk Mode** — accesso alla microSD dal computer per copiare libri.
- **Risparmio energetico** — CPU a 240 MHz durante apertura/caricamento e 80 MHz come baseline; deep sleep con schermata di avvio dedicata.
- **Batteria** — apprendimento degli estremi di carica, rilevamento carica completa e cutoff di sicurezza.

## Ottimizzazioni

Il firmware è stato rifinito per ridurre lavoro inutile senza cambiare l'interfaccia:

- ricerca binaria degli offset di pagina;
- cache delle metriche e delle larghezze del testo;
- cache ZIP/manifest/titoli e cache testuale della ricerca;
- tokenizer HTML con confronto tag compatto;
- `brd_text_fit` con ricerca binaria del punto di troncamento;
- conversione EPUB→testo con meno copie e lookup indicizzati;
- conteggio pagine limitato ai capitoli reali;
- scrittura atomica dello stato e cache immagini gestita alla chiusura del libro;
- CPU 240 MHz solo nei percorsi di apertura/caricamento, ritorno a 80 MHz nella lettura normale.

## Hardware e requisiti

- **M5Stack Paper S3**
- microSD formattata FAT32 con cartella `books/`
- cavo USB dati
- PlatformIO Core oppure VS Code + estensione PlatformIO

## Struttura della repository

```
.
├── src/                    # firmware, UI, EPUB, paginazione, HAL
├── lib/                    # miniz, tinyxml2
├── boards/                 # definizione board PlatformIO
├── tools/merge_firmware.py # immagine merged per flash completo
├── assets/                 # immagini per README e manuale
├── firmware/               # binario merged pronto per il flash
├── platformio.ini
├── partitions.csv
├── README.md
└── MANUAL.md               # guida completa build, flash e primo avvio
```

## Build rapida

```bash
git clone <url-della-repository> LysisEpubReader
cd LysisEpubReader
pio run -e m5stack-papers3
```

Il binario applicativo viene creato in `.pio/build/m5stack-papers3/firmware.bin`. L'immagine merged da 8 MB viene creata nella radice del progetto.

## Flash rapido

Con la board collegata e la porta corretta:

```bash
pio run -e m5stack-papers3 -t upload --upload-port COM3
```

In alternativa, usando l'immagine merged inclusa:

```bash
esptool.py --chip esp32s3 -p COM3 -b 921600 write_flash 0x0 firmware/lysis-reader-m5stack-papers3.bin
```

> La porta può essere `COM3`, `/dev/ttyACM0`, `/dev/cu.usbmodem...` o un'altra porta assegnata dal sistema. Su Windows verificarla in Gestione dispositivi.

## Primo avvio

1. Copia i file `.epub` nella cartella `books/` della microSD.
2. Inserisci la microSD nel Paper S3.
3. Accendi la board e attendi la schermata di avvio.
4. Dalla libreria tocca un libro per aprirlo.
5. Tocca il bordo sinistro/destro per cambiare pagina; scorri verso l'alto per il menu e verso il basso per tornare alla libreria.

## Manuale completo

La guida passo passo per installazione, compilazione, flashing, preparazione della microSD, primo avvio e risoluzione dei problemi è in **[MANUAL.md](MANUAL.md)**.

## Versione

**V1.0 Beta** — firmware e documentazione allineati alla build corrente.

## Licenza

Vedi [LICENSE](LICENSE).
