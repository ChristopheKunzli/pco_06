## Introduction

Ce projet consiste à implémenter un thread pool en
utilisant un moniteur de Hoare permettant de gérer un ensemble de threads dans le but d'exécuter des tâches.

Lors de traitements intensifs en termes de consommation de temps CPU, il est utile de répartir la charge sur plusieurs
cœurs. Toutefois, la création et la destruction de threads sont des opérations coûteuses. Un thread pool permet
d'atténuer cet surcoût en conservant les threads utilisés pour permettre d'exécuter plusieurs tâches une fois le traitement de la précédente achevé.

Caractéristiques du système demandé :

- Limitation des threads actifs : Un nombre maximum de threads actif peut être défini.
- Gestion des requêtes : Les tâches en surplus sont placées dans une file d'attente, avec une limite maximale.
- Timeout des threads inactifs : Les threads inactifs pendant une durée définie sont terminés.

## Conception

Méthodes implémentées :

- `bool start(std::unique_ptr<Runnable> runnable)`
    - Si il y a `maxNbWaiting` tâches en attente, la tache est cancel puis refusée (return false)
    - Si le nombre de threads actifs est inférieur au nombre maximum de threads, un nouveau thread est créé pour
      exécuter
      la tâche.
    - Sinon, un thread en attente est réveillé.
    - La tache est ajoutée à la file d'attente.
    - Retourne true si la tâche est acceptée, false si trop de tâches sont en attentes.
    - Un bloquage de l'appelant peut avoir lieu si la tâche est acceptée, mais pas traitée immédiatement.
    - Les accès concurrents sont gérés par le moniteur.
- `void execute(Condition *condition, std::atomic<bool> *isWaiting)`
    - Routine des threads internes.
    - Prend une tâche de la file d'attente si disponible
    - Informe les clients en attente dans `start` que la tâche va être executée
    - Execute la tâche
    - Sinon le thread se met en attente.
    - Elle lance un thread avec la méthode `handleTimeout` qui vérifie et informe le thread parent s'il est
      inactif depuis trop longtemps. Dans ce cas, le thread parent est terminé.
- `void handleTimeout(std::shared_ptr<std::atomic<bool>> canTimeout, std::shared_ptr<std::atomic<bool>> stopRequested, Condition *condition, std::atomic<bool> *isWaiting)`
    - sleep pendant la durée de timeout.
    - Si le thread parent en attente n'a pas encore pu récupérer de tâche, il lui signal qu'il doit se terminer puis le
      réveil.
- `~ThreadPool()`
    - Si des tâches sont encores en attente, il attend qu'elles soient traitées.
    - Il demande et attend l'arrêt des threads en cours d'exécution. (join)
    - Il attend et détruit les threads de timeout.

Attributs de la class:
- `size_t nbThread`: le nombre de thread actif dans le thread pool
- `Condition stopCondition`: une variable de condition permettant d'informer le destructeur quand toutes les tâches en attente ont été traitées.
- `std::vector<struct Thread>threads{}`: un vecteur de la struct Thread qui contiend
  - `PcoThread *thread`: un pointeur sur le thread créé
  - `Condition *condition`: une variable de condition utilisée par le thread et celui gérant son timeout.
  - `std::atomic<bool> *isWaiting`: un boolean indiquant si le thread a terminé son travail et attend une nouvelle tâche à traiter.
- `std::queue<struct Task> waiting`: un vecteur de la struct Task qui contiend
  - `std::unique_ptr<Runnable> runnable`: un pointeur sur le runnable à traiter
  - `std::shared_ptr<std::atomic<bool>> isProcessed`: un boolean mis à true lorsque la runnable a été traitée.
  - `std::shared_ptr<Condition> condition`: une variable de condition utilisée par la méthode start pour être informé lorsque la tâche est traitée.

## Tests

Nous utilisons les tests fournis dans le projet pour valider le bon fonctionnement de notre implémentation.

Malheureusement, les tests ne fonctionnent pas comme attendu. En effet, le temps d'exécution est généralement trop long.

Cela peut être dû au fait que notre thread pool semble exécuter les tâches proche de manière séquentielle. Nous avons tenté de résoudre le problème, mais nous n'y sommes pas parvenus. Il nous semble pourtant que `monitorOut()` est toujours appelé dès que possible.

## Conclusion

Ce laboratoire nous a permis de concevoir et d’implémenter un système de thread pool en exploitant un moniteur de Hoare. Cette solution a pour objectif d’optimiser l’utilisation des ressources CPU tout en réduisant les coûts associés à la création et à la destruction fréquentes de threads.

Malgré nos efforts, nous avons rencontré des défis liés à des problèmes de deadlocks et à une parallélisation imparfaite de l’exécution des tâches. Bien que les problèmes de deadlocks aient été résolus, les difficultés liées à la parallélisation subsistent, impactant les performances globales du pool.

Par ailleurs, l’utilisation du moniteur de Mesa, découvert lors du laboratoire précédent, nous a semblé plus intuitive que celle du moniteur de Hoare dans ce laboratoire.