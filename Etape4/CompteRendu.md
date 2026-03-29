# Compte-rendu — Projet Transfert de Fichiers (FTP)
**Introduction aux Systèmes et Réseaux — L3 Informatique — Université Grenoble Alpes**

---

## 1. Présentation générale

Ce projet consiste à implémenter un serveur de fichiers inspiré du protocole FTP,
en C, en utilisant les primitives de gestion de processus et de communication
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
  par le maître via `fork` + `execv` au démarrage. Chaque esclave reçoit un index
  du maître et dispose de son propre répertoire de travail (`repServeur0/`,
  `repServeur1/`, etc.), initialisé au démarrage depuis le répertoire de référence
  `repServeur/`.

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

L'esclave construit dynamiquement la commande `ls <my_repdir>` avec `snprintf` et
l'exécute via `popen`. Le résultat est envoyé au client ligne par ligne sous forme
de blocs `response_t` avec `endOfFile = false`, puis un bloc final vide avec
`endOfFile = true`. Le client affiche chaque ligne reçue sur la sortie standard.

**Commandes put et rm avec propagation (Q16)**

- `put` : le client envoie la requête puis le contenu du fichier bloc par bloc
  (chaque bloc est un `response_t` avec `endOfFile` à `true` sur le dernier).
  L'esclave écrit le fichier dans son répertoire propre (`repServeurN/`), puis
  propage l'opération à tous les autres esclaves via une requête `SYNC` en
  connexion directe pair-à-pair.

- `rm` : l'esclave supprime le fichier avec `unlink()` dans son répertoire propre,
  puis propage via `SYNC`.

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
┌──────────────────────────────────────────────────────┐
│  DÉMARRAGE : ./server                                │
│                                                      │
│  Ouvre SLAVE_REG_PORT (2120)                         │
│    ├─ fork+execv ──► ./esclave localhost 2120 2122 0 │
│    ├─ fork+execv ──► ./esclave localhost 2120 2123 1 │
│    └─ ...                                            │
│                                                      │
│  Chaque esclave :                                    │
│    1. crée repServeurN/                              │
│    2. cp -rf repServeur/ → repServeurN/              │
│    3. s'enregistre sur SLAVE_REG_PORT                │
│                                                      │
│  Maître envoie slave_list_t à chacun                 │
└──────────────────────────────────────────────────────┘

Client                   Maître (2121)        Esclave N (2122+N)
  │                           │                      │
  │─ connexion TCP ──────────►│                      │
  │◄─ PORT (ip:port esclave) ─│                      │
  │─ fermeture ──────────────►│                      │
  │                                                  │
  │─ connexion TCP ──────────────────────────────── ►│
  │─ GET / PUT / LS / RM / LOGIN ───────────────────►│
  │◄─ réponse(s) ────────────────────────────────────│
  │─ BYE ───────────────────────────────────────────►│

Propagation PUT/RM (pair-à-pair, best-effort) :
  repServeur0/fichier ──── SYNC ────► repServeur1/fichier
                      └─── SYNC ────► repServeur2/fichier ...
```

---

## 5. Description des tests

Les tests complets sont décrits dans le fichier `tests.txt`. Les fichiers de test
(`hello.txt`, `large.txt`) sont à placer dans `repServeur/` et `upload.txt` dans
`repClient/` avant le premier lancement. À chaque `./server`, les répertoires
`repServeurN/` sont réinitialisés depuis `repServeur/`, ce qui garantit un état
de départ identique et reproductible pour chaque session de test.

| Question | Scénario testé |
|----------|----------------|
| Q1–Q3 | Connexion de base, GET, vérification du fichier reçu par diff |
| Q4 | Arrêt propre (Ctrl+C), vérification qu'aucun esclave ne reste |
| Q5 | Séparation repServeurN/ et repClient/ |
| Q6–Q7 | GET fichier inexistant : erreur sans plantage |
| Q8 | GET fichier > MAXCHAR : transfert multi-blocs, diff |
| Q9 | Plusieurs GET sur une même connexion |
| Q10 | Ctrl+C pendant GET, reprise à l'offset correct |
| Q11–Q12 | Enregistrement des esclaves et création de repServeurN/ |
| Q13 | Tourniquet : deux clients simultanés sur deux esclaves différents |
| Q15 | ls : listing de repServeurN/ affiché chez le client |
| Q16 put | put après login, diff, propagation vérifiée dans repServeur1/ |
| Q16 rm | rm après login, absent de repServeur0/ et repServeur1/ |
| Q17 | put/rm sans login refusés, mauvais mdp, login valide, état par connexion |
| Robustesse | Survie après Ctrl+C client, commande invalide |

---

## 6. Difficultés rencontrées et choix techniques

**Répertoires individuels par esclave** : chaque esclave reçoit un index du maître
via `execv` et construit son répertoire de travail `repServeurN/` au démarrage.
Il y copie le contenu de `repServeur/` (répertoire de référence) via `cp -rf`,
ce qui garantit un état initial identique à chaque lancement. La commande `SYNC`
propage ensuite les modifications entre ces répertoires distincts, rendant la
démonstration de la cohérence éventuelle observable directement sur le système de
fichiers.

**Gestion de SIGPIPE** : lorsqu'un client se déconnecte abruptement, toute écriture
sur le socket mort génère `SIGPIPE`, qui tue le processus par défaut. Nous ignorons
ce signal (`Signal(SIGPIPE, SIG_IGN)`) et utilisons `rio_writen` (minuscule, non
fatale) à la place de `Rio_writen` pour détecter l'erreur et continuer à servir
d'autres clients.

**Wrappers fatals vs. non-fatals** : dans `apply_request`, toutes les lectures et
écritures réseau utilisent les fonctions minuscules (`rio_readn`, `rio_writen`)
pour que le processus ne se termine pas sur une erreur réseau. Les wrappers
majuscules (`Rio_readn`, `Rio_writen`) sont conservés uniquement dans les phases
d'initialisation où une erreur est effectivement irrécupérable.

**Transmission de la liste des esclaves** : pour que chaque esclave connaisse les
adresses de ses pairs (nécessaire pour la propagation), le maître garde les
connexions d'enregistrement ouvertes jusqu'à ce que tous les esclaves soient
enregistrés, puis envoie la `slave_list_t` complète à chacun dans la foulée de
l'accusé de réception.

**Encodage des identifiants de login** : la structure `request_t` ne dispose que
d'un champ `filename` comme zone de texte libre. Nous l'utilisons pour transporter
les identifiants sous la forme `"login:password"`, séparés par un caractère `:`,
que l'esclave parse avec `strchr`.