# Architecture Gameplay — Contrat d'autoring & API des 4 briques

> **Statut : DESIGN à valider.** Aucune de ces API n'est encore implémentée. Ce
> document définit *comment on code un jeu* dans NextEngine, pour humains **et**
> LLM, avec **un seul paradigme** et des garde-fous qui rendent le code sale
> *impossible* — pas juste découragé.

## 0. La doctrine

> **La logique vit dans des `Behaviour` ; on compose avec des nœuds ; on appelle
> vers le bas, on signale vers le haut ; l'état global vit dans des autoloads.**

5 règles dures :

1. **Toute logique de jeu = un `Behaviour`.** Un nœud, c'est de la composition +
   des données. Pas de logique dans les nœuds, jamais de classe « Manager ».
2. **Composer, pas grossir.** Une fonctionnalité = une petite scène/prefab + des
   behaviours focalisés (un `Player` = `Movement` + `Health` + `Inventory`, pas
   un `PlayerController` de 2000 lignes).
3. **Call down, signal up.** Un behaviour pilote son nœud + ses descendants par
   appel direct. Pour communiquer vers le haut ou à distance : **signal**.
4. **Pas de global sauf services moteur + autoloads.** `Input`, `Time`, `Audio`,
   `tree()` sont les seuls globaux fournis. L'état de jeu persistant va dans un
   **autoload** (singleton béni, jamais codé à la main).
5. **Trouver un nœud = groupe ou requête scopée, jamais par son nom.**

### « Contraint par construction »

Le point clé : l'API **ne fournit pas** les outils du code sale.

| Anti-pattern | Pourquoi impossible ici |
|---|---|
| God-manager qui tripote tout | Aucun nœud n'est joignable globalement par nom. |
| Singleton codé à la main (`static T& instance()`) | On **déclare** un autoload ; le moteur l'instancie/persiste. L'utilisateur n'écrit jamais de cycle de vie. |
| Bus d'événements global | Aucun type « EventBus » n'existe. Les events transverses sont des **signaux typés sur un autoload nommé** (propriétaire clair). |
| Callbacks `std::function` qui fuient / pendouillent | `listen()` gère la durée de vie automatiquement. |
| `findNodeByName("Player")` partout | N'existe pas. Seulement `group()` (opt-in) + requêtes sur son propre sous-arbre. |
| Logique de chargement de scène recopiée | Un seul appel `tree().changeScene(...)`, différé par le moteur. |

---

## 1. Brique « Signaux » — `core/Signal.hpp`

Communication **typée, déclarée, à durée de vie gérée**. Remplace tout callback
ad-hoc (dont les `std::function onEnter/onExit` actuels de `AreaNode`).

```cpp
namespace ne {

// Handle RAII move-only. Se déconnecte à la destruction ou via disconnect().
// Sûr si l'émetteur OU l'auditeur meurt en premier (bloc de contrôle partagé).
class Connection {
public:
    Connection() = default;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    ~Connection();                 // déconnecte
    void disconnect();
    bool connected() const;
};

// Émetteur typé. Déclaré comme membre PUBLIC d'un Behaviour (ou Node).
template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    [[nodiscard]] Connection connect(Slot slot) const;  // connexion brute (rare)
    void emit(Args... args) const;                       // notifie tous les slots
    size_t slotCount() const;
};

} // namespace ne
```

On **déclare** ses signaux ; on **émet** dans sa propre logique :

```cpp
class HealthBehaviour : public Behaviour {
public:
    const char* typeName() const override { return "Health"; }

    Signal<int> healthChanged;   // émet la nouvelle valeur
    Signal<>    died;

    void damage(int amount) {
        hp_ = std::max(0, hp_ - amount);
        healthChanged.emit(hp_);
        if (hp_ == 0) died.emit();
    }
private:
    int hp_ = 100;
};
```

On **écoute** via `Behaviour::listen()` — connexion auto-déconnectée quand le
behaviour auditeur meurt (zéro bookkeeping, donc personne n'évite les signaux) :

```cpp
// Ajouts à Behaviour (core) :
class Behaviour {
public:
    // Connecte un signal ; la connexion vit aussi longtemps que CE behaviour.
    template <typename... Args, typename Fn>
    void listen(const Signal<Args...>& sig, Fn&& fn);
private:
    std::vector<Connection> connections_;   // possédées par le behaviour
};
```

```cpp
class HealthBarBehaviour : public Behaviour {
    void onReady() override {
        // descendant → accès direct autorisé (call down)
        auto* hp = node()->findBehaviourInChildren<HealthBehaviour>();
        listen(hp->healthChanged, [this](int v){ setFill(v / 100.0f); });
    }
};
```

**Règle d'usage (dans CLAUDE.md) :** tu n'as le droit de `connect`/`listen` que
sur (a) un signal d'un de tes descendants, ou (b) un signal d'un **autoload**.
Jamais sur un nœud arbitraire récupéré par nom.

---

## 2. Brique « SceneTree » — `scene/SceneTree.{hpp,cpp}`

Le **contexte runtime** : scène courante, changement de scène différé, autoloads,
groupes, pause. Possédé par l'`Engine`. Joignable depuis n'importe quel nœud.

```cpp
namespace ne {

class SceneTree {
public:
    // — Scène — (différé : exécuté en fin de frame, sûr depuis onUpdate)
    void   changeScene(const std::string& scenePath);
    void   reloadScene();
    Scene& currentScene();

    // — Autoloads (brique 3) —
    template <typename T> T* autoload();              // par type de Behaviour
    Node*  autoloadNode(const std::string& name);     // par nom

    // — Groupes (brique 4) —
    const std::vector<Node*>& group(const std::string& name);
    Node*  firstInGroup(const std::string& name);

    // — Pause / sortie —
    void   setPaused(bool paused);   // v1 : pilote Time::setScale(0/1)
    bool   paused() const;
    void   quit();
};

} // namespace ne
```

Accès depuis le graphe — **un seul chemin** :

```cpp
// Node (core) : remonte jusqu'à la racine Scene et renvoie son arbre (null hors-arbre).
class Node {
public:
    SceneTree* tree() const;
};

// Behaviour : raccourci.
class Behaviour {
public:
    SceneTree* tree() const { return node() ? node()->tree() : nullptr; }
};
```

Usage :

```cpp
class PlayButton : public Behaviour {
    void onReady() override {
        listen(node()->getBehaviour<UIButtonBehaviour>()->clicked,
               [this]{ tree()->changeScene("levels/level1.scene"); });
    }
};
```

**Différé** : `changeScene` ne fait que mémoriser la cible. L'`Engine`, après
`scene->update(dt)`, détecte la demande, charge la nouvelle scène via
`SceneSerializer`, **réattache les autoloads** (qui survivent), bascule la scène
active. On ne libère jamais l'arbre en plein update.

---

## 3. Brique « Autoloads » — état global sans singleton manuel

Un autoload = **un `Behaviour` normal** (avec ses données + ses signaux) porté par
un nœud persistant que le moteur crée une fois et **garde vivant à travers les
changements de scène**. L'utilisateur **ne code aucun singleton** : il déclare,
le moteur gère instance + durée de vie + ordre d'init.

```cpp
// Déclaration — UNE ligne, au démarrage (ou via la config du projet).
tree.registerAutoload<GameState>("GameState");

// Le state, c'est juste un Behaviour :
class GameState : public Behaviour {
public:
    const char* typeName() const override { return "GameState"; }

    Signal<int> scoreChanged;
    Signal<>    gameOver;

    void addScore(int n) { score_ += n; scoreChanged.emit(score_); }
    void loseLife()      { if (--lives_ <= 0) gameOver.emit(); }

    void save(nlohmann::json& j) const override { j["score"] = score_; j["lives"] = lives_; }
    void load(const nlohmann::json& j) override { /* ... */ }
private:
    int score_ = 0, lives_ = 3;
};
```

Accès — **toujours** par l'arbre, typé, jamais de `GameState::instance()` :

```cpp
tree()->autoload<GameState>()->addScore(10);
listen(tree()->autoload<GameState>()->scoreChanged, [this](int s){ setLabel(s); });
```

Deux façons de fournir le state d'un autoload (au choix, même mécanisme) :
- **Code** : `registerAutoload<GameState>("GameState")` (state défini en C++).
- **Données** : une `.scene` listée dans la config projet comme autoload (state
  défini par composition dans l'éditeur). Data-driven, façon Godot Autoload.

Les events **transverses** (« un ennemi est mort », « pièce ramassée ») sont des
**signaux portés par un autoload** : propriétaire nommé, typé, inspectable — au
lieu d'un bus global anonyme.

---

## 4. Brique « Groupes & requêtes scopées »

Remplace la recherche par nom. Deux outils, et **rien d'autre** pour trouver un
nœud.

```cpp
// Node (core) : appartenance à des groupes (tags opt-in), maintenue par le tree.
class Node {
public:
    void addToGroup(const std::string& group);
    void removeFromGroup(const std::string& group);
    bool isInGroup(const std::string& group) const;

    // Requêtes SCOPÉES À SON PROPRE SOUS-ARBRE (donc « call down » uniquement) :
    template <typename T> T* getBehaviour() const;              // sur ce nœud (existe déjà)
    template <typename T> T* findBehaviourInChildren() const;   // DFS descendant
    template <typename T> T* getChildNode() const;              // 1er enfant de type T
};
```

```cpp
// Global, mais réduit à l'opt-in explicite (un nœud DOIT s'être tagué) :
tree()->group("enemies");          // tous les nœuds tagués "enemies"
tree()->firstInGroup("player");    // le player, sans le chercher par nom
```

Exemples :

```cpp
// Le player se déclare (dans son onReady) :
node()->addToGroup("player");

// Une tourelle vise le player sans couplage par nom :
if (Node* p = tree()->firstInGroup("player")) aimAt(p->worldTransform());

// Une bombe affecte tous les ennemis :
for (Node* e : tree()->group("enemies"))
    if (auto* h = e->findBehaviourInChildren<HealthBehaviour>()) h->damage(50);
```

> **Il n'existe pas** de `tree()->findByName(...)` ni d'accès global mutable à un
> nœud. C'est le garde-fou §5 rendu structurel.

### Bonus ergonomie : dépendances explicites entre behaviours

Pour éviter le couplage implicite (« je suppose qu'il y a un Rigidbody à côté ») :

```cpp
// Behaviour : récupère un behaviour frère, le crée s'il manque (à appeler en onReady).
template <typename T> T* requireBehaviour();
```

```cpp
void onReady() override { rb_ = requireBehaviour<RigidBodyBehaviour>(); }
```

---

## 5. Exemple complet : menu → niveau → game over

```
Autoload "GameState"           (persistant : score, lives, signaux)
MainMenu.scene
  └─ PlayButton (Behaviour)    → tree().changeScene("level1.scene")
Level1.scene
  ├─ Player.scene              addToGroup("player")
  │    ├─ Movement (Behaviour)
  │    └─ Health   (Behaviour) → émet died
  ├─ Coin.scene (×N)           AreaNode trigger → émet collected
  └─ ScoreLabel (Behaviour)    listen(GameState.scoreChanged)
GameOver.scene
```

Flux, sans un seul manager :

```cpp
// Pièce ramassée (trigger Area, scopé à elle-même) :
void onReady() override {
    listen(node()->getBehaviour<AreaBehaviour>()->entered, [this](Node* who){
        if (who->isInGroup("player")) {
            tree()->autoload<GameState>()->addScore(10);   // event transverse → autoload
            node()->queueFree();                            // se supprime
        }
    });
}

// Mort du player :
void onReady() override {
    listen(node()->findBehaviourInChildren<HealthBehaviour>()->died, [this]{
        tree()->autoload<GameState>()->loseLife();          // signal up → autoload
    });
}

// Réaction globale, portée par l'autoload :
class GameOverWatcher : public Behaviour {   // posé sur l'autoload GameState lui-même
    void onReady() override {
        listen(node()->getBehaviour<GameState>()->gameOver,
               [this]{ tree()->changeScene("gameover.scene"); });
    }
};

// UI de score :
void onReady() override {
    listen(tree()->autoload<GameState>()->scoreChanged, [this](int s){ setText(s); });
}
```

`GameState` survit aux 3 scènes ; personne ne « gère » personne ; tout va **bas
en appels, haut/loin en signaux**.

---

## 6. Intégration moteur (fichiers touchés) — *à implémenter après validation*

| Brique | Fichiers | Nature |
|---|---|---|
| Signaux | `core/Signal.hpp` (nouveau) ; `scene/Behaviour.hpp` (+`listen`, `connections_`) | header-only + petit ajout |
| SceneTree | `scene/SceneTree.{hpp,cpp}` (nouveau) ; `Engine` (possède le tree, applique le changeScene différé) ; `scene/Node` (+`tree()`) ; `scene/Scene` (+`tree_` back-ptr) | nouveau service |
| Autoloads | `SceneTree` (registre + persistance) ; réutilise `BehaviourRegistry`/`SceneSerializer` ; `project/Project` (liste d'autoloads data-driven) | dans le tree |
| Groupes/requêtes | `scene/Node` (+groupes, +requêtes scopées) ; `SceneTree` (index name→nœuds, maj sur add/remove/destroy) | ajout à Node |

Compatibilité : tout reste **nœuds + behaviours** ; aucune rupture du modèle
existant (physique, animation, UI, sérialisation). L'éditeur (multi-scènes via
`sceneOverride_`) garde sa gestion ; `tree()` cible le runtime de jeu.

---

## 7. Décisions à valider avant implémentation

1. **Signaux : typés (templates) vs nommés (chaîne + variant).** Reco : **typés**
   — sûrs à la compilation (un LLM obtient une erreur de build, pas un échec
   silencieux). Coût : connexions non éditables dans l'inspecteur (faites en code,
   ce qui est l'objectif pour un workflow LLM-first).
2. **Autoloads : enregistrement code (`registerAutoload<T>`) et/ou config projet
   (.scene).** Reco : les **deux**, même back-end.
3. **Pause & « tourner pendant la pause »** (menus). v1 = pause globale via
   `Time` ; les *process modes* à la Godot (nœuds qui tournent en pause) =
   itération ultérieure. À confirmer.
4. **`queueFree()` différé** (suppression sûre en fin de frame) à ajouter à `Node`
   en même temps que le changeScene différé (même mécanisme).
