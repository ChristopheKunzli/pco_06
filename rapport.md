## Introduction

Ce projet consiste à implémenter un thread pool gérant un ensemble de threads dans le but d'exécuter des tâches, en
utilisant un moniteur de Hoare.

Lors de traitements intensifs en termes de consommation de temps CPU, il est utile de répartir la charge sur plusieurs
cœurs. Toutefois, la création et la destruction de threads sont des opérations coûteuses. Un thread pool permet
d'atténuer cet overhead en maintenant une piscine de threads prêts à exécuter des tâches.

Caractéristiques du système demandé :

- Limitation des threads actifs : Un nombre maximum de threads actif peut être défini.
- Gestion des requêtes : Les tâches en surplus sont placées dans une file d'attente, avec une limite maximale.
- Timeout des threads inactifs : Les threads inactifs pendant une durée définie doivent être terminés pour libérer des
  ressources.

## Conception

Méthodes implémentées :

- `bool start(std::unique_ptr<Runnable> runnable)`
    - La tache est ajoutée à la file d'attente.
    - Si le nombre de threads actifs est inférieur au nombre maximum de threads, un nouveau thread est créé pour
      exécuter
      la tâche.
    - Sinon, un thread en attente est réveillé.
    - Retourne true si la tâche est acceptée, false si trop de tâches sont en attentes.
    - Un bloquage de l'appelant peut avoir lieu si la tâche est acceptée, mais pas traitée immédiatement.
    - Les accès concurrents sont gérés par le moniteur.
- `void execute()`
    - Routine des threads internes.
    - Prend une tâche de la file d'attente si disponible et l'exécute.
    - Sinon le thread se met en attente.
    - Elle lance un thread avec la méthode `handleTimeout` qui vérifie et informe le thread parent s'il est
      inactif depuis trop longtemps. Dans ce cas, le thread parent est terminé.
- `void handleTimeout(std::shared_ptr<std::atomic<bool>> requestedStop, std::shared_ptr<std::atomic<bool>> timeout)`
    - sleep pendant la durée de timeout.
    - Si le thread parent en attente n'a pas encore pu récupérer de tâche, il lui signal qu'il doit se terminer puis le
      réveil.
- `~ThreadPool()`
    - Si des tâches sont encores en attente, il attend qu'elles soient traitées.
    - Il demande et attend l'arrêt des threads en cours d'exécution. (join)
    - Il attend et détruit les threads de timeout.

## Tests

Nous utilisons les tests fournis dans le projet pour valider le bon fonctionnement de notre implémentation.

## Conclusion
