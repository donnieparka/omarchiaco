# Mouse da tastiera

Premi **Super + Ctrl + M** per la modalità smart. Il programma rileva le aree
visibili e le etichetta con lettere. Rilascia i tasti e digita le lettere del
punto desiderato. Compare un mirino al centro della zona scelta.
Il rilevamento usa i contorni dell'immagine, quindi può includere anche testo
e zone che non sono pulsanti.

**Super + Ctrl + Shift + M** apre la griglia regolare, utile se smart non
identifica il punto desiderato. Se smart non riesce a catturare lo schermo o
non rileva nessun punto, la griglia si apre automaticamente.

Le frecce scelgono la metà della zona nella direzione indicata. Ogni pressione
riduce la zona e avvicina il mirino al punto desiderato, fino al singolo pixel.
La barra di aiuto compare durante questa fase.

| Tasto dopo la scelta della cella | Azione |
| --- | --- |
| Frecce | Restringi la zona e sposta il mirino |
| Invio oppure G | Clic sinistro e chiudi |
| H | Clic destro e chiudi |
| B | Clic centrale e chiudi |
| Spazio | Sposta il puntatore e chiudi senza clic |
| Backspace | Annulla l'ultimo passo, anche tornando alla griglia |
| Esc | Chiudi senza clic |

La selezione compare sul monitor attivo. **Ctrl + Alt + Tab** cambia il monitor
attivo; poi **Super + Ctrl + M** apre lì la griglia.
Funziona sul desktop e sulle applicazioni Wayland e XWayland.
Non richiede un servizio aggiuntivo o privilegi di amministratore.
Usa il servizio utente `ydotool`, già installato, attivo e abilitato su questo
sistema, per inviare il clic. Il selettore restituisce le coordinate;
il comando le applica con Hyprland e controlla la posizione prima del clic.

## Installazione locale

Il programma C è [wl-kbptr](https://github.com/moverest/wl-kbptr), versione
0.4.1, commit `6191d1725515830b8c51db25b57f9eeceb1565e5`.
Il sorgente è in `~/.local/share/keyboard-mouse/wl-kbptr`.
La patch locale è in `default/keyboard-mouse/wl-kbptr-omarchy.patch`. Evita il conflitto
con gli screenshot di Omarchy, aggiunge Invio per il clic sinistro e la guida
durante la rifinitura, supporta OpenCV 5 e gestisce gli errori di cattura e
l'assenza di punti per consentire la griglia alternativa. Annulla gli
aggiornamenti grafici pendenti prima di distruggere la finestra, evitando
il crash durante la conferma. Le divisioni
mantengono anche l'ultimo pixel nelle zone di dimensione dispari.
Il binario installato è `~/.local/bin/wl-kbptr-omarchy`.

Per ricompilare dal sorgente locale già modificato:

```bash
cd ~/.local/share/keyboard-mouse/wl-kbptr
uv run --with meson --with ninja -- meson setup build --reconfigure --buildtype=release -Dopencv=enabled
uv run --with meson --with ninja -- meson compile -C build
install -m755 build/wl-kbptr ~/.local/bin/wl-kbptr-omarchy
```

La configurazione di colori, lettere e modalità è in `~/.config/wl-kbptr/config`.
La scorciatoia è in `~/.config/hypr/bindings.lua`.

## Verifiche

Il rilevamento, la rifinitura e la selezione dei tre pulsanti sono stati
verificati su Xiaomi, portatile e HP ruotato usando l'uscita delle coordinate
senza inviare clic. Un clic sinistro reale è stato poi ricevuto da una finestra
temporanea sul portatile a scala 3, tramite il comando installato.
Hyprland carica entrambe le scorciatoie senza errori di configurazione.
