# Compte-rendu — Projet Transfert de Fichiers (FTP)
**Introduction aux Systèmes et Réseaux — L3 Informatique — Université Grenoble Alpes**

---

## 1. Présentation générale

Ce projet consiste à implémenter un serveur de fichiers inspiré du protocole FTP,
en C, en utilisant les primitives POSIX de gestion de processus et de communication
réseau fournies par la bibliothèque `csapp`. Le projet est divisé en quatre étapes
progressives, de la version mono-client basique jusqu'à un système distribué avec
répartition de charge, opérations avancées et authentification.

---

## 2. Architecture globale

Le système final est composé de trois types de processus distincts :

- **Le serveur maître** (`server.c`) : répartiteur de charge (*load balancer*).
  Il ne traite aucune requête FTP directement. Son seul rôle est d'attendre les
  connexions des clients, de choisir un esclave disponible en tourniquet, et de
  renvoyer ses coordonnées réseau au client.

- **Les serveurs esclaves** (`esclave.c`) : processus qui traitent effectivement
  les requêtes FTP des clients (GET, PUT, LS, RM). Ils sont lancés automatiquement
  par le maître via `fork` + `execv` au démarrage.

- **Le client** (`client.c`) : se connecte d'abord au maître pour obtenir
  l'adresse d'un esclave, puis dialogue directement avec cet esclave pour toutes
  ses requêtes.

Les types de données partagés (structures de requête et de réponse, énumérations,
fonctions de conversion réseau) sont définis dans `types.h`.

---

## 3. Réalisations par étape

### Etape I & II — Serveur FTP de base

**Protocole de communication**

Les échanges entre client et serveur reposent sur deux structures fixes :

- `request_t` : contient le type de la requête (`typereq_t`), un nom de fichier
  (`filename`) et un offset (`offset`) pour la reprise de transfert.
- `response_t` : contient le type de réponse, un code de statut (0 = succès,
  -1 = erreur), un drapeau `endOfFile`, un tableau de données (`data`) et la
  taille effective des données (`dataSize`).

Toutes les structures sont converties en ordre réseau (*big-endian*) avant envoi
et reconverties à la réception grâce aux fonctions `hton_req`, `ntoh_req`,
`hton_resp`, `ntoh_resp`.

**Transfert par blocs (Q8)**

Le fichier est lu et envoyé par blocs de `MAXCHAR` (100) octets. Le client
reconnaît la fin du transfert grâce au champ `endOfFile` de la dernière réponse.
Ce découpage permet de transférer des fichiers arbitrairement grands sans
monopoliser la mémoire.

**Plusieurs requêtes par connexion (Q9)**

Le serveur maintient la connexion ouverte tant que le client n'envoie pas de
requête `BYE`. La boucle principale d'`apply_request` lit les requêtes en continu
jusqu'à réception de `BYE` ou fermeture du socket.

**Reprise de transfert (Q10)**

Lors d'un GET, le client mesure la taille du fichier déjà reçu dans `repClient/`
(via `wc -c`) et la transmet dans le champ `offset` de la requête. Le serveur
positionne sa lecture avec `lseek(fd, req.offset, SEEK_SET)` pour n'envoyer que
la partie manquante. Si le client est interrompu en cours de transfert, l'esclave
détecte l'erreur d'écriture (retour négatif de `rio_writen`) et retourne proprement
sans planter, grâce au flag `client_gone` et à l'ignorance de `SIGPIPE`.

**Pool de processus et arrêt propre (Q3 & Q4)**

Le serveur maintient un pool de `NPROC` processus fils qui acceptent les connexions
en parallèle. Un gestionnaire de signal `SIGINT` envoie `SIGTERM` à chaque fils
du pool et termine proprement.

---

### Etape III — Répartition de charge

**Enregistrement des esclaves (Q11 & Q12)**

Au démarrage, le maître ouvre un port d'enregistrement dédié (`SLAVE_REG_PORT` =
2120) **avant** de lancer les esclaves par `fork` + `execv`. Chaque esclave se
connecte à ce port et envoie un message `PORT` contenant son propre port d'écoute
(dans `resp.status`) et son adresse IP (dans `resp.data`).

Le maître attend que les `NB_SLAVES` esclaves soient tous enregistrés avant
d'accepter le moindre client. Il leur envoie ensuite à chacun :
1. Un accusé de réception (message `PORT` avec `status = 0`).
2. La liste complète des esclaves (`slave_list_t`) pour permettre la propagation
   inter-esclaves (Q16).

Les informations retenues pour chaque esclave sont : son **adresse IP** et son
**port d'écoute**, qui sont les seules données dont le client a besoin pour se
connecter directement.

**Redirection des clients (Q13)**

Lorsqu'un client se connecte au maître, celui-ci choisit un esclave selon un
tourniquet (*round-robin*) et lui envoie les coordonnées de l'esclave choisi dans
un message `PORT`. Le client ferme la connexion avec le maître et se reconnecte
directement à l'esclave. Toute la phase FTP (GET, PUT, LS, RM…) se déroule ensuite
exclusivement entre le client et l'esclave, sans passer par le maître.

---

### Etape IV — Opérations avancées

**Commande ls (Q15)**

L'esclave exécute `popen("ls ./repServeur/", "r")` et envoie le résultat au client
ligne par ligne sous forme de blocs `response_t` avec `endOfFile = false`, puis un
bloc final vide avec `endOfFile = true`. Le client affiche chaque ligne reçue sur
la sortie standard.

**Commandes put et rm avec propagation (Q16)**

- `put` : le client envoie la requête puis le contenu du fichier bloc par bloc
  (chaque bloc est un `response_t` avec `endOfFile` à `true` sur le dernier).
  L'esclave écrit le fichier dans `repServeur/`, puis propage l'opération à tous
  les autres esclaves via une requête `SYNC` en connexion directe pair-à-pair.

- `rm` : l'esclave supprime le fichier avec `unlink()` puis propage via `SYNC`.

La propagation est **best-effort** : l'esclave ouvre une connexion vers chaque pair
et envoie la requête `SYNC` contenant le type d'opération (encodé dans `offset`)
et les données nécessaires. On ne garantit pas de cohérence forte : un client qui
se reconnecte immédiatement après un `put` ou un `rm` peut tomber sur un esclave
qui n'a pas encore reçu la propagation.

**Authentification (Q17)**

Chaque connexion client maintient un état booléen `auth` (initialisé à `false`).
La commande `login` envoie les identifiants encodés sous la forme `"login:password"`
dans le champ `filename` de la requête. L'esclave vérifie les identifiants dans une
table statique (`USERS[]`) et met `auth = true` en cas de succès.

Les commandes `put` et `rm` vérifient `auth` en début de traitement et retournent
une erreur immédiate si la connexion n'est pas authentifiée. L'état d'authentification
est strictement **par connexion** : il n'est pas partagé entre sessions.

---

## 4. Interconnexion des entités

```
┌─────────────────────────────────────────┐
│            DÉMARRAGE                    │
│                                         │
│  ./server                               │
│    ├─ fork+execv ──► ./esclave ... 2122 │
│    └─ fork+execv ──► ./esclave ... 2123 │
│                                         │
│  Esclaves s'enregistrent sur port 2120  │
│  Maître envoie slave_list_t à chacun    │
└─────────────────────────────────────────┘

Client                     Maître (port 2121)         Esclave (port 2122 ou 2123)
  │                               │                             │
  │── connexion TCP ─────────────►│                             │
  │◄─ message PORT (ip:port) ─────│                             │
  │── fermeture connexion ────────►│                             │
  │                                                             │
  │── connexion TCP ────────────────────────────────────────────►│
  │── requête GET/PUT/LS/RM/LOGIN ──────────────────────────────►│
  │◄─ réponse(s) ───────────────────────────────────────────────│
  │── requête BYE ──────────────────────────────────────────────►│
  │── fermeture connexion ──────────────────────────────────────►│

Propagation PUT/RM :
  Esclave 1 ──── SYNC ────────────────────────────────────────►Esclave 2
```

---

## 5. Description des tests

Les tests complets sont décrits dans le fichier `tests.txt`. Voici un résumé des
scénarios couverts, dans l'ordre des questions du sujet :

| Question | Scénario testé |
|----------|---------------|
| Q1–Q3 | Connexion de base, envoi d'un GET, réception et vérification du fichier |
| Q4 | Arrêt propre du serveur (Ctrl+C), vérification qu'aucun fils ne reste |
| Q5 | Séparation des répertoires repServeur/ et repClient/ |
| Q6–Q7 | GET d'un fichier inexistant : message d'erreur sans plantage |
| Q8 | GET d'un fichier > MAXCHAR : transfert multi-blocs, vérification par diff |
| Q9 | Plusieurs GET sur une même connexion avant bye |
| Q10 | Interruption Ctrl+C pendant un GET, reprise à l'offset correct au relancement |
| Q11–Q12 | Démarrage du serveur : vérification de l'enregistrement des 2 esclaves |
| Q13 | Redirection vers esclave, tourniquet vérifié sur deux terminaux simultanés |
| Q15 | ls : liste correcte du répertoire, mise à jour dynamique |
| Q16 put | put après login, vérification diff, propagation aux deux esclaves |
| Q16 rm | rm après login, fichier absent du disque et du ls, propagation, rm inexistant |
| Q17 | put/rm sans login refusés, mauvais mdp refusé, login valide accepté, état par connexion |
| Robustesse | Survie esclave après Ctrl+C client, clients séquentiels, commande invalide |

---

## 6. Difficultés rencontrées et choix techniques

**Gestion de SIGPIPE** : lorsqu'un client se déconnecte abruptement, toute écriture
sur le socket mort génère `SIGPIPE`, qui tue le processus par défaut. Nous ignorons
ce signal (`Signal(SIGPIPE, SIG_IGN)`) et utilisons `rio_writen` (minuscule, non
fatale) à la place de `Rio_writen` pour détecter l'erreur et continuer à servir
d'autres clients.

**Wrappers fatals vs. non-fatals** : dans `apply_request`, toutes les lectures et
écritures réseau utilisent les fonctions minuscules (`rio_readn`, `rio_writen`)
pour que le processus ne se termine pas sur une erreur réseau. Les wrappers majuscules
(`Rio_readn`, `Rio_writen`) sont conservés uniquement dans les phases d'initialisation
où une erreur est effectivement irrécupérable.

**Transmission de la liste des esclaves** : pour que chaque esclave connaisse les
adresses de ses pairs (nécessaire pour la propagation), le maître garde les connexions
d'enregistrement ouvertes jusqu'à ce que tous les esclaves soient enregistrés, puis
envoie la `slave_list_t` complète à chacun dans la foulée de l'accusé de réception.

**Conflits de noms dans le switch** : le compilateur C ne permet pas de déclarer des
variables dans deux branches d'un `switch` avec le même nom sans blocs `{}`. Chaque
`case` complexe a été encapsulé dans un bloc pour éviter ces conflits.

**Encodage des identifiants de login** : la structure `request_t` ne dispose que d'un
champ `filename` comme zone de texte. Nous l'utilisons pour transporter les identifiants
sous la forme `"login:password"`, séparés par un caractère `:`, que l'esclave parse avec
`strchr`.