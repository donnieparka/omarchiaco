# Stato indipendente per monitor

Il 7 ottobre 2026 l'utente ha scelto che ogni monitor ricordi coordinate, modalità video, scala, rotazione e attivazione proprie, senza dipendenze persistenti da altri monitor. I comandi di posizione relativa usano un riferimento per calcolare le coordinate una volta. Scollegare o disattivare un monitor non sposta gli altri; alla riconnessione il monitor riprende le proprie coordinate salvate.

I comandi del pannello accettano eventuali spazi vuoti e rifiutano disposizioni sovrapposte. I cambi di scala, risoluzione e rotazione richiesti dal pannello ricalcolano gli allineamenti secondo [0003](0003-geometry-reflow.md).

Un errore del driver o un'attivazione fallita non diventano una preferenza salvata di disattivazione. L'identità del singolo monitor, la persistenza nella configurazione nativa e il recupero temporaneo dello schermo del portatile seguono [0004](0004-monitor-configuration-authority.md).
