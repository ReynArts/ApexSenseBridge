# Reverse engineering APEX 4/5 FORCEADAPT

Analyse du 2026-09-30, complétée par plusieurs captures guidées. **Inventaire du
logiciel, pas spécification exhaustive du firmware. Les essais matériels
utilisent uniquement des commandes d'effets connues ; aucun opcode inconnu,
test usine, calibration ou écriture flash n'a été envoyé.**

## Résultat

- Le SDK local contient 191 variantes de constructeurs de commandes dans
  80 fichiers : lecture d'état, transport, profils, macros, RGB, moteurs,
  calibration, tests usine et mise à jour. Ce ne sont pas 191 opcodes distincts,
  ni 191 commandes acceptées par chaque modèle.
- La commande d'effets APEX 4/5 étudiée offre un début de course et quelques
  paramètres dépendant du mode. Elle n'expose pas une table de dix forces.
- Une table de dix segments existe dans le SDK K6. Son garde logiciel exclut
  les APEX 4/5 ; elle n'est pas une extension démontrée de leur protocole.
- Le champ de rupture nommé `End` dans protobuf est présenté comme une
  **longueur** par l'interface et la documentation officielle. La traduction
  native weapon a été corrigée pour envoyer la longueur, pas la fin absolue.
- La conversion linéaire des zones et les coefficients de force restent des
  choix non calibrés. Une meilleure fidélité globale n'est pas établie par
  cette analyse ou par les tests automatisés.

## Corpus et reproductibilité

Les sources décompilées sont dans le répertoire local ignoré
`work/flydigi-windows-4.2.2.3/decompiled/`. Les binaires propriétaires ne sont
pas ajoutés au dépôt.

| Élément du corpus local | SHA-256 du fichier |
|---|---|
| `service-bundle/Flydigi.ControllerSdk.dll` | `3114CD25C284A74D309449D5BE2DBE6EE9F0D44F8E9BDF98AA6A4C576043B252` |
| `service-bundle/SpaceStationService.dll` | `B4999C1F2819DB6A9B82C4C1856433C53FC2DBECA9D77576B9FFC7919A865660` |
| `service-bundle/AdapterTriggerService.dll` | `06C63CFABE570116170715DB0E8AE4BD119240A64F11BD6CD3E99043373C51D0` |
| `.vite/renderer/main_window/assets/index-DwSmmom_.js` | `35237BC05C415C69CCD68817919369EFE2B3C8A21264112CC1CB96DB6DCBC5AF` |

La comparaison du [site officiel actuel](https://space_station.flydigi.com/)
porte sur les modules suivants. Les empreintes sont calculées sur le texte
reçu, réencodé en UTF-8 ; elles ne prétendent pas identifier les octets
compressés du transfert HTTP.

| Module web lu le 2026-09-30 | SHA-256 UTF-8 |
|---|---|
| [index-DYb4BL3W.js](https://space_station.flydigi.com/assets/index-DYb4BL3W.js) | `FC934452FD97BA68EE64EC3CFF5F353A4DF5A16B451E82F72764C28E510EAFDE` |
| [vendor-gpa6-core-D5jdJfEw.js](https://space_station.flydigi.com/assets/vendor-gpa6-core-D5jdJfEw.js) | `C8855E137009540959DDA393B4C3C199B036FD970347DBA36F67FC3DE6532DAB` |
| [vendor-gpa6-contract-BEtt2INU.js](https://space_station.flydigi.com/assets/vendor-gpa6-contract-BEtt2INU.js) | `6FFF76494840914BE89F2BB20BE53B5A9F4F1A9862A54358BC933B94357B2107` |
| [vendor-gpa6-codec-C4fNnifj.js](https://space_station.flydigi.com/assets/vendor-gpa6-codec-C4fNnifj.js) | `6DC78EEF4FFBD7F8BA7A878C0116B2F3A8C7D9A5C8C8388DC01C3246F30737DE` |

Les entrées de profils et filtres de modèles inspectées dans cette version web
couvrent `k6` et `f5`, pas `k5` ou APEX 4. `f5` n'est pas l'APEX 5.
Le module core expose notamment un mode moteur `83` à paramètres
`area, target, mode, ...params`, différent de l'ancien SDK K6. Il ne faut pas
en déduire que la commande fonctionne sur APEX 4/5. Cette comparaison n'est
pas un audit de chaque module natif de xHaptics ni de futures versions du site.

Pour reproduire l'inventaire SDK, sans manette et sans exécuter les DLL :

```powershell
./scripts/inventory-flydigi-commands.ps1 | Format-Table Source, Class, CommandId, Line
./scripts/inventory-flydigi-commands.ps1 -AsJson
```

Le script lit les surcharges `public override byte CommandId()` à corps entre
accolades. Il conserve l'expression du constructeur conditionnel au lieu de
l'évaluer. Les méthodes héritées, autres signatures, sous-commandes et
commandes présentes seulement dans un autre binaire sont hors de son champ.
Une classe sans suffixe de transport ne doit pas être classée par supposition.

## Commande d'effets APEX 4/5

Source primaire locale : `ControllerSdk/Flydigi.ControllerSDK.data.command/
SetForceTriggerCommandFactory.cs`, méthodes `CommandId`, `CreateCommand`
et classes `ForceTriggerConfig*`. Le garde
`ControllerSdk.SetForceTriggerConfigImpl` exige `IsSupportForceTrigger`.

| Transport du SDK | Effet autonome | Liaison aux vibrations de poignée |
|---|---|---|
| NewXInput | `0x51`, taille annoncée 10, apply puis side/mode/paramètres | `0x52`, taille annoncée 11, side/bind/paramètres |
| XInput ancien | `0x30`, sélecteur 6, apply puis side/mode/paramètres | `0x30`, sélecteur 8 |
| DInput ancien | `0xA0`, famille 1, apply puis side/mode/paramètres | `0xA0`, famille 4 |

L'identifiant seul n'est donc pas suffisant pour identifier une commande.
`SyncTriggerWithGripCommandFactory` possède aussi un `0x51` sur l'ancien
transport XInput, avec une autre disposition. Ce n'est pas le `0x51` NewXInput.

Les noms d'enum SDK des modes 2 et 3 sont inversés par rapport aux libellés
physiques de l'interface. ASB utilise les noms explicites RecoilRattle et
SniperBreak ; ne pas changer leurs numéros.

| Mode wire | Nom SDK | Comportement UI | Paramètres après side/mode |
|---|---|---|---|
| 0 | Normal | Aucun effet | Aucun paramètre utile du constructeur |
| 1 | Race | Résistance | Début, résistance, match-input |
| 2 | Sniper | Vibration / mitrailleuse | Début, résistance initiale, amplitude, fréquence, match-input |
| 3 | Recoil | Résistance puis rupture | Début, longueur de résistance, force, zéro, match-input |
| 4 | Lock | Verrouillage | Position, force (255 par défaut), match-input |
| 5 | Vibration | Constructeur déclaré | Même forme que le mode 2 ; utilisation physique non établie ici |

Le chemin de configuration de Space Station n'utilise pas le constructeur
du mode 5 pour son onglet « vibration » : `ControllerRepository.
CreateForceAdapterConfig` construit une `ForceTriggerConfigSyncWithGrip`,
donc `0x52` en NewXInput. Ne pas considérer un slot d'enum déclaré comme
un effet matériel validé. La liaison comporte side, bindType, filter, scale,
stroke, pressureLevel, strength, frequency ; il ne s'agit pas de dix forces.

Pour le chemin ASB actuellement implémenté :

- APEX 5 : rapport 32 octets, `03 5A A5 51 0A 01 side mode`, puis cinq
  paramètres à l'offset 8 et remplissage nul. Ne pas lui ajouter un checksum
  d'une autre famille de commandes.
- APEX 4 : rapport 15 octets, `05 A0 01 00 side mode`, puis cinq paramètres
  à l'offset 6. Le flag apply reste à zéro conformément au correctif
  d'espacement/blocage déjà présent dans ASB.
- Le constructeur brut reprend la particularité de `ForceTriggerConfigCommon` :
  en mode Race, début nul et match-input égal à 1 donnent match-input nul.
- La variante SDK side=Both existe, mais ASB utilise LT et RT séparément.
  Elle n'autorise pas deux courbes distinctes dans un seul paquet.

Ces dispositions ASB sont couvertes par transport simulé. Elles ne constituent
pas une validation de toutes les connexions USB/dongle/Bluetooth ou versions
de firmware. Un ACK ne prouve pas un effet mécanique.

## Pourquoi le champ End est une longueur

Trois éléments concordent :

1. Le constructeur SDK s'appelle `ForceTriggerConfigRecoil(side, stroke,
   recoilStroke, strength, matchStroke)` : le deuxième paramètre de course
   est transmis sans calcul supplémentaire.
2. Le frontend `index-DwSmmom_.js` lie `recoilEnd` au contrôle
   `trigger_recoil_journey` (« course de maintien de la rupture »), avec
   minimum 1, maximum 255, indépendamment du début (maximum 192).
   Il transmet ensuite cette valeur au champ protobuf `end`.
3. La [documentation officielle Flydigi](https://help.flydigi.com/docs/wh87ym595nggy2p4)
   définit ce contrôle comme la longueur de la course résistante.

Le chemin service transmet directement `TriggerTypeRecoil.End` au paramètre
`recoilStroke` (`ControllerRepository.cs:869`). Le nom protobuf était donc
trompeur pour notre traduction.

Exemple : weapon DualSense aux zones 2 et 7. Le mapping linéaire actuel donne
38 et 134. Envoyer début=38, longueur=134 étend l'intervalle demandé jusqu'à
172 dans ce modèle, au lieu de 134. Le mapping corrigé envoie début=38,
longueur=96. Cela corrige la sémantique du paramètre ; les positions physiques,
la largeur réellement obtenue et le ressenti demandent encore une mesure.

Le correctif concerne uniquement weapon natif `0x25`. Le mode legacy `0x02`
reste transmis comme avant pour éviter de réinterpréter ses paramètres sans
preuve de leur sémantique. Les coefficients de force n'ont pas été augmentés.

## Dix zones, profils et K6

Source K6 : `K6TriggerStrengthMappingCommandFactory.cs:22`, commande
`0x56 / 86`. Son paquet contient notamment :

| Champ | Disposition SDK historique |
|---|---|
| Cible | Offset 5 |
| Segment | Offset 6, borné de 0 à 9 |
| Intensité début/fin | Offsets 7 et 8 |
| Fréquence début/fin | Offsets 9 et 10 |
| Amplitude début/fin | Offsets 11 et 12 |
| Mode/forme d'onde | Offsets 13 et 14 |
| Checksum | Offset 15 |

Le garde `ControllerSdk.cs:666` impose NewXInput et
`DeviceType == 149 || DeviceCode == "k6"`. Les commandes voisines sont
`0x53` mode, `0x54` mode local avec début/fin et gain, `0x55` forme
d'onde et `0x57` temps réel. Elles sont inventoriées, **pas envoyées sur
APEX 4/5**, et leur disposition historique ne doit pas être supposée compatible
avec le protocole actuel du logiciel web.

Dans le profil APEX 4/5, `MappingConfigParser.cs:335` décode un bloc de
20 octets par gâchette : mode, liaison, cinq paramètres de liaison,
`MixedBorder`, puis dix emplacements de paramètres d'effet.
`ControllerRepository.SaveTriggerAdapterConfig` renseigne les premiers
emplacements selon le mode. Dix emplacements génériques ne prouvent pas dix
forces par zone. `MixedBorder` est sérialisé, mais sa sémantique firmware n'est
pas établie par les chemins inspectés.

Le bloc distinct Zero/End/Point1/Point2 configure la courbe de l'axe d'entrée.
Il ne doit pas être transformé en commande de courbe de force. Une position
LT/RT publiée au jeu peut être remappée : elle n'est pas automatiquement un
angle mécanique linéaire et calibré.

## Fidélité actuelle et améliorations possibles

Le format natif DualSense est décrit par
[les générateurs de Nielk1](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db).
La traduction historique de `PS5DataManager.cs` reconnaît des motifs d'octets,
avec des branches différentes pour LT et RT. Ce n'est pas un décodeur complet
de dix zones ; copier cette traduction ne suffit pas à obtenir une équivalence.

| Aspect | Ce qui est établi dans notre code | Ce qui reste non établi |
|---|---|---|
| Feedback `0x21` | Masque et dix forces compactées décodés | Une commande Race ne préserve ici ni les trous ni les variations |
| Weapon `0x25` | Deux positions et force ; longueur corrigée | Équivalence mécanique du début, de la rupture et du retour |
| Vibration `0x26` | Masque, amplitudes, fréquence à l'octet 9 | Équivalence des unités et de la forme d'onde du mode 2 |
| Force | Niveaux conservateurs 8–64 ou 15–120 | Loi de conversion physique DualSense/Flydigi |
| Zones | `round(zone * 192 / 10)` | Relation réelle entre position d'axe et course FORCEADAPT |
| Arrêt | Off 5, masque natif vide ou fréquence nulle vers Normal | Tous les comportements de profils/liaison précédemment actifs |

« Première zone + maximum » est une **politique du traducteur**, pas une limite
matérielle prouvée. Elle peut être exacte structurellement pour une résistance
uniforme jusqu'en fin de course, mais perd une courbe non uniforme ou des trous.
Une force native décodée correctement n'établit pas que son ressenti est correct.

Une piste consiste à garder les dix forces et à mettre à jour la résistance
en fonction de la position de la gâchette, en neutralisant les zones inactives.
C'est une émulation côté PC, pas une table native découverte. Elle demande :

- Une position physique exploitable malgré les courbes et match-input.
- Un worker borné, distinct de la boucle d'entrée, avec coalescence et hystérésis.
- Des mesures de latence, stabilité et remise à zéro, y compris sur une pression
  rapide qui traverse plusieurs zones entre deux écritures.
- Une prise en compte du budget APEX 4 : ASB espace les écritures vendor de
  25 ms et partage le writer entre LT, RT et rumble. Cela représente au mieux
  40 écritures/s globales, pas 40 mises à jour par gâchette ; si les trois slots
  sont continuellement actifs, chacun reçoit environ un tiers du budget.
- Des mesures séparées pour APEX 5 : son chemin actuel ne définit pas ce même
  espacement. La fréquence d'entrée ne donne pas la fréquence d'actualisation
  mécanique ni un débit de commandes sûr.

Aucune émulation dynamique n'a été activée par cet audit : elle ajouterait des
changements fréquents de mode/force sans cadence matériellement validée.

Il faut aussi distinguer indépendance du **calcul** et état du firmware :
notre conversion native n'utilise plus les octets de rumble pour calculer sa
force. Cela ne démontre pas qu'une liaison `0x52` configurée précédemment
est désactivée par `0x51` ou Normal. Tester les profils/liaisons séparément ;
ne pas attribuer leur persistance aux symptômes Horizon sans reproduction.

## Validation pour la prochaine beta

1. Conserver le correctif de longueur, les régressions de décodage et les plafonds
   de force existants. Ne pas annoncer une reproduction exacte de dix zones.
2. Capturer en lecture les effets DualSense et commandes ASB pendant visée,
   tir, changement d'arme, relâchement et menus des deux Horizon.
3. Sur APEX 4 et APEX 5, comparer des effets **déjà connus**, un côté à la fois,
   avec une force faible et un arrêt immédiat disponible : début à 0/38/77,
   rupture début 38 avec longueurs 19/39/96, puis vibration à paramètres fixes.
   Relever apparition/disparition de la résistance et caractère répétable.
4. Mesurer le délai entre envoi et effet mécanique, puis le comportement sous
   mises à jour bornées. Vérifier retour de la gâchette, absence d'oscillation
   et absence de blocage d'entrée ; arrêter à la première anomalie.
5. Calibrer séparément les lois de position, force et fréquence par modèle,
   firmware et transport. Ne pas augmenter les forces pour compenser un
   écart non mesuré.
6. Seulement ensuite décider d'un renderer par zones opt-in, ou rechercher
   une autre commande avec preuve firmware/capture. Aucun fuzzing d'opcodes,
   test usine, calibration, changement d'identité ou écriture flash aveugle.

Les fichiers de mise à jour trouvés dans l'installation locale sont pour
`f5`, pas un firmware APEX 4/5. Aucun firmware APEX 4/5 n'a été désassemblé
ici. L'inventaire ci-dessous ne prouve pas l'absence de commandes privées.

## Vérifications effectuées

- Extraction : 80 sources, 191 variantes, un identifiant conditionnel conservé ;
  export JSON relu avec le même nombre d'entrées.
- Traduction : cas fixes et 45 paires de zones acceptées, LT et RT, rumble varié.
- Bridge : paquets simulés APEX 4/5, longueur corrigée, force réduite à 50 %
  sans modifier la longueur, arrêts, déduplication et échecs d'écriture.
- Build natif Release réussi ; tests ciblés 2/2 et suite CTest Release 25/25.
  Le sandbox bloquait le FileTracker MSBuild ; la compilation a réussi avec
  les permissions d'exécution nécessaires, sans modifier la configuration.
- À l'issue de l'analyse statique : aucun essai mécanique ou jeu effectué.
  Les deux essais qualitatifs ultérieurs sont décrits ci-dessous ; aucun des
  deux Horizon n'a été testé et la calibration quantitative reste à faire.

### Captures guidées APEX 5 du 2026-09-30

Deux essais sans jeu ont ensuite été réalisés avec l'utilisateur sur la
manette identifiée APEX 5, DeviceType 128, dongle 2,4 GHz. Le service
Flydigi Space Station a été arrêté temporairement avec son accord via UAC,
puis son état Running a été rétabli après les essais. Aucun profil, transport
d'entrée, firmware, calibration ou réglage persistant n'a été modifié.

L'outil support `Apex45TriggerCapture` est une cible CMake `EXCLUDE_FROM_ALL`.
Il utilise le traducteur existant avec des effets DualSense **synthétiques**,
intercepte les octets réellement transmis à `writeOutputReport`, mesure début
et fin des appels, et mémorise les positions XInput. Ce n'est ni une capture
d'un jeu, ni une capture USB matérielle, ni une lecture du couple du moteur.
Une écriture réussie est un résultat de transport, pas un ACK firmware.

| Capture locale ignorée | Résultats objectifs | SHA-256 |
|---|---|---|
| `captures/apex5-rt-forceadapt-20260930-01.jsonl` | 1 922 positions, 11 écritures sans erreur, cinq phases de 6 s, retour Normal réussi | `8868B5F520D39D492420EFB59ABE078302497CB064A78B79C6F080268849F62E` |
| `captures/apex5-rt-vibration-20260930-02.jsonl` | 1 154 positions, 9 écritures sans erreur, trois phases de 6 s, retour Normal réussi | `BA360B941768D6C496D48CEE93DA8B210B3850F3DA9459C1A96B9B4EC5506FEC` |

La synthèse locale `captures/apex5-forceadapt-20260930-summary.json` conserve
ces empreintes, les paramètres, les plages d'axe et les retours de l'utilisateur.

Premier essai, RT uniquement : Normal, Race `{38,8,0,0,0}`, rupture
`{38,39,8,0,0}`, rupture `{38,96,8,0,0}`, vibration `{77,1,15,10,0}`.
RT atteint 0–255 dans chaque phase, LT reste à zéro. L'utilisateur rapporte
une résistance **nettement plus longue dans le deuxième effet de rupture**,
et, après clarification, une vibration à 15 ressentie mais initialement
non identifiée comme telle. Ce retour qualitatif
concorde avec le sens « longueur » ; il ne mesure pas une longueur mécanique.

Deuxième essai : Normal puis vibrations `{77,1,30,20,0}` et
`{77,1,60,20,0}`. RT parcourt 0–255 à 30 et 0–242 à 60, LT reste à zéro.
L'utilisateur ressent une vibration **seulement au second palier**. Puisque
début, résistance initiale et fréquence demandée sont identiques, ce retour
suggère une différence de perceptibilité dans ces conditions, sans la prouver :
l'utilisateur souligne ensuite la difficulté d'attribution sans pause neutre.
Cela ne fournit pas un seuil universel, une force minimale garantie ou une
loi de conversion pour tous les profils/firmwares. Le premier essai à 15
utilise une autre fréquence et ne permet pas une comparaison isolée d'amplitude.

L'hypothèse de travail devient : le coefficient actuel `nativeLevel * 15`
peut donner une vibration non perceptible aux niveaux faibles sur cette
manette avec ces paramètres. Ce n'est **pas** une preuve de la cause des
symptômes Horizon, dont nous n'avons pas les paquets. Aucun plancher de force
ni modification de traduction supplémentaire n'a été appliqué après ces essais.
La suite utile est de mesurer des amplitudes intermédiaires et plusieurs
fréquences à conditions constantes avant de définir une loi non linéaire.

Limites de mesure : les axes sont remappés par XInput, pas des angles physiques.
Le polling demande une pause de 5 ms, mais le premier essai observe en moyenne
15,629 ms entre échantillons (environ 64 Hz, minimum 11,877 ms,
maximum 16,670 ms). Les appels d'écriture durent 1,770–3,998 ms ; ces durées ne
sont pas le délai de réponse mécanique. `firmware_raw=0` dans les captures
est une valeur non renseignée par le parseur, pas une version firmware connue.
Les lignes JSONL sont regroupées par type à la sauvegarde ; trier selon les
horodatages pour reconstituer le déroulé, et non selon leur ordre dans le fichier.

Pour reproduire hors jeu, fermer toute session de bridge et Space Station,
connecter une seule manette et garder RT relâchée si l'effet gêne :

```powershell
cmake --build build-win --config Release --target Apex45TriggerCapture
./build-win/Release/Apex45TriggerCapture.exe --plan
./build-win/Release/Apex45TriggerCapture.exe --output captures/new-baseline.jsonl
./build-win/Release/Apex45TriggerCapture.exe --output captures/new-effects.jsonl --activate
./build-win/Release/Apex45TriggerCapture.exe --output captures/new-vibration.jsonl --activate --vibration-check
```

Les fichiers doivent être nouveaux et leur répertoire doit exister.
Sans `--activate`, seule la lecture d'identité et des axes est effectuée,
sans neutralisation ou effet envoyé. `--plan` ne fait aucune E/S HID.
Les fichiers sont sauvegardés après la remise à Normal afin de ne pas faire
d'E/S disque à chaque échantillon ; les données en mémoire seraient perdues
en cas de crash ou de terminaison forcée. L'arrêt normal, les erreurs après
activation et Ctrl+C tentent la neutralisation. Une fermeture forcée du processus
n'offre pas cette garantie. `stop-active-sessions` peut aussi demander l'arrêt
gracieux grâce à la propriété de session partagée ; ne pas tuer le processus.

### Répétitions et limites de reproductibilité

Les mêmes deux séries ont été relancées à la demande de l'utilisateur :

| Capture | Données | SHA-256 |
|---|---|---|
| `captures/apex5-rt-forceadapt-20260930-03.jsonl` | 1 925 positions, 11 écritures réussies ; RT 0–255 dans chacune des cinq phases, LT à zéro ; neutralisation réussie | `E7A3683BE51812B9D65904659961F1D155A515BCA596AE77A3BE85931251FC93` |
| `captures/apex5-rt-vibration-20260930-04.jsonl` | 1 153 positions, 9 écritures réussies ; RT et LT toujours à zéro ; neutralisation réussie | `1BA7CA37E7F1AE8B7E4959AE0C21BBAB4DCFAAA91AF50BC538A3FF5533BC70C6` |

L'utilisateur confirme **ne pas avoir réalisé la deuxième série**. La capture
04 est conservée comme trace technique, mais exclue de toute comparaison
perceptive. Le démarrage de la seconde série pendant les questions sur la
première n'était pas un protocole interactif adéquat.

Pour la capture 03, son retour décrit d'abord une résistance sur toute la
gâchette, puis une rupture très courte et nette, suivie d'une légère vibration.
Il confirme cette fois percevoir la dernière vibration à amplitude 15.
Ce récit ne permet pas d'identifier sûrement les deux phases weapon 39/96
par rapport à la phase Race précédente. Il ne constitue donc pas une
confirmation répétée du sens ou du ressenti des deux longueurs.

L'utilisateur précise ensuite avoir également ressenti la vibration à 15
lors du premier essai, sans l'avoir identifiée comme telle. Son premier « non »
est donc retiré : aucune absence de vibration à 15 ni variabilité à paramètres
identiques n'est établie. Les premiers retours ne constituent pas une calibration
validée et ne justifient pas d'augmenter les forces par défaut.
Les essais suivants doivent isoler un effet à la fois, annoncer précisément
son début, revenir à Normal, puis attendre le retour de l'utilisateur avant
de démarrer le suivant. La capture 05 a également été neutralisée, mais son
enchaînement sans pause rend la comparaison 30/60 difficile selon l'utilisateur :
elle n'est pas une validation perceptive de cette différence. L'outil propose
désormais `--single-effect PHASE_NAME` : Normal, un effet, puis Normal ; une
nouvelle invocation attend le retour humain avant de tester un autre effet.
Space Station a été réactivé après ces répétitions et son état Running vérifié.

### Relevé de position avec marqueurs humains

Le diagnostic ajoute `--position-check`, `--mark-positions` et `--wait-ready`.
Il impose un seul effet actif entre deux phases neutres. Y confirme la
disponibilité, A marque la résistance ressentie, B la rupture et X annule.
Les contrôles et l'analyse sont décrits dans `APEX45_TRIGGER_MEASUREMENTS.md`.

Capture `captures/apex5-rt-position38-20260930-08.jsonl`, SHA-256
`516EC5172EB171F5A4F9A5F4D9DC866F2A3C78AE2CED9A2E91EE4164BA259A4E` :
1 920 échantillons, aucune erreur d'écriture et neutralisation confirmée.
L'effet Race demandé est `{38,8,0,0,0}`. Pendant les 10 secondes actives,
639 échantillons donnent RT 0–164, LT reste à zéro. Deux marqueurs A sont
enregistrés, à RT 98 puis 68 (médiane 83, dispersion 30 unités XInput).

Ce sont des positions signalées par l'utilisateur, pas un couple ni un angle.
L'utilisateur confirme avoir arrêté RT au début ressenti pour les deux appuis.
Cette dispersion et cet essai unique
interdisent de convertir 38 en 83 ou d'ajuster automatiquement la loi de course.
Répéter le même réglage avant de passer aux débuts 77/115/154, en conservant
profil et connexion. Les captures 06/07 valident le fonctionnement des
vibrations isolées selon le retour utilisateur « tout fonctionne », mais ne
contiennent aucun marqueur de position et ne calibrent donc pas la course.

La relecture des constructeurs Race/Recoil et du garde K6 ne découvre aucune
table native dix forces pour APEX 4/5. Cela ne prouve pas que toute commande
cachée est absente. Aucun paquet expérimental K6 ni mise à jour dynamique n'est
envoyé. Les coefficients et la traduction de production restent inchangés.
Space Station demeure arrêté depuis l'annulation de sa relance UAC précédente ;
ce nouveau relevé n'a pas changé son état.

La répétition 09 (`captures/apex5-rt-position38-20260930-09.jsonl`, SHA-256
`5E3C8B1533DE8730ADD292B331C871978FD362E10108A9107F0056727F767BBD`)
contient 1 924 échantillons et 642 pendant l'effet, tous à RT=0, sans marqueur.
Écritures et neutralisation réussies, mais cette répétition est exclue du relevé
de course. Le diagnostic attend désormais Y **après** la préparation neutre et
active l'effet dès son relâchement, sans délai neutre supplémentaire.
L'utilisateur dit ne pas avoir ressenti de résistance pendant 09 et demande
de refaire ; l'absence de déplacement RT empêche de trancher sur la cause.

Capture 10 après ce changement :
`captures/apex5-rt-position38-20260930-10.jsonl`, SHA-256
`08D153B67317F09680137910D64DE5BFCE4DCD51F6FCB0C65FF9FDAF67A44F98`.
1 920 échantillons, 639 pendant Race `{38,8,0,0,0}`, RT 0–93, LT à zéro,
aucune erreur et neutralisation réussie. Un marqueur A à RT 49 ; l'utilisateur
confirme la résistance ressentie et RT maintenu au début. Les valeurs 98/68/49 à même commande ne
permettent pas une calibration stable. Il faut contrôler geste, profil et
relation de l'axe remappé à la course, puis répéter plusieurs positions.

Les trois essais suivants utilisent le départ immédiat sur relâchement de Y,
toujours force 8, pauses neutres et marqueur A pendant la phase active :

| Capture | Début demandé | Repère A, axe RT | Échantillons total / actifs | SHA-256 |
|---|---|---|---|---|
| `apex5-rt-position38-20260930-11.jsonl` | 38 | 61 | 1 919 / 640 | `D16BC43BD496781E426399C3B546E062034FA137E98A44225D5DBFA6CFF6A242` |
| `apex5-rt-position77-20260930-12.jsonl` | 77 | 113 | 1 920 / 640 | `99C0171E37782F1B701BA1E1068FBE52458608C636475B26152AD9C9B42635AF` |
| `apex5-rt-position115-20260930-13.jsonl` | 115 | 160 | 1 920 / 641 | `3CA974D1D97E2733312EA15B9D30E308B83A244CC0D2B735EA8496A8EE46E3C9` |

LT reste à zéro, toutes les écritures réussissent et les trois captures sont
neutralisées. Les positions apparaissent dans l'ordre demandé. Les commandes
de course et l'axe n'ont pas la même échelle : dans l'hypothèse d'un profil
linéaire 0–255 et d'une course FORCEADAPT 0–192, les débuts 38/77/115
correspondraient respectivement à environ 50/102/153 sur l'axe. Cette hypothèse
est compatible approximativement avec les derniers marqueurs, mais n'est pas
une calibration physique validée. Il ne faut ni comparer directement 38 à 61,
ni déduire une correction universelle à partir des écarts.

La répétition de 38 donne encore 49 puis 61. Les points 77 et 115 n'ont qu'un
marqueur chacun ; le début 38 avait aussi donné 98 et 68 avec l'ancien protocole.
La dispersion humaine/profil/mécanique n'est pas isolée. Aucun gain, seuil de
force, loi de course ou émulation dix zones n'est modifié dans le moteur.
Le résultat utilisable pour la beta est un diagnostic reproductible et une
validation subjective de débuts distincts, pas une précision de force accrue.

### Ruptures isolées et correction du sens des repères

L'utilisateur demande de poursuivre et de laisser Space Station arrêté.
Les essais suivants conservent début 38 et force 8, en ne changeant que la
longueur du mode SniperBreak. Écritures et neutralisation sont réussies, LT
reste à zéro. Les SHA-256 permettent de retrouver les JSONL locaux ignorés.

| Capture | Paramètre longueur | Repères enregistrés | SHA-256 |
|---|---|---|---|
| `apex5-rt-length39-20260930-14.jsonl` | 39 | Aucun ; RT 0–255, utilisateur confirme avoir oublié A/B | `AE8FA6740BDAF204A5943D03D23BC63BC90B6986C03BDB7415664F27E184EAEC` |
| `apex5-rt-length39-20260930-15.jsonl` | 39 | A=16, B=187 ; RT encore en mouvement avant B | `1532DD74D2466F791B531FE582913CA0E2ACFD81616203F9BE0E2E5978B20EB7` |
| `apex5-rt-length96-20260930-16.jsonl` | 96 | A=11, B=176, positions maintenues | `FB99CBC9AE7EBE31486C5DD373C48276789D1EA8FC1581B13A1D414456122CE2` |
| `apex5-rt-length39-20260930-17.jsonl` | 39 | Aucun ; RT 0–255 | `6F4BCCE74512EB5594061E149006E05F0E03A3F54CDEE635DB61FBEBB7268477` |
| `apex5-rt-snap39-20260930-18.jsonl` | 39 | Blocage=8, après-rupture=134, positions maintenues | `70821E0C5D76DAB3883081D5D602EF8C83DBFC02E922612639B82D6FD60740BA` |
| `apex5-rt-snap96-20260930-19.jsonl` | 96 | Blocage=13, après-rupture=166 ; RT varie avant B | `AA9AF4ECF17C44F9A68C699A4C95229EB190B240A86C9BDB26700BA15CC2E855` |

Après 15/16, l'utilisateur confirme ressentir 96 plus long que 39, mais précise
que A marquait le début de son appui : ces A ne sont donc **pas** des débuts
de résistance. Il explique ensuite la difficulté d'une rupture sèche : A
marque le blocage et B la position après son saut. La mention de Y pour ce
dernier repère est immédiatement corrigée par l'utilisateur : il voulait B.
Aucun essai avec Y comme repère de rupture n'a été exécuté.

Le diagnostic distingue désormais ces états avec `--snap-trial` : A produit
`block`, B produit `after_break`. Y démarre après préparation neutre ; B
suivant A termine l'effet, avec un maximum de 30 secondes, puis retour Normal.
Les 18/19 contiennent respectivement 1 474/1 559 échantillons, dont 705/790
pendant l'effet. Les boutons sont aussi conservés avec chaque axe pour les
captures nouvelles. L'analyse teste le maintien avant le bouton et exige une
confirmation humaine du sens des limites avant de rendre un intervalle
subjectif exploitable. Les états blocage/après-saut ne sont jamais convertis
en longueur résistante ni en force mesurée.

La différence qualitative 39/96 corrobore la sémantique de longueur du SDK,
mais les repères ne calibrent pas les unités ni ne valident une équivalence
DualSense. Les positions après-saut dépendent de la main et peuvent être prises
après un dépassement/retour. La mesure du couple, la frontière mécanique exacte,
la latence moteur et l'extension éventuelle à dix forces indépendantes restent
non établies. Aucun nouveau coefficient de production n'est appliqué.

## Inventaire des constructeurs SDK

Chemins relatifs à `decompiled/ControllerSdk/`. Les variantes sont données
avec leur classe exacte ; les identifiants peuvent partager des sous-commandes.
Les classes des répertoires `bak`, `test` ou destinées à d'autres modèles
ne sont pas des fonctionnalités validées sur APEX 4/5. Les commandes de
calibration, restauration usine, identité et upgrade ne sont pas des commandes
à essayer pour améliorer un effet.

| Source SDK | Variantes et identifiants |
|---|---|
| `Flydigi.ControllerSDK.data.command.bak/SwitchToDInputCommandFactory.cs` | `SwitchModeCommandXInput` : `0x17` |
| `Flydigi.ControllerSDK.data.command.bak/SwitchToXInputCommandFactory.cs` | `SwitchModeCommandXInput` : `0xED` |
| `Flydigi.ControllerSDK.data.command.config/ApplyMappingConfigByCfgIdCommandFactory.cs` | `ApplyMappingConfigByCfgIdCommandDInput` : `0x50`<br>`ApplyMappingConfigByCfgIdCommandNewXInput` : `0xA2`<br>`ApplyMappingConfigByCfgIdCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.config/ReadCurrentMappingConfigIdCommand.cs` | `ReadCurrentMappingConfigIdCommandDInput` : `0xEB`<br>`ReadCurrentMappingConfigIdCommandXInput` : `0x20` |
| `Flydigi.ControllerSDK.data.command.config/ReadLedConfigCommand.cs` | `ReadRgbConfigCommandDInput` : `0xE5`<br>`ReadRgbConfigCommandNewXInput` : `0xA7`<br>`ReadRgbConfigCommandXInput` : `0x26` |
| `Flydigi.ControllerSDK.data.command.config/ReadMacroConfigCommand.cs` | `ReadMacroConfigCommandNewXInput` : `0xAC` |
| `Flydigi.ControllerSDK.data.command.config/ReadMappingConfigCommand.cs` | `ReadMappingConfigCommandDInput` : `0xEB`<br>`ReadMappingConfigCommandNewXInput` : `0xA3`<br>`ReadMappingConfigCommandXInput` : `0x21` |
| `Flydigi.ControllerSDK.data.command.config/ReadMappingConfigVersionAllCommandFactory.cs` | `ReadMappingConfigVersionAllCommandDInput` : `0x50`<br>`ReadMappingConfigVersionAllCommandNewXInput` : `0xA1`<br>`ReadMappingConfigVersionAllCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.config/ReadMappingConfigVersionCommandFactory.cs` | `ReadMappingConfigVersionCommandDInput` : `0x50`<br>`ReadMappingConfigVersionCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.config/ResetMappingConfigByCfgIdCommandFactory.cs` | `ResetMappingConfigByCfgIdCommand` : `0xAF` |
| `Flydigi.ControllerSDK.data.command.config/SaveCurrentMappingConfigCommandFactory.cs` | `SaveCurrentMappingConfigCommandDInput` : `0x50`<br>`SaveCurrentMappingConfigCommandNewXInput` : `0xA6`<br>`SaveCurrentMappingConfigCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.config/SaveCurrentSwitchMappingConfigCommandFactory.cs` | `SaveCurrentSwitchMappingConfigCommandDInput` : `0x50`<br>`SaveCurrentSwitchMappingConfigCommandNewXInput` : `0xAB`<br>`SaveCurrentSwitchMappingConfigCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.config/SetMappingEnableCommandFactory.cs` | `SetMappingEnableControllerCommandDInput` : `0xEE`<br>`SetMappingEnableControllerCommandXInput` : `0x18` |
| `Flydigi.ControllerSDK.data.command.config/WriteAllMappingConfigCommandFactory.cs` | `WriteMappingConfigCommandStartDInput` : `0xEF`<br>`WriteMappingConfigCommandStartNewXInput` : `0xA4`<br>`WriteMappingConfigCommandStartXInput` : `0x25`<br>`WriteMappingConfigPackCommandDInput` : `0x22`<br>`WriteMappingConfigPackCommandNewXInput` : `0xA5`<br>`WriteMappingConfigPackCommandXInput` : `0x24` |
| `Flydigi.ControllerSDK.data.command.config/WriteAllRgbConfigCommand.cs` | `WriteRgbConfigCommandStartDInput` : `0xE6`<br>`WriteRgbConfigCommandStartNewXInput` : `0xA8`<br>`WriteRgbConfigCommandStartXInput` : `0x2A`<br>`WriteRgbConfigPackCommandDInput` : `0x33`<br>`WriteRgbConfigPackCommandNewXInput` : `0xA9`<br>`WriteRgbConfigPackCommandXInput` : `0x29` |
| `Flydigi.ControllerSDK.data.command.config/WriteMappingConfigCommandFactory.cs` | `WriteMappingConfigCommandStartDInput` : `0xEF`<br>`WriteMappingConfigCommandStartNewXInput` : `0xA4`<br>`WriteMappingConfigCommandStartXInput` : `0x23`<br>`WriteMappingConfigPackCommandDInput` : `0x22`<br>`WriteMappingConfigPackCommandNewXInput` : `0xA5`<br>`WriteMappingConfigPackCommandXInput` : `0x24` |
| `Flydigi.ControllerSDK.data.command.config/WriteMarcoConfigCommandFactory.cs` | `WriteMacroConfigCommandStartNewXInput` : `0xAD`<br>`WriteMappingConfigPackCommandNewXInput` : `0xAE` |
| `Flydigi.ControllerSDK.data.command.config/WriteRgbConfigCommand.cs` | `WriteRgbConfigCommandStartDInput` : `0xE6`<br>`WriteRgbConfigCommandStartNewXInput` : `0xA8`<br>`WriteRgbConfigCommandStartXInput` : `0x28`<br>`WriteRgbConfigPackCommandDInput` : `0x33`<br>`WriteRgbConfigPackCommandNewXInput` : `0xA9`<br>`WriteRgbConfigPackCommandXInput` : `0x29` |
| `Flydigi.ControllerSDK.data.command.information/DongleInfoCommandFactory.cs` | `DongleInfoControllerCommandDInput` : `0x11`<br>`DongleInfoControllerCommandXInput` : `0x11` |
| `Flydigi.ControllerSDK.data.command.information/ExtraInfoCommandFactory.cs` | `ExtraInfoControllerCommandDInput` : `0xF5`<br>`ExtraInfoControllerCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.information/HeartBeatCommandFactory.cs` | `HeartBeatControllerCommandDInput` : `0xEC`<br>`HeartBeatControllerCommandNewXInput` : `0x01`<br>`HeartBeatControllerCommandXInput` : `0x10` |
| `Flydigi.ControllerSDK.data.command.information/ProbeConfigProtocolVersionCommandFactory.cs` | `ProbeConfigProtocolVersionCommand` : `0x07` |
| `Flydigi.ControllerSDK.data.command.information/ReadModeUsageCountCommandFactory.cs` | `ReadModeUsageCountCommandCommandDInput` : `0x50`<br>`ReadModeUsageCountCommandCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.information/ReadNicknameCommandFactory.cs` | `ReadNickNameControllerCommandNewXInput` : `0x02` |
| `Flydigi.ControllerSDK.data.command.information/ReadRawDataReportStatusCommandFactory.cs` | `ReadRawDataReportStatusCommand` : `0x10` |
| `Flydigi.ControllerSDK.data.command.information/ReadUidCommandFactory.cs` | `ReadUidControllerCommandDInput` : `0xFA`<br>`ReadUidControllerCommandNewXInput` : `0x04`<br>`ReadUidControllerCommandXInput` : `0xA0` |
| `Flydigi.ControllerSDK.data.command.information/ReadUsageCountCommandFactory.cs` | `ReadUsageCountCommandCommandDInput` : `0xFA`<br>`ReadUsageCountCommandCommandXInput` : `0xA1` |
| `Flydigi.ControllerSDK.data.command.screen/EnableScreenStatusBarAlwaysOnCommandFactory.cs` | `EnableScreenStatusBarAlwaysOnCommandDInput` : `0xF2`<br>`EnableScreenStatusBarAlwaysOnCommandNewXInput` : `0x13`<br>`EnableScreenStatusBarAlwaysOnCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.screen/OffScreenCommandFactory.cs` | `OffScreenCommandNewXInput` : `0x13` |
| `Flydigi.ControllerSDK.data.command.screen/ReadScreenSettingCommandFactory.cs` | `ReadScreenSettingCommandDInput` : `0xF2`<br>`ReadScreenSettingCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.screen/UploadPic2K2DataCommand.cs` | `UploadPic2K2DataCommandDInput` : `0xD1`<br>`UploadPic2K2DataCommandXInput` : `0xD1` |
| `Flydigi.ControllerSDK.data.command.screen/UploadPic2K2EndCommand.cs` | `UploadPic2K2EndCommandDInput` : `0xD2`<br>`UploadPic2K2EndCommandXInput` : `0xD2` |
| `Flydigi.ControllerSDK.data.command.screen/UploadPic2K2FinishCommand.cs` | `UploadPic2K2FinishCommandDInput` : `0xD3`<br>`UploadPic2K2FinishCommandXInput` : `0xD3` |
| `Flydigi.ControllerSDK.data.command.screen/UploadPic2K2StartCommand.cs` | `UploadPicStartCommandDInput` : `0xD0`<br>`UploadPicStartCommandXInput` : `0xD0` |
| `Flydigi.ControllerSDK.data.command.screen/UploadPicCommandK1Factory.cs` | `UploadPic2K2DataCommandDInput` : `0xD1`<br>`UploadPic2K2DataCommandXInput` : `0xD1`<br>`UploadPicEndCommandDInput` : `0xD2`<br>`UploadPicEndCommandXInput` : `0xD2`<br>`UploadPicStartCommandDInput` : `0xD0`<br>`UploadPicStartCommandXInput` : `0xD0` |
| `Flydigi.ControllerSDK.data.command.setting/DeviceMaskCommandFactory.cs` | `DeviceMaskCommandDInput` : `0x10` |
| `Flydigi.ControllerSDK.data.command.setting/DisableMacroMappingCommandFactory.cs` | `DisableMacroMappingControllerCommandDInput` : `0xE9`<br>`DisableMacroMappingControllerCommandXInput` : `0x19` |
| `Flydigi.ControllerSDK.data.command.setting/EnableAudioCommandFactory.cs` | `EnableAudioCommandDInput` : `0xFA`<br>`EnableAudioCommandNewXInput` : `0x13`<br>`EnableAudioCommandXInput` : `0xA2` |
| `Flydigi.ControllerSDK.data.command.setting/EnableDockSmartStopCommandFactory.cs` | `EnableDockSmartStopCommandDInput` : `0x50`<br>`EnableDockSmartStopCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/EnableDS5DataCommandFactory.cs` | `EnableDS5ModeDataCommandDInput` : `0xE8` |
| `Flydigi.ControllerSDK.data.command.setting/EnableJoystickAutoCalibrationCommandFactory.cs` | `EnableJoystickAutoCalibrationCommandDInput` : `0x50`<br>`EnableJoystickAutoCalibrationCommandNewXInput` : `0x13`<br>`EnableJoystickAutoCalibrationCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/EnableJoystickDebounceCommandFactory.cs` | `EnableJoystickDebounceCommandDInput` : `0x50`<br>`EnableJoystickDebounceCommandNewXInput` : `0x13`<br>`EnableJoystickDebounceCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/EnableJoystickReboundCommandFactory.cs` | `EnableJoystickReboundCommandDInput` : `0x50`<br>`EnableJoystickReboundCommandNewXInput` : `0x13`<br>`EnableJoystickReboundCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/EnableMappingSwitchCommandFactory.cs` | `EnableMappingSwitchCommandDInput` : `0x50`<br>`EnableMappingSwitchCommandNewXInput` : `0x13`<br>`EnableMappingSwitchCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/EnableMotionDebounceCommandFactory.cs` | `EnableMotionDebounceCommandDInput` : `0xF5`<br>`EnableMotionDebounceCommandNewXInput` : `0x13`<br>`EnableMotionDebounceCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.setting/EnableQuickSwitchConfigCommandFactory.cs` | `EnableQuickSwitchConfigCommandDInput` : `0xFA`<br>`EnableQuickSwitchConfigCommandNewXInput` : `0x13`<br>`EnableQuickSwitchConfigCommandXInput` : `0xA2` |
| `Flydigi.ControllerSDK.data.command.setting/EnableXboxHomeButtonCommandFactory.cs` | `EnableXboxHomeButtonCommandNewXInput` : `0x13`<br>`EnableXboxHomeButtonCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.setting/ReadAutoSleepPeriodCommandFactory.cs` | `ReadAutoSleepPeriodCommandDInput` : `0xF2`<br>`ReadAutoSleepPeriodCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.setting/ReadHardwareFunctionStatusCommandFactory.cs` | `ReadHardwareFunctionEnableStatusCommandDInput` : `0xF2`<br>`ReadHardwareFunctionEnableStatusCommandNewXInput` : `0x03`<br>`ReadHardwareFunctionEnableStatusCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/RestartCommandFactory.cs` | `UpdateNicknameCommandNewXInput` : `0x1D` |
| `Flydigi.ControllerSDK.data.command.setting/SetHardwareMacroEnableCommandFactory.cs` | `SetHardwareMacroEnableCommandDInput` : `0x50`<br>`SetHardwareMacroEnableCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/SwitchModeCommandFactory.cs` | `SwitchModeCommand` : `0x1B` |
| `Flydigi.ControllerSDK.data.command.setting/UpdateJoystickPrecisionCommandFactory.cs` | `UpdateJoystickPrecisionCommandDInput` : `0x50`<br>`UpdateJoystickPrecisionCommandNewXInput` : `0x15`<br>`UpdateJoystickPrecisionCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/UpdateJoystickSensitivityCommandFactory.cs` | `UpdateJoystickSensitivityCommandDInput` : `0x50`<br>`UpdateJoystickSensitivityCommandNewXInput` : `0x16`<br>`UpdateJoystickSensitivityCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/UpdateNicknameCommandFactory.cs` | `UpdateNicknameCommandNewXInput` : `0x18` |
| `Flydigi.ControllerSDK.data.command.setting/UpdateReportRateCommandFactory.cs` | `UpdateReportRateCommandDInput` : `0x50`<br>`UpdateReportRateCommandNewXInput` : `0x14`<br>`UpdateReportRateCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command.setting/UpdateSleepTimeCommandFactory.cs` | `UpdateSleepTimeCommandDInput` : `0xF2`<br>`UpdateSleepTimeCommandNewXInput` : `0x17`<br>`UpdateSleepTimeCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.test/CalibrationAdcCommandFactory.cs` | `CalibrationAdcCommandDInput` : `0xE2`<br>`CalibrationAdcCommandNewXInput` : `0xF0`<br>`CalibrationAdcCommandXInput` : `0x14` |
| `Flydigi.ControllerSDK.data.command.test/SleepCommandFactory.cs` | `SleepControllerCommandDInput` : `0xE4`<br>`SleepControllerCommandXInput` : `0x16` |
| `Flydigi.ControllerSDK.data.command.test/TestForceTriggerCommandFactory.cs` | `TestForceTriggerCommandDInput` : `0xF2`<br>`TestForceTriggerCommandNewXInput` : `0xF7`<br>`TestForceTriggerCommandXInput` : `0xF2` |
| `Flydigi.ControllerSDK.data.command.test/TestIndicatorCommandFactory.cs` | `TestIndicatorControllerCommandDInput` : `0xE3`<br>`TestIndicatorControllerCommandNewXInput` : `0xF1`<br>`TestIndicatorControllerCommandXInput` : `0x15` |
| `Flydigi.ControllerSDK.data.command.test/TestJoystickCommandFactory.cs` | `TestJoystickControllerCommandDInput` : `0xF6`<br>`TestJoystickControllerCommandNewXInput` : `0xF6`<br>`TestJoystickControllerCommandXInput` : `0xF6` |
| `Flydigi.ControllerSDK.data.command.test/TestLedCommandFactory.cs` | `TestLedControllerCommandDInput` : `0xE0`<br>`TestLedControllerCommandNewXInput` : `0xF5`<br>`TestLedControllerCommandXInput` : `0x13` |
| `Flydigi.ControllerSDK.data.command.test/TestLossCommandFactory.cs` | `TestLossCommandNewXInput` : `0xF4`<br>`TestLossCommandXInput` : `0x41` |
| `Flydigi.ControllerSDK.data.command.test/TestRecoverFactoryCommand.cs` | `TestRfCommandNewXInput` : `0xFD` |
| `Flydigi.ControllerSDK.data.command.test/TestRfCommandFactory.cs` | `TestRfCommandDInput` : `0xF2`<br>`TestRfCommandNewXInput` : `0xF4` |
| `Flydigi.ControllerSDK.data.command.test/TestScreenCommandFactory.cs` | `TestScreenControllerCommandDInput` : `0xA0`<br>`TestScreenControllerCommandNewXInput` : `0xF2`<br>`TestScreenControllerCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command.test/TestVibrationCommandFactory.cs` | `TestVibrationCommandDInput` : `0xF5`<br>`TestVibrationCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command/AcquireControllerCommandFactory.cs` | `AcquireControllerCommandCommand` : `0x1C` |
| `Flydigi.ControllerSDK.data.command/EnableRawDataTransportInCommandFactory.cs` | `EnableRawDataTransportInCommand` : `0x11`<br>`EnableRawDataTransportInCommandDInput` : `0xF5`<br>`EnableRawDataTransportInCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command/K6TriggerLocalModeCommandFactory.cs` | `K6TriggerLocalModeCommandNewXInput` : `0x54` |
| `Flydigi.ControllerSDK.data.command/K6TriggerModeCommandFactory.cs` | `K6TriggerModeCommandNewXInput` : `0x53` |
| `Flydigi.ControllerSDK.data.command/K6TriggerRealtimeCommandFactory.cs` | `K6TriggerRealtimeCommandNewXInput` : `0x57` |
| `Flydigi.ControllerSDK.data.command/K6TriggerStrengthMappingCommandFactory.cs` | `K6TriggerStrengthMappingCommandNewXInput` : `0x56` |
| `Flydigi.ControllerSDK.data.command/K6TriggerWaveformCommandFactory.cs` | `K6TriggerWaveformCommandNewXInput` : `0x55` |
| `Flydigi.ControllerSDK.data.command/SetForceTriggerCommandFactory.cs` | `ForceTriggerControllerCommandDInput` : `0xA0`<br>`ForceTriggerControllerCommandNewXInput` : `0x51 / 0x52 (selon configuration)`<br>`ForceTriggerControllerCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command/SwitchToFirmwareUpgradeModeCommandFactory.cs` | `SwitchToFirmwareUpgradeModeCommandDInput` : `0xF5`<br>`SwitchToFirmwareUpgradeModeCommandNewXInput` : `0x1F`<br>`SwitchToFirmwareUpgradeModeCommandXInput` : `0x30` |
| `Flydigi.ControllerSDK.data.command/SyncTriggerWithGripCommandFactory.cs` | `SyncTriggerWithGripControllerCommandXInput` : `0x51` |
| `Flydigi.ControllerSDK.data.command/VibrationCommandFactory.cs` | `VibrationControllerCommandDInput` : `0x0F`<br>`VibrationControllerCommandNewXInput` : `0x12`<br>`VibrationControllerCommandXInput` : `0x50` |
| `Flydigi.ControllerSDK.data.command/WriteDeviceTypeCommandFactory.cs` | `WriteDeviceTypeControllerCommand` : `0xFE`<br>`WriteDeviceTypeControllerCommandDInput` : `0xF5`<br>`WriteDeviceTypeControllerCommandXInput` : `0x50` |

