# Scorciatoie nei pannelli

I pannelli della barra mostrano etichette di due lettere accanto ai controlli
attivi. Apri un pannello e digita le lettere, senza Super o altri modificatori.
Puoi selezionare altre voci nello stesso pannello senza riavviare un programma.
Le opzioni aperte da un pulsante ricevono a loro volta le etichette.

Per un cursore, digita la sua etichetta e usa le frecce. Home ed End selezionano
gli estremi; Invio termina la regolazione. Escape annulla una sequenza iniziata
o termina la regolazione, poi un altro Escape chiude il pannello.
Backspace cancella la prima lettera. Le etichette compatibili con la prima
lettera si evidenziano. Durante l'inserimento di testo o di una password,
le lettere continuano a essere testo.

Wi-Fi identifica le reti tramite SSID e Bluetooth tramite indirizzo MAC.
La stessa voce conserva il codice se cambia posizione o viene ricreata.
Un codice di una voce scomparsa resta riservato fino alla chiusura del pannello.
Digitarlo non seleziona un altro dispositivo. All'attivazione il sistema
ricontrolla la lista e risolve il dispositivo corrente.

Le etichette occupano spazio riservato nel testo. I nomi lunghi nelle liste
continuano a usare l'elisione del pannello. Il contenuto dei pulsanti viene
ridotto se supera la loro larghezza; le etichette e il contenuto del pannello
sono ritagliati ai rispettivi limiti. Le liste continuano a scorrere.
I controlli disabilitati non ricevono codici.

La funzione è integrata nella barra principale tramite `shell/Ui/PanelHints.qml`. Non serve più un plugin che eredita la barra. I popup finali di conferma, incluso Mantieni/Ripristina, mantengono la propria gestione della tastiera.

wl-kbptr e le sue combinazioni Super+Ctrl+M e Super+Ctrl+Shift+M restano una
funzione separata per selezionare punti nelle altre applicazioni.

## Installazione e verifica

La barra del fork carica la funzione direttamente. Il piccolo helper per i controlli personalizzati usa Hyprland, jq e il servizio utente ydotool. `omarchy-dev-install-ydoo` configura questo servizio. Il selettore wl-kbptr è opzionale e indipendente da queste shortcut.

```sh
make install-panel-hints
make test-panel-hints
omarchy restart shell
```

I test usano reti e dispositivi simulati, senza modificare monitor o connessioni.
Richiedono Python 3 e Quickshell con i componenti Omarchy installati.
Per ispezionare i codici del pannello aperto:

```sh
omarchy-shell omarchy.panel-shortcuts state
```

La configurazione originale della macchina resta in `~/.config/omarchy`; questa migrazione modifica i sorgenti del fork.
