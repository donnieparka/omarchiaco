# Ricalcolo della disposizione dopo un cambio di geometria

Il 7 ottobre 2026 l'utente ha segnalato che conservare le coordinate assolute durante un cambio di scala rompe gli allineamenti. Ha scelto di mantenere fermo il portatile, quando è attivo, e ricalcolare le posizioni degli altri monitor in base alle nuove dimensioni logiche. L'operazione richiamata dal pannello per cambiare scala, risoluzione o rotazione conserva lati, allineamenti e spazi della disposizione precedente e usa la scala effettiva dopo la normalizzazione. Senza un portatile attivo resta fermo il monitor più vicino all'origine.

Il ricalcolo parte dalla disposizione osservata prima del comando. Le coordinate risultanti appartengono al singolo monitor e vengono salvate soltanto dopo la conferma prevista da [0004](0004-monitor-configuration-authority.md). Restano valide le regole di indipendenza di [0002](0002-independent-monitor-state.md): scollegare o disattivare un monitor lascia ferme le posizioni degli altri. Il pannello recepisce le modifiche manuali alla configurazione senza un secondo ricalcolo.

In disposizioni ambigue si usa il vicino più prossimo e l'allineamento più vicino fra bordi e centro, conservando l'eventuale scostamento; a parità si preferisce il centro. Un risultato sovrapposto viene rifiutato interamente.
