# ApexSenseBridge — Fiche projet / Handoff Codex

**État au 31 août 2026**  
**Dernière base fonctionnelle à reprendre : `v0.2.2`**  
**Langage : C++20**  
**Plateforme cible : Windows 10/11 x64**  
**Licence actuelle du projet : GPL-3.0-or-later**

---

## 1. Résumé du projet

**ApexSenseBridge** a pour objectif de donner à une **Flydigi APEX 5** sur PC un ressenti aussi proche que possible d'une **Sony DualSense**, sans sacrifier les avantages de l'APEX 5 :

- disposition Xbox / boutons `A B X Y` ;
- connexion 2,4 GHz ;
- polling élevé de l'APEX 5 ;
- sticks, boutons et axes conservés sur le chemin d'entrée natif quand c'est possible ;
- gâchettes motorisées **FORCEADAPT** exploitées dynamiquement ;
- vibrations de poignées enrichies à partir du retour natif DualSense ;
- consommation CPU/RAM très faible ;
- latence de jeu minimale.

Le principe fondamental est de **ne pas transformer le programme en remapper complet si ce n'est pas nécessaire**.

Architecture cible préférée :

```text
                         CHEMIN DES INPUTS
APEX 5 physique ───── XInput natif ───────────────► Jeu
   │                                                   │
   │                                                   │
   │                         CHEMIN DU FEEDBACK        │
   │                                                   ▼
   │                                        DualSense virtuelle
   │                                                   │
   │                              output reports / haptic audio
   │                                                   │
   └───────────────◄──── ApexSenseBridge ◄─────────────┘
                         │               │
                         │               │
                         ▼               ▼
                    FORCEADAPT       vibrations
                     LT / RT          poignées
```

L'objectif est donc, dans le mode idéal :

- **0 ajout de latence aux sticks/boutons**, car ils restent en XInput natif ;
- la DualSense virtuelle sert principalement à recevoir les **effets de sortie** générés par les jeux ;
- ces effets sont convertis en commandes adaptées au matériel de l'APEX 5.

Un **mode de compatibilité “Full DualSense proxy”** pourra être ajouté pour les jeux qui refusent d'envoyer leurs effets à une DualSense virtuelle “silencieuse” côté inputs.

---

# 2. Objectif produit détaillé

## 2.1. Gâchettes adaptatives

Lorsqu'un jeu PC supporte nativement les adaptive triggers DualSense :

```text
Jeu
  ↓
Output report DualSense
  ↓
Décodage type + paramètres LT/RT
  ↓
Traduction vers le vocabulaire FORCEADAPT APEX 5
  ↓
Commandes Flydigi 81 / 82
  ↓
Moteurs LT / RT de l'APEX 5
```

La traduction ne doit pas être inventée arbitrairement.

Le projet **OpenFlydigi a déjà retranscrit la logique interne de Flydigi** utilisée pour convertir des effets DualSense vers les gâchettes APEX 5. Cette implémentation doit être utilisée comme référence principale.

## 2.2. Vibrations / ressenti haptique

La DualSense n'utilise pas les mêmes actionneurs que l'APEX 5.

Une DualSense possède des actionneurs haptiques de type voice-coil ; l'APEX 5 dispose de moteurs de vibration plus conventionnels. On ne pourra donc jamais reproduire physiquement 100 % du signal.

L'objectif est de **préserver au maximum la texture, les intensités et la dynamique**, plutôt que de faire une simple conversion “son fort = vibration forte”.

Pour les jeux qui émettent du vrai haptic audio DualSense :

```text
Haptic audio DualSense
      ↓
capture ch2 + ch3
      ↓
DSP temps réel
      ↓
séparation fréquentielle
      │
      ├── basses fréquences → gros moteur gauche APEX
      └── hautes fréquences → petit moteur droit APEX
```

OpenFlydigi possède déjà un DSP de référence matériellement testé :

- audio : 48 kHz ;
- DualSense 4 canaux :
  - ch0 = jack casque ;
  - ch1 = haut-parleur ;
  - ch2 = haptique gauche ;
  - ch3 = haptique droite ;
- crossover de référence : **150 Hz** ;
- `GATE = 0.015` ;
- courbe compressive `CURVE = 0.7` ;
- décimation de référence : `8`.

Ce DSP doit être **porté en C++ et optimisé**, pas réinventé sans mesure.

## 2.3. Jeux sans haptique DualSense

Ordre de priorité des sources de feedback :

1. **Haptique DualSense natif** si disponible.
2. **Output rumble DualSense / XInput** si disponible.
3. **Analyse audio du jeu** uniquement comme fallback/enrichissement optionnel.

L'analyse audio générique est une fonctionnalité ultérieure, pas la première brique.

## 2.4. Prompts Xbox à l'écran

Préférence UX :

```text
A / B / X / Y
```

plutôt que :

```text
✕ / ○ / □ / △
```

Le mode hybride doit essayer de garder l'APEX 5/XInput comme contrôleur actif côté entrée pour que les jeux conservent leurs prompts Xbox.

**Limite connue :** certains jeux choisissent leurs prompts simplement parce qu'une DualSense est connectée. Il faudra donc tester titre par titre et prévoir des modes de compatibilité.

---

# 3. Contraintes de performance

Le programme doit être conçu comme un service léger, pas comme une grosse application Electron.

## Cibles

### Chemin input

En mode hybride :

- l'application **ne doit pas être dans le chemin des sticks/boutons** ;
- latence ajoutée aux inputs : idéalement **0 ms**.

Si un mode Full DualSense proxy est nécessaire :

- latence logicielle additionnelle visée : **< 2–3 ms**.

### Feedback

- adaptive triggers : effet appliqué idéalement en **1–5 ms** ;
- éviter toute file d'attente qui accumule des rapports obsolètes ;
- coalescer les changements lorsqu'un nouveau rapport rend l'ancien inutile.

### CPU

Machine de référence utilisateur : **Intel Core i5-10400F**.

Budget cible :

- bridge triggers / rumble : **< 1 % CPU moyen** ;
- haptique audio activé : idéalement **< 2–3 % CPU** ;
- pas de busy-loop.

### RAM

- cible : **< 100 Mo** ;
- idéalement nettement moins pour le core/service.

---

# 4. Stack technique

## Langage

**C++20**

Motifs :

- faible overhead ;
- accès direct Win32 / HID / audio ;
- contrôle des allocations et du threading ;
- intégration naturelle avec le code Windows existant ;
- SteamlessController, une des références Windows principales, est déjà en C++ ;
- évite une runtime lourde en arrière-plan.

## Build

**CMake** est actuellement utilisé comme système de build.

Ce n'est pas une dépendance runtime : uniquement une dépendance développeur.

La base actuelle supporte :

- Visual Studio Build Tools 2026 ;
- Visual Studio Build Tools 2022.

Environnement Windows déjà utilisé avec succès :

```text
Visual Studio Build Tools 2026
MSBuild 18.9.1
MSVC compiler 19.51.36256.0
MSVC tools 14.51.36231
Windows SDK 10.0.26100.0
x64
```

Build manuel :

```powershell
cmake -S . -B .\build-win -G "Visual Studio 18 2026" -A x64
cmake --build .\build-win --config Release
```

Le script fourni :

```powershell
.\scripts\build-windows.ps1
```

détecte désormais VS Build Tools 2026 puis 2022.

---

# 5. Repositories GitHub de référence

## 5.1. OpenFlydigi — référence PRIORITAIRE pour le matériel APEX 5

**Repository :**  
https://github.com/mkaliaha/openflydigi

**Licence : MIT**

C'est la référence technique principale pour toute la partie Flydigi/APEX 5.

Le projet a reverse-engineeré Flydigi Space Station et a validé de nombreuses commandes directement sur APEX 5.

### Fichiers particulièrement importants

#### `PROTOCOL.md`

https://github.com/mkaliaha/openflydigi/blob/main/PROTOCOL.md

Contient :

- framing HID ;
- commandes ;
- FORCEADAPT ;
- commandes 81 / 82 ;
- comportement filaire / dongle ;
- identités de périphériques ;
- commandes de vibration ;
- détails sur les profils.

#### `flydigi/device.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/device.py

Référence pour :

- `VID = 0x37D7` ;
- famille PID contrôleur ;
- interface vendor ;
- rapport HID ;
- ACK ;
- transport ;
- détection et sécurité.

Référence mesurée :

```text
VID = 0x37D7
APEX 5 connu = PID 0x2501
famille pad = PID >> 12 == 2
vendor usage descriptor = 0xFFA0
report OUT = 0x03
report reply = 0x04
packet = 32 bytes
```

#### `flydigi/effects.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/effects.py

Référence pour :

- `Normal` ;
- `Race` ;
- `Recoil/Rattle` ;
- `Sniper/Breakthrough` ;
- `Lock` ;
- command 82 / vibration bind ;
- commandes de vibration poignées.

#### `flydigi/ds5.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/ds5.py

Codec DualSense :

- input reports ;
- output reports ;
- flags ;
- rumble ;
- adaptive triggers ;
- USB report `0x02` ;
- BT report `0x31`.

#### `flydigi/relay.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/relay.py

**Fichier critique.**

Il contient la traduction exacte :

```text
DualSense trigger effect
          ↓
translate_ds5()
          ↓
Flydigi FORCEADAPT mode + params
```

Cette table vient de la logique Flydigi `PS5DataManager.ProcessDataWithResult`, elle ne doit donc pas être remplacée par une approximation “plus propre”.

Types DualSense explicitement gérés dans cette logique :

```text
1
2
5
6
33
37
38
```

La conversion droite/gauche n'est pas toujours symétrique.

Conserver ces bizarreries tant qu'elles sont conformes au comportement Flydigi.

#### `flydigi/haptics.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/haptics.py

Référence pour convertir le vrai haptic audio DualSense vers les deux moteurs de poignée APEX 5.

**Important :** le mapping est fréquentiel, pas gauche → gauche / droite → droite.

#### `flydigi/dsmode.py`

https://github.com/mkaliaha/openflydigi/blob/main/flydigi/dsmode.py

Architecture du mode DualSense complet sous Linux et contraintes d'usage.

OpenFlydigi a déjà démontré qu'un mode DualSense générique peut fonctionner avec des jeux que Flydigi ne connaît pas à l'avance.

---

## 5.2. SteamlessController DualSense fork — référence Windows

**Repository :**  
https://github.com/david419kr/steamless-controller-XB-PS-NS

**Licence du code principal : MIT**

Ce repo est important car il montre déjà sur Windows :

- contrôleur virtuel DualSense ;
- création via VIIPER ;
- lecture/émission de rapports DualSense ;
- adaptive trigger signals ;
- Haptic Feedback best effort ;
- HidHide ;
- application tray légère ;
- Steam Input / contrôleur virtuel.

Il cible initialement le Steam Controller, donc la partie matériel devra être remplacée par notre backend APEX 5.

### Ce qu'on souhaite réutiliser conceptuellement

```text
Virtual DualSense
VIIPER sidecar
output feedback capture
driver lifecycle
HidHide handling
Windows service/tray architecture
```

Le but n'est pas de conserver sa couche Steam Controller.

---

## 5.3. VIIPER — backend de périphérique virtuel

**Repository :**  
https://github.com/Alia5/VIIPER

**Licence : GPLv3**

VIIPER est utilisé par SteamlessController pour créer les périphériques virtuels.

À évaluer comme backend principal pour :

- DualSense virtuelle USB ;
- remontée des output reports ;
- potentiel support des interfaces supplémentaires nécessaires au haptic audio.

SteamlessController utilise une **version patchée** de VIIPER, car le VIIPER stock ne fournit pas tous les rapports nécessaires au support DualSense avancé.

### Tâche de recherche importante

Déterminer précisément :

- si la version patchée expose seulement HID ;
- ou si elle permet également une représentation USB suffisamment complète pour le **haptic audio DualSense** ;
- comment récupérer les output reports sans les faire passer par la couche input de notre APEX.

---

## 5.4. usbip-win2 — USB virtuel Windows

**Repository :**  
https://github.com/vadimgrn/usbip-win2

Utilisé par SteamlessController / VIIPER.

C'est un client USB/IP Windows avec driver UDE.

À conserver comme dépendance potentielle si le virtual DualSense doit être présenté comme un véritable périphérique USB composite.

Attention : l'installation d'un driver doit rester optionnelle et clairement documentée.

---

## 5.5. HidHide — optionnel

**Repository :**  
https://github.com/nefarius/HidHide

But :

- empêcher certains jeux/Steam de voir simultanément deux contrôleurs ;
- cacher sélectivement le périphérique physique tout en gardant le virtuel visible.

**Ne pas activer automatiquement au début.**

Le mode hybride cherche justement à garder le chemin XInput physique actif. HidHide sera un outil de compatibilité par jeu, pas une hypothèse globale.

---

# 6. Licence du projet

La base actuelle `ApexSenseBridge v0.2.2` est :

```text
GPL-3.0-or-later
```

C'est un choix cohérent avec la possibilité d'intégrer ou distribuer des composants GPL comme VIIPER.

OpenFlydigi est MIT et peut être utilisé comme référence/portage dans un projet GPL, mais :

- conserver les copyrights/attributions nécessaires si du code est directement porté ;
- ajouter un fichier `NOTICE` / `THIRD_PARTY_NOTICES.md` ;
- documenter les commits upstream utilisés comme références.

SteamlessController est MIT pour le code principal, mais son bundle contient notamment VIIPER GPLv3.

**Ne pas modifier la licence sans analyser les dépendances réellement embarquées.**

---

# 7. État actuel du code ApexSenseBridge

## Version à reprendre : v0.2.2

Structure :

```text
ApexSenseBridge-v0.2.2/
│
├── CMakeLists.txt
├── LICENSE
├── README.md
├── CHANGELOG.md
│
├── scripts/
│   ├── build-windows.ps1
│   └── test-rt.bat
│
├── src/
│   ├── main.cpp
│   │
│   ├── core/
│   │   ├── DeviceInfo.h
│   │   ├── TriggerEffect.h
│   │   └── TriggerResetGuard.h
│   │
│   ├── flydigi/
│   │   ├── Apex5Protocol.h
│   │   ├── Apex5Protocol.cpp
│   │   ├── Apex5Device.h
│   │   └── Apex5Device.cpp
│   │
│   └── platform/
│       ├── HidTransport.h
│       ├── UnsupportedHidTransport.cpp
│       └── windows/
│           └── WindowsHidTransport.cpp
│
└── tests/
    └── test_protocol.cpp
```

## Principes d'architecture déjà posés

Le projet a été volontairement découpé en couches :

### `core/`

Structures métier / sécurité.

Aucune connaissance Win32.

### `flydigi/`

Connaît :

- protocole APEX 5 ;
- FORCEADAPT ;
- device facade.

Ne doit pas dépendre directement de SetupAPI.

### `platform/windows/`

Connaît :

- SetupAPI ;
- HID API Windows ;
- handles ;
- `WriteFile` ;
- `HidD_SetOutputReport`.

### `main.cpp`

CLI mince.

Ne doit pas accumuler la logique hardware au fur et à mesure du projet.

**Conserver cette séparation.**

---

# 8. Ce qui est déjà implémenté

## 8.1. Génération du protocole FORCEADAPT

Le code sait générer des rapports pour :

- `Normal` ;
- `Race` ;
- effet rattle/recoil ;
- effet breakthrough/sniper ;
- `Lock` ;
- mode vibration conservé comme non validé.

## 8.2. Framing corrigé

Le tout premier prototype utilisait une mauvaise hypothèse de report ID.

La version actuelle suit la collection vendor mesurée par OpenFlydigi :

```text
byte 0  = 0x03
byte 1  = 0x5A
byte 2  = 0xA5
byte 3  = Command ID
byte 4  = payload length
byte 5+ = payload
total   = 32 bytes
```

Pour les commandes live APEX 5 :

```text
Command 81 = SetForceTrigger
Command 82 = SyncWithGrip
```

## 8.3. Sides

```text
Left  = 1
Right = 2
Both  = 3
```

**Attention : `Both = 3` est ignoré par l'APEX 5 tout en pouvant ACKer.**

Toujours envoyer une commande distincte par trigger.

## 8.4. Modes actuellement modélisés

```text
0 = Normal
1 = Race
2 = SDK "Sniper"  / comportement UI Flydigi "Recoil" / rattle
3 = SDK "Recoil"  / comportement UI Flydigi "Sniper" / breakthrough
4 = Lock
5 = Vibration live, sous-documenté
```

Ne pas “corriger” le nommage mode 2/3 sans comprendre le décalage historique Flydigi.

## 8.5. Transport HID Windows

Implémenté avec :

- `HidD_GetHidGuid`;
- `SetupDiGetClassDevsW`;
- `SetupDiEnumDeviceInterfaces`;
- `SetupDiGetDeviceInterfaceDetailW`;
- `CreateFileW`;
- `HidD_GetAttributes`;
- `HidD_GetManufacturerString`;
- `HidD_GetProductString`;
- `HidD_GetPreparsedData`;
- `HidP_GetCaps`.

Écriture :

1. `WriteFile`;
2. fallback `HidD_SetOutputReport`.

## 8.6. Filtre de sécurité actuel

Un périphérique est considéré comme candidat uniquement si :

```cpp
vendorId == 0x37D7
(productId >> 12) == 2
usagePage == 0xFFA0
```

Le but est d'éviter absolument d'envoyer une commande FORCEADAPT sur un périphérique HID au hasard.

## 8.7. CLI

```text
list
test-rt [index]
clear [index]
dry-run
```

### `dry-run`

N'écrit pas sur le matériel.

### `test-rt`

Prévu pour appliquer un `Race` léger sur RT :

```text
start = 70
resistance = 30 / 255
duration = environ 1,5 s
```

Puis remettre LT/RT en `Normal`.

## 8.8. Sécurité trigger

Présence d'un `TriggerResetGuard` RAII.

Le test tente de remettre les deux triggers en `Normal` :

- fin normale ;
- erreur C++ ;
- `Ctrl+C` propre.

Impossible de garantir le cleanup après :

- power loss ;
- BSOD ;
- `TerminateProcess`;
- crash kernel/driver.

## 8.9. Tests

Tests unitaires du protocole via CTest.

Vérification faite sur la base v0.2.2 :

```text
100% tests passed
0 tests failed
```

---

# 9. Historique des versions déjà réalisées

## v0.1

Preuve de concept.

- construction commandes FORCEADAPT ;
- aucune vraie communication HID ;
- mauvais report ID supposé au départ ;
- utile uniquement pour démarrer la structure.

## v0.2

Refactor important.

- architecture core / flydigi / platform ;
- framing corrigé ;
- SetupAPI/HID Windows ;
- filtre de périphérique ;
- `list`, `test-rt`, `clear`, `dry-run` ;
- cleanup trigger ;
- tests.

## v0.2.1

Corrections build.

Le premier script avait forcé Visual Studio 2022 alors que la machine utilisait Build Tools 2026.

## v0.2.2

État actuel.

- fix `<iterator>` pour `std::back_inserter` ;
- build script détecte VS 2026 ou 2022 ;
- suppression automatique du vieux cache CMake si générateur différent ;
- compilation Windows réussie.

---

# 10. Validation matérielle Windows — RÉSOLUE

Mise à jour du 31 août 2026 : l'ancienne absence d'interface était causée par
une manette endormie ou par un diagnostic insuffisant. La collection Windows
du dongle a maintenant été identifiée avec certitude :

```text
VID:PID       37D7:2501
Produit       Flydigi APEX5 Wireless
Collection    MI_02 / COL01
UsagePage     FFA0
Usage         0001
Input report  32 octets
Output report 32 octets
```

La commande read-only `0x01` a répondu :

```text
Apex 5 / k5 / DeviceType 128 / dongle raw 2
adaptive triggers: yes
```

Le test doux `test-rt` a ensuite appliqué un effet Race physiquement ressenti
sur RT pendant environ 1,5 seconde, puis LT et RT ont été remis à Normal avec
succès. Le chemin `Windows -> HID vendor -> FORCEADAPT APEX 5` est donc validé
sur le dongle 2,4 GHz.

---

# 11. Diagnostic HID Windows — TERMINÉ

Les commandes `diagnose`, `diagnose --all-hid` et `diagnose --json` sont
implémentées. Elles ont permis l'identification ci-dessus et restent
strictement sans commande d'effet matériel.

## 11.1. Créer un diagnostic HID Windows READ-ONLY

Avant toute nouvelle écriture matérielle, ajouter :

```text
ApexSenseBridge.exe diagnose
```

Cette commande ne doit faire **aucun WriteFile / SetOutputReport / Feature write**.

Elle doit afficher toutes les interfaces HID pertinentes, avec au minimum :

```text
index
device path complet
VID
PID
manufacturer
product
serial si disponible
UsagePage
Usage
InputReportByteLength
OutputReportByteLength
FeatureReportByteLength
```

Ne pas filtrer uniquement sur la règle stricte.

Afficher les périphériques qui remplissent AU MOINS un des critères :

```text
VID == 0x37D7
OU path contient "vid_37d7"
OU manufacturer/product contient Flydigi
OU manufacturer/product contient APEX
OU usage page vendor-defined
```

Prévoir aussi :

```text
--all-hid
--json
```

pour faciliter les diagnostics.

## 11.2. Enrichir l'énumération SetupAPI

Pour chaque interface pertinente, récupérer si possible :

- `SP_DEVINFO_DATA`;
- hardware IDs ;
- compatible IDs ;
- device instance ID ;
- parent physique ;
- friendly name ;
- class ;
- interface number `MI_xx` depuis le path.

Objectif :

comprendre quelles top-level HID collections appartiennent au même dongle/APEX.

## 11.3. Tester deux chemins

Le diagnostic devra être lancé :

1. APEX 5 via dongle 2,4 GHz ;
2. APEX 5 branchée directement en USB.

Comparer les résultats.

## 11.4. Ne PAS lancer FORCEADAPT avant identification

La prochaine commande physique ne doit être réactivée qu'après avoir confirmé précisément la collection vendor.

---

# 12. Sécurité hardware à améliorer avant le premier vrai write

Le filtre VID/PID/UsagePage est utile mais insuffisant comme garde définitive.

Ajouter une vérification d'identité APEX 5 via la commande d'information Flydigi :

```text
Command 0x01 = get info / identity path
```

S'inspirer de :

```text
openflydigi/flydigi/identity.py
openflydigi/flydigi/device.py
```

Workflow recommandé :

```text
ouvrir interface candidate
      ↓
commande READ/identity
      ↓
vérifier réponse
      ↓
vérifier DeviceCode APEX 5 / capacité adaptive_triggers
      ↓
autoriser seulement ensuite command 81/82
```

Le projet ne doit pas faire confiance uniquement au nom Windows du périphérique.

---

# 13. Transport à compléter

La v0.2.2 ne fait encore que l'écriture.

OpenFlydigi gère de vrais échanges commande + ACK.

À implémenter :

```text
write report
    ↓
wait response
    ↓
report 0x04
    ↓
valider echoed command id
    ↓
valider success flag
```

Référence OpenFlydigi :

- report reply `0x04`;
- command ID répercutée dans la réponse ;
- flag succès.

Créer une API du type :

```cpp
struct CommandReply {
    bool ack;
    uint8_t commandId;
    uint8_t status;
    std::array<uint8_t, ...> raw;
};

CommandResult HidTransport::exchange(
    std::span<const uint8_t> request,
    std::chrono::milliseconds timeout
);
```

Éviter que la couche `Apex5Device` manipule directement des handles Windows.

---

# 14. Concurrence avec Flydigi Space Station

Space Station peut envoyer ses propres commandes au contrôleur.

OpenFlydigi indique que certains chemins peuvent réécrire les effets très fréquemment.

Pendant les premiers tests :

```text
fermer complètement Flydigi Space Station
```

À terme, deux options :

### A. ApexSenseBridge devient le seul propriétaire des adaptive triggers

Plus simple.

### B. Coexistence

Plus complexe.

Il faudra empêcher les deux programmes de se battre sur l'état des moteurs.

**Ne pas utiliser un lock exclusif kernel qui casserait Steam/SDL ou les interfaces normales du pad.**

---

# 15. Phase DualSense virtuelle

Une fois le chemin APEX 5 validé :

## Milestone 1

Créer une DualSense virtuelle sous Windows.

## Milestone 2

Afficher un compteur des output reports reçus :

```text
virtual_ds_connected = yes
output_reports = 1234
trigger_reports = 85
rumble_reports = 512
```

Aucun contrôle APEX ne doit être modifié à cette étape.

## Milestone 3

Décoder `ds5.py::parse_output()` en C++.

Données importantes :

```text
USB output report ID = 0x02
Bluetooth output report ID = 0x31
```

Capturer :

- motor left ;
- motor right ;
- left adaptive trigger ;
- right adaptive trigger.

---

# 16. Traduction adaptive triggers

Une fois les reports DualSense capturés :

**Porter la logique `openflydigi/flydigi/relay.py::translate_ds5()`**.

Ne pas essayer de créer une conversion générique tant que la traduction Flydigi connue fonctionne.

Pipeline :

```text
DualSense TriggerEffect
      ↓
translate_ds5()
      ↓
(side, mode, params)
      ↓
Apex5Protocol
      ↓
command 81
```

Points critiques :

- type inconnu : ne pas forcément envoyer `Normal`;
- certaines commandes inconnues doivent laisser l'état existant ;
- conversion droite/gauche asymétrique ;
- valeurs “étranges” présentes volontairement.

Créer des tests unitaires à partir de la table OpenFlydigi.

---

# 17. Rumble standard

Le protocole APEX possède également une commande de vibration :

```text
Command 0x12
```

À implémenter après FORCEADAPT stable.

Ne pas attendre 100 ms d'ACK sur chaque update de rumble en streaming.

Architecture :

```text
dernier niveau désiré
      ↓
coalescing
      ↓
update moteur
```

La boucle haptique ne doit pas accumuler des dizaines d'anciens rapports.

---

# 18. Haptic audio — objectif principal “feeling DualSense”

Cette phase est importante pour obtenir beaucoup de niveaux et de textures de vibration.

## 18.1. Ne pas faire une simple FFT à 48 kHz dans une boucle lourde

Les moteurs APEX ne peuvent pas reproduire la bande passante d'une DualSense.

Utiliser un DSP léger.

Référence OpenFlydigi :

```text
48 kHz input
ch2 + ch3 haptic
one-pole low-pass
crossover 150 Hz
low band → moteur gauche
high band → moteur droit
RMS
gate
curve
0..255
```

## 18.2. Implémentation C++

Créer par exemple :

```text
src/haptics/
    HapticProcessor.h
    HapticProcessor.cpp
    HapticConfig.h
```

API souhaitable :

```cpp
struct MotorLevels {
    uint8_t lowFrequencyMotor;
    uint8_t highFrequencyMotor;
};

MotorLevels HapticProcessor::process(std::span<const float> interleaved);
```

Pas d'allocation dans `process()`.

## 18.3. Fréquence d'update moteur

Il n'est pas nécessaire d'écrire 48 000 fois/s au pad.

Décimer/analyser puis actualiser les moteurs à une fréquence cohérente, à mesurer.

Point de départ :

```text
100 à 250 updates/s
```

puis profiler.

---

# 19. Virtual USB / haptic audio Windows : inconnue importante

C'est un des risques techniques majeurs.

Il faut déterminer comment un jeu PC envoie son haptique DualSense :

- certains effets passent par les output reports HID ;
- certains jeux utilisent le périphérique audio USB DualSense.

Pour ces derniers, une simple manette HID virtuelle peut être insuffisante.

### Recherche à faire dans SteamlessController + VIIPER

Vérifier si la version patchée de VIIPER utilisée par :

https://github.com/david419kr/steamless-controller-XB-PS-NS

expose un périphérique USB composite suffisamment complet pour que le jeu ouvre une sortie audio haptique.

Si oui :

- réutiliser/capturer ce flux.

Si non :

- étudier le chemin USB/IP / UDE ;
- ou une sortie audio virtuelle dédiée.

**Ne pas écrire un driver Windows custom tant qu'une solution userspace + driver existant suffit.**

---

# 20. Mode hybride et compatibilité

## Mode Hybrid — préféré

```text
APEX XInput physique → inputs jeu

DualSense virtuelle → feedback uniquement
```

Avantages :

- aucune latence input ajoutée ;
- prompts Xbox potentiellement conservés ;
- 1000 Hz APEX non perturbé.

Problème :

certains jeux peuvent refuser d'envoyer du feedback à une DualSense qui ne fournit jamais d'inputs.

## Mode Full DualSense Proxy — fallback

```text
APEX input
    ↓
ApexSenseBridge
    ↓
DualSense virtuelle
    ↓
jeu
```

À utiliser seulement si nécessaire.

Dans ce mode, il faudra gérer :

- mapping APEX Xbox layout → positions DualSense ;
- gyro ;
- touchpad virtuel / fallback ;
- suppression du double input ;
- éventuellement HidHide.

---

# 21. Steam Input

Steam Input peut :

- masquer le contrôleur ;
- convertir en Xbox ;
- empêcher un jeu de communiquer nativement avec la DualSense virtuelle.

Pour les jeux natifs DualSense :

```text
Steam Input doit souvent être désactivé par jeu.
```

Ne pas désactiver Steam Input globalement sans demander à l'utilisateur.

Prévoir dans le futur un diagnostic :

```text
Game supports native DualSense
Steam Input appears active
→ warning
```

---

# 22. Gestion des prompts Xbox / PlayStation

Ne jamais promettre que les prompts Xbox seront conservés dans 100 % des jeux.

Prévoir une politique par jeu :

```text
Preferred:
Hybrid / Xbox prompts

Fallback:
Full DS proxy / PlayStation prompts
```

Une future base de compatibilité pourra mémoriser :

```json
{
  "game": "Example",
  "mode": "hybrid",
  "steamInput": false,
  "xboxPrompts": true,
  "nativeTriggers": true,
  "nativeHaptics": true
}
```

---

# 23. Architecture logicielle cible

Proposition :

```text
src/
├── app/
│   ├── Application.cpp
│   ├── ServiceState.cpp
│   └── GameSession.cpp
│
├── core/
│   ├── DeviceInfo.h
│   ├── Result.h
│   ├── Logging.h
│   └── ShutdownToken.h
│
├── flydigi/
│   ├── Apex5Protocol.*
│   ├── Apex5Device.*
│   ├── Apex5Identity.*
│   ├── Apex5Effects.*
│   └── Apex5Rumble.*
│
├── dualsense/
│   ├── DualSenseReports.*
│   ├── TriggerTranslator.*
│   ├── VirtualDualSense.*
│   └── FeedbackRouter.*
│
├── haptics/
│   ├── HapticProcessor.*
│   └── AudioCapture.*
│
├── platform/
│   └── windows/
│       ├── WindowsHidTransport.*
│       ├── WindowsDeviceEnumerator.*
│       ├── WindowsAudioCapture.*
│       └── WindowsProcessDetection.*
│
├── third_party/
│   └── ...
│
└── cli/
    └── main.cpp
```

Ne pas créer toutes ces classes immédiatement.

Évoluer milestone par milestone.

---

# 24. Règles de qualité de code demandées

Le code doit rester lisible même si le projet devient complexe.

## Principes

- pas de spaghetti ;
- responsabilités courtes ;
- RAII pour tous les handles ;
- pas de raw `new/delete` hors wrapper exceptionnel ;
- pas de gros `main.cpp` ;
- pas de protocole codé dans l'UI ;
- pas de Win32 dans la logique métier ;
- pas de `sleep()` arbitraires quand une attente événementielle est possible ;
- éviter les singletons ;
- éviter les globales mutables ;
- erreurs explicites ;
- logs exploitables.

## C++

Compiler avec :

```text
/W4
/permissive-
```

Ajouter progressivement :

- `clang-tidy` ;
- static analysis MSVC ;
- sanitizers lorsque compatibles.

## Protocoles

Aucun magic byte opaque dans plusieurs fichiers.

Exemple :

```cpp
namespace flydigi::protocol {
constexpr uint16_t kVendorId = 0x37D7;
constexpr uint8_t kSetForceTrigger = 81;
}
```

## Tests

Chaque conversion doit avoir des tests.

Minimum :

```text
tests/
    test_apex_protocol.cpp
    test_ds5_output_parser.cpp
    test_trigger_translation.cpp
    test_haptic_processor.cpp
```

Le hardware doit être testé séparément des tests unitaires.

---

# 25. Threading / temps réel léger

Éviter le modèle :

```text
while(true) {
    poll_everything();
    sleep(1ms);
}
```

Préférer :

- overlapped I/O Windows ;
- événements ;
- wait handles ;
- ring buffers courts ;
- coalescing.

Proposition :

```text
Thread A:
Virtual DualSense output reports
          ↓
FeedbackRouter

Thread B:
APEX vendor I/O queue
          ↓
serialisation des commandes

Thread C (seulement haptics):
audio capture + DSP
```

Les outputs moteurs peuvent être coalescés :

si 10 valeurs arrivent plus vite que le hardware ne peut les appliquer, la valeur la plus récente est généralement la plus utile.

---

# 26. CPU optimization checklist

Pour respecter le i5-10400F :

- aucune boucle 1000 Hz inutile si aucun événement ;
- pas d'analyse FFT massive si un filtre IIR suffit ;
- buffers préalloués ;
- pas de logs à chaque frame ;
- logs debug désactivables ;
- update trigger uniquement quand l'effet change ;
- haptic motor update limité à la fréquence réellement utile ;
- aucune GUI lourde obligatoire ;
- tray UI séparée du core/service si nécessaire.

---

# 27. UX future

Une fois le core stable, ajouter une petite application tray native.

Écran possible :

```text
ApexSenseBridge
────────────────────────────
APEX 5          Connected
Input mode      Native XInput
DualSense       Active
Game            Spider-Man
Adaptive        Native → FORCEADAPT
Haptics         Native audio
CPU             0.6 %
────────────────────────────
[ Disable bridge ]
[ Settings ]
```

Réglages utilisateur :

```text
Haptic intensity
Impact
Texture/finesse
Trigger strength scale
Prefer Xbox prompts
Audio fallback
```

Mais **pas d'UI avant de valider le hardware et la virtual DualSense**.

---

# 28. Sécurité / stabilité

## Au démarrage

- ne jamais envoyer une commande à un périphérique non identifié ;
- vérifier identité/capabilities ;
- mémoriser état de connexion.

## À l'arrêt

- remettre LT et RT Normal ;
- arrêter rumble ;
- détacher proprement le périphérique virtuel.

## Reconnexion

L'APEX 5 peut disparaître du bus lorsqu'elle dort.

Le programme final doit :

```text
APEX disconnected
→ garder le virtual controller dans un état sûr
→ relâcher tous les inputs si Full Proxy
→ ne pas crasher
→ détecter la reconnexion
→ rouvrir vendor HID
→ restaurer l'état nécessaire
```

OpenFlydigi `relay.py::PadLink` est une excellente référence pour ce comportement.

---

# 29. Risques techniques connus

### Risque A — interface vendor Windows

**Résolu sur dongle 2,4 GHz.** La collection validée est `MI_02/COL01`,
UsagePage `0xFFA0`, rapports entrée/sortie de 32 octets. Le chemin USB direct
reste à documenter mais ne bloque plus le mode principal.

### Risque B — feedback sur DualSense virtuelle sans input

Le mode hybride doit être testé empiriquement.

### Risque C — vrai haptic audio

Il peut nécessiter une représentation USB/audio plus complète qu'un simple gamepad virtuel.

### Risque D — prompts

Varient selon les jeux.

### Risque E — Steam Input

Peut intercepter les contrôleurs.

### Risque F — anti-cheat

Éviter :

- injection DLL ;
- lecture mémoire jeu ;
- hooks invasifs.

Le core doit fonctionner comme un périphérique/bridge normal.

### Risque G — Space Station

Peut écraser les effets si elle tourne.

---

# 30. Critères de réussite MVP

Le MVP est atteint si :

1. ✅ l'APEX 5 est détectée en 2,4 GHz ;
2. ✅ l'identité APEX 5 est validée (`k5`, DeviceType 128) ;
3. ✅ le bridge applique un effet FORCEADAPT réel sur RT ;
4. ✅ le bridge remet automatiquement LT/RT à Normal ;
5. une DualSense virtuelle est visible dans Windows ;
6. un jeu natif DualSense lui envoie des output reports ;
7. les effets adaptatifs du jeu sont traduits en FORCEADAPT ;
8. les inputs APEX restent directs en XInput dans le mode hybride ;
9. aucune sensation de latence supplémentaire sur sticks/boutons ;
10. CPU moyen du bridge triggers reste sous ~1 % sur la machine de référence.

---

# 31. Critères de réussite “DualSense feeling”

Version avancée considérée réussie si :

- effets d'arme adaptatifs ;
- accélérateur/frein avec résistance ;
- breakpoints ;
- vibrations de trigger ;
- gros impacts ;
- petites textures ;
- intensités progressives et non binaires ;
- haptic audio natif transformé en signal cohérent pour les moteurs APEX ;
- faible vibration parasite en périodes silencieuses ;
- pas de vibration permanente issue du bruit de fond audio ;
- préférence Xbox prompts dans les jeux compatibles avec le mode hybride.

---

# 32. Tests jeux recommandés

Pour valider le native DualSense path, choisir d'abord des jeux PC connus pour utiliser :

- adaptive triggers ;
- haptic feedback natif.

Références déjà citées/testées dans les projets upstream :

```text
Marvel's Spider-Man Remastered
Ratchet & Clank: Rift Apart
Deathloop
```

L'objectif initial n'est pas de créer des profils spécifiques.

Si un jeu supporte nativement DualSense, le bridge devrait fonctionner **sans profil par jeu**.

---

# 33. Premier ticket Codex — TERMINÉ SUR DONGLE

Le diagnostic, l'identité `0x01`, la garde d'écriture et le test FORCEADAPT
physique ont été réalisés. La variante USB directe reste à documenter mais ne
bloque plus le chemin produit prioritaire en 2,4 GHz.

## Titre

**Windows HID diagnostics + safe APEX 5 identity discovery**

## Objectif

Partir de la v0.2.2.

Ne pas implémenter de Virtual DualSense tant que le hardware path n'est pas validé.

## Travail

1. Auditer l'architecture existante.
2. Conserver la séparation `core / flydigi / platform`.
3. Ajouter une énumération HID diagnostic read-only.
4. Ajouter `diagnose`, `diagnose --all-hid`, `diagnose --json`.
5. Afficher :
   - path ;
   - VID/PID ;
   - manufacturer/product/serial ;
   - usage page/usage ;
   - report lengths ;
   - hardware/device instance IDs ;
   - interface number / parent quand possible.
6. Ajouter tests pour le filtrage et le formatage.
7. Ne pas envoyer de commande hardware dans `diagnose`.
8. Préparer ensuite une classe `Apex5Identity` mais ne pas activer les writes avant validation utilisateur.

## Definition of Done

Le programme doit nous permettre de savoir précisément :

```text
quelle interface Windows correspond au vendor channel de l'APEX 5
```

en dongle **et** en USB.

---

# 34. Ancien prompt de diagnostic — ARCHIVE

> **Ne plus utiliser ce prompt comme état courant.** Il est conservé pour
> retracer le ticket qui a mené à la validation du 31 août 2026. Les mentions
> d'interface introuvable et d'absence de test physique ci-dessous sont
> désormais obsolètes.

```text
Tu reprends un projet Windows C++20 appelé ApexSenseBridge.

Lis d'abord PROJECT_BRIEF_CODEX.md intégralement puis inspecte le code existant avant de modifier quoi que ce soit.

Objectif global :
donner à une Flydigi APEX 5 un feeling aussi proche que possible d'une DualSense sur PC, en conservant autant que possible l'APEX en XInput natif pour les inputs afin de ne pas ajouter de latence, et en utilisant une DualSense virtuelle essentiellement pour récupérer le feedback natif des jeux (adaptive triggers + haptics), qui sera ensuite traduit vers FORCEADAPT et les moteurs de l'APEX.

Contraintes majeures :
- C++20.
- Windows 10/11 x64.
- code propre, modulaire, pas de spaghetti.
- RAII.
- pas de logique Win32 dans les couches métier.
- pas de busy polling.
- pas de remapping input dans le mode hybride.
- ne jamais écrire sur une interface HID qui n'a pas été identifiée avec certitude.
- ne pas implémenter une GUI maintenant.
- préserver la licence GPL-3.0-or-later et les notices upstream.

Repositories de référence prioritaires :
1. https://github.com/mkaliaha/openflydigi
   - PROTOCOL.md
   - flydigi/device.py
   - flydigi/effects.py
   - flydigi/ds5.py
   - flydigi/relay.py
   - flydigi/haptics.py
2. https://github.com/david419kr/steamless-controller-XB-PS-NS
3. https://github.com/Alia5/VIIPER
4. https://github.com/vadimgrn/usbip-win2
5. https://github.com/nefarius/HidHide

État actuel :
- v0.2.2 compile correctement sous Visual Studio Build Tools 2026.
- tests protocoles passent.
- le transport Windows SetupAPI/HID existe.
- les commandes FORCEADAPT 81 sont encodées.
- le filtre actuel cherche VID 0x37D7 + PID family 2xxx + UsagePage 0xFFA0.
- sur la machine réelle, `ApexSenseBridge.exe list` retourne :
  `No APEX 5 vendor HID interface found.`
- la manette est pourtant connectée et fonctionne via son dongle 2,4 GHz.
- aucun test FORCEADAPT physique n'a donc encore été exécuté.

Ta première tâche est exclusivement de diagnostiquer proprement l'énumération Windows.

Implémente :
- `diagnose`
- `diagnose --all-hid`
- `diagnose --json`

Ces commandes doivent être strictement read-only.

Pour les interfaces pertinentes, affiche :
- device path
- VID/PID
- manufacturer
- product
- serial
- UsagePage / Usage
- InputReportByteLength
- OutputReportByteLength
- FeatureReportByteLength
- Hardware IDs / instance ID
- parent ou informations permettant de regrouper les top-level collections
- interface number MI_xx si disponible

Une interface est pertinente si au moins :
- VID == 0x37D7
- OU path contient vid_37d7
- OU product/manufacturer contient Flydigi/APEX
- OU usage page est vendor-defined.

N'envoie AUCUN output report dans ces modes.

Ajoute les tests pertinents et garde la CLI légère.

Après implémentation, donne :
1. les fichiers modifiés ;
2. les raisons techniques ;
3. les commandes de build/test ;
4. les commandes exactes à lancer sur la machine réelle ;
5. les points qu'il faudra vérifier avant de réactiver `test-rt`.

Ne passe pas encore à la DualSense virtuelle.
```

---

# 35. Notes finales pour la reprise

Le point le plus important est de **ne pas refaire ce qui existe déjà upstream**.

OpenFlydigi contient aujourd'hui beaucoup plus qu'une simple documentation FORCEADAPT :

- protocole matériel ;
- virtual DualSense Linux ;
- parsing output DualSense ;
- traduction adaptive trigger → APEX ;
- DSP haptic audio → moteurs APEX ;
- reconnexion du pad ;
- comportements mesurés.

La stratégie doit être :

```text
comprendre upstream
      ↓
porter les briques nécessaires proprement vers Windows/C++
      ↓
utiliser Steamless/VIIPER pour ce qui est spécifique à la virtualisation Windows
      ↓
mesurer
      ↓
optimiser
```

Pas :

```text
réinventer toutes les couches à partir de zéro
```

La prochaine étape immédiate est le **Milestone DualSense virtuelle Windows** :
évaluer et intégrer un backend capable de créer la DualSense virtuelle et de
compter ses output reports, sans encore router ces rapports vers l'APEX.
