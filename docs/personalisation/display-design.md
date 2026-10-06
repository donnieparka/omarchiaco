# Impostazioni dei monitor

Progetto confermato dall'utente e implementato il 6 ottobre 2026. Motore C, servizio e popup QML installati. Verificati sul desktop reale il caricamento del popup, l'applicazione della disposizione HP a sinistra di Xiaomi e il ripristino automatico dopo 15 secondi senza conferma. Dopo il passaggio alla conferma centrale Omarchy, la disposizione HP a sinistra di Xiaomi è stata confermata e salvata: HP a (-4115,120), Xiaomi a (-3440,0), portatile a (0,0). Nessuna transazione pendente e nessun errore Hyprland al controllo finale.

## Richieste

- Scegliere un monitor di riferimento anche quando sono collegati tre o più monitor.
- Disporre un monitor a sinistra, a destra, sopra o sotto il riferimento.
- Caso iniziale: HP Full HD a sinistra dello Xiaomi.
- Accedere a posizione, rotazione, risoluzione, scala e attivazione dalla barra, eliminando il passaggio al menu separato.
- Implementare il motore in C e mantenere la presentazione in QML nel popup della barra.
- Scegliere le modalità video tra quelle riportate da Hyprland.

## Decisioni confermate

- La relazione di posizione è permanente e deve essere mantenuta dopo cambi di scala, risoluzione, rotazione e riconnessione.
- Uno spostamento manuale scioglie le relazioni che coinvolgono il monitor spostato. Gli altri monitor mantengono la propria posizione; l'utente può ricreare le relazioni. Questo prevale sul trascinamento automatico dei monitor collegati.
- L'allineamento predefinito è al centro. L'utente potrà scegliere anche altri allineamenti.
- Il popup esistente presenta pulsanti distinti per le impostazioni. Il motore è in C; il livello grafico resta QML.
- Quando viene scollegato un monitor, i monitor esterni rimasti si affiancano al display del portatile.
- Dopo una modifica manuale l'utente ha 15 secondi per premere Mantieni. Senza conferma si ripristinano le impostazioni precedenti.
- Chiarimento successivo dell'utente: Mantieni/Ripristina devono comparire in un grande popup al centro dello schermo, come il vecchio selettore Omarchy, non dentro il pannello della barra.

## Comportamento concordato

- Nel popup ogni monitor offre Posizione, Rotazione, Risoluzione, Scala e Attiva/Disattiva.
- Posizione permette di scegliere un altro monitor collegato, un lato e l'allineamento; il centro è il valore iniziale.
- La nuova relazione scelta sostituisce quelle coinvolte nello spostamento manuale. Le relazioni sciolte non vengono ripristinate automaticamente.
- La riconnessione ripristina solo relazioni ancora salvate. La disposizione temporanea dopo uno scollegamento non cancella tali relazioni.
- Le disposizioni con sovrapposizioni o relazioni incompatibili vengono segnalate prima dell'applicazione, lasciando all'utente la scelta di una diversa posizione.
- La disposizione automatica dopo collegamento o scollegamento non richiede conferma; il timer riguarda le modifiche manuali.
- Viene preservata la protezione contro la disattivazione dell'ultimo monitor attivo.
- Il primo assetto richiesto è HP a sinistra di Xiaomi, con centri allineati e senza modificare scala, risoluzione o rotazione.
- Un subagente implementerà i controlli QML mentre il lavoro principale realizzerà il motore C e la sua integrazione.

## Verifiche

- Calcolo delle posizioni con scale diverse, rotazioni e almeno tre monitor.
- Rottura delle relazioni dopo uno spostamento manuale e conservazione delle relazioni dopo cambi di geometria.
- Affiancamento al portatile dopo scollegamento e ripristino delle relazioni ancora salvate alla riconnessione.
- Conferma e scadenza dei 15 secondi, inclusa chiusura del popup senza conferma.
- Elenco delle modalità effettivamente disponibili e rifiuto di modalità non supportate.
- Compilazione C, controlli del popup QML e verifica della configurazione Hyprland applicata.

I test del modello C passano anche con AddressSanitizer e UndefinedBehaviorSanitizer. I 22 test di integrazione passano contro una simulazione isolata di Hyprland. Tra i casi verificati: riconnessione durante un ricalcolo già in corso, scollegamento durante una modifica da confermare, attesa del controllo periodico senza false segnalazioni di modifica pendente e ripristino dopo l'arresto forzato del processo che gestisce il timer.

La conferma usa ora `omarchy.menu`, con larghezza 720, opzioni Ripristina/Mantieni e file di risposta associati al token della transazione. Il timer parte dopo l'apertura del popup. Un errore di apertura ripristina la configurazione; Esc e scadenza ripristinano; Mantieni salva. Il processo che applica la modifica sopravvive alla distruzione del client QML, per esempio quando si disattiva il monitor che ospita il pannello.

L'interfaccia QML è stata caricata in un'istanza di prova con tre monitor simulati e verificata con applicazione, conferma, ripristino ed errori del motore.

Backup precedente all'installazione: `/home/iaco/.local/state/omarchy/monitor-migration-20261006-125840/`.

## Evidenze dal codice

- `bin/omarchy-monitor-apply` accetta posizioni automatiche generiche, senza un monitor di riferimento.
- `shell/plugins/panels/monitor/Panel.qml` chiude il popup per avviare `omarchy-monitor-layout --monitor` tramite il pulsante `Resolution & layout…`.
- `shell.json` contiene sia il tray `omarchy.tray` sia il widget display `iaco.monitor`.
