# Relevés de course APEX 4/5

## Ce que l'on mesure

Sans dynamomètre, ces essais relèvent la **position XInput au moment où
l'utilisateur signale une résistance ou une rupture**. Ce n'est ni une force
mesurée, ni un angle mécanique, ni une calibration automatique du traducteur.
Les axes peuvent être remappés par le profil ; le bouton de repère ajoute
un délai humain. Maintenir RT à la position ressentie avant de marquer réduit
ce biais sans le supprimer.

Les commandes restent les modes connus Race et SniperBreak, force fixe 8,
un seul effet actif sur RT. Aucun opcode K6, flash, calibration firmware,
profil ou émulation dynamique n'est envoyé. LT n'est pas stimulée ; les
gâchettes sont neutralisées au départ et à la fin.

## Protocole

1. Fermer les jeux et éviter toute commande concurrente de Space Station/ASB.
   Conserver le même modèle, connexion et profil pour les comparaisons.
2. Lancer un seul essai ci-dessous. Après le compte à rebours et la première
   phase neutre, l'outil attend la disponibilité. Gâchettes relâchées, presser
   puis relâcher **Y** : l'effet démarre immédiatement à son relâchement.
   Sans confirmation sous 60 secondes, aucun effet actif n'est envoyé.
3. Pendant la phase
   active, presser RT très lentement. Dès la résistance ressentie, maintenir
   la position et taper **A**. Pour un effet weapon, maintenir également la
   position de rupture et taper **B**. Relâcher RT avant de recommencer.
4. **X** ou Ctrl+C arrête le test ; relâcher immédiatement si inconfortable.
   La dernière phase revient à Normal. Aucun effet suivant n'est automatique.
5. Recueillir le retour humain avant de lancer un autre fichier. Répéter au
   moins trois essais par réglage ; conserver les essais manqués, mais ne pas
   les utiliser comme preuves de course ou d'absence de résistance.
6. Restaurer Space Station s'il avait été arrêté temporairement. Une annulation
   de sa relance UAC laisse le service arrêté : ne pas annoncer une restauration.

### Cas d'une rupture sèche

Une rupture sèche ne permet pas forcément de séparer début et fin d'une plage
résistante. Si A désigne le blocage et B la position **après** son saut, employer
`--snap-trial` : les labels deviennent `block` et `after_break`, pas des limites
d'intervalle. Y ne sert qu'au démarrage. L'effet reste à force 8, s'arrête après
B suivant A ou après 30 secondes maximum, puis revient à Normal. X annule.

```powershell
./build-win/Release/Apex45TriggerCapture.exe --output captures/rt-snap39-01.jsonl --activate --single-effect weapon_start38_length39_force8 --snap-trial --phase-seconds 6
```

Ne pas soustraire B moins A pour calibrer une longueur : la position après le
saut dépend aussi de la pression et du mouvement de la main. `--marker-trial`
offre la même durée bornée et l'arrêt sur repères pour les relevés de début/fin
explicitement identifiés, avec `--position-check` et un seul effet.

Compilation du diagnostic (cible hors build par défaut) :

```powershell
cmake --build build-win --config Release --target Apex45TriggerCapture
```

Premier essai, début demandé à 38, force 8, avec pauses neutres :

```powershell
./build-win/Release/Apex45TriggerCapture.exe --output captures/rt-start38-01.jsonl --activate --position-check --single-effect feedback_start38_force8 --mark-positions --wait-ready --phase-seconds 10
```

Un nouveau nom de fichier est obligatoire à chaque essai. Pour afficher tous
les stimuli sans ouvrir la manette ni créer de capture :

```powershell
./build-win/Release/Apex45TriggerCapture.exe --position-check --plan
```

Débuts Race disponibles : 0, 38, 77, 115, 154. Longueurs weapon disponibles,
avec début 38 : 20, 39, 96. Ces valeurs sont les commandes issues des masques
DualSense synthétiques, pas des positions mécaniques constatées. Le premier
intervalle vaut 20 (58 moins 38), malgré une largeur idéale de zone de 19,2.

## Analyse

```powershell
./scripts/analyze-trigger-position-captures.ps1 -Path captures/rt-start38-01.jsonl -AsJson
```

Le rapport conserve l'identité, la connexion, l'empreinte SHA-256, les paramètres
envoyés, la plage RT et le minimum/médiane/maximum des marqueurs A/B. Il refuse
de déclarer des positions subjectives exploitables sans mouvement RT, avec LT
utilisée, sans repère de début, après interruption/erreur d'écriture ou sans
neutralisation confirmée. L'absence de marqueur n'est pas une absence d'effet.
`firmware_raw=0` signifie que la version n'est pas renseignée.

Le contrôle de maintien utilise les 150 ms précédant le bouton : au moins
cinq échantillons couvrant 100 ms, avec une dispersion maximale de deux unités
RT. C'est un filtre de qualité du geste, pas une précision mécanique garantie.
`marker_acquisition_usable` décrit seulement l'enregistrement ; les champs
`subjective_*_usable` restent faux sans `-ConfirmEffectBoundaries`. N'utiliser
ce commutateur que si l'utilisateur confirme que A/B désignent réellement
les limites de résistance, pas le début du geste ou l'après-rupture. Les labels
`block`/`after_break` ne deviennent jamais un intervalle de résistance.

Comparer les distributions à paramètres identiques avant de chercher une loi
position/commande. Un seul repère ne valide pas la répétabilité. Ne pas convertir
automatiquement `rt / 255` en angle, ni appliquer une nouvelle table aux jeux.
Pour une loi de force, il faut ajouter une mesure externe synchronisée et une
référence DualSense, au même point de contact et à vitesse contrôlée.

## Dix zones indépendantes : état de la vérification

La relecture du SDK local confirme que `ForceTriggerConfigRace` transmet un
début et une résistance unique. `ForceTriggerConfigRecoil` transmet un début,
une longueur et une force unique. La commande à dix segments K6 est filtrée
par `NewXInput && (DeviceType == 149 || DeviceCode == "k6")` ; l'APEX 5 des
captures est `k5`, type 128. Les dix emplacements génériques du profil ne
constituent pas une table de dix forces indépendantes.

Cela établit les possibilités de ces chemins SDK, **pas l'absence absolue de
commandes cachées dans le firmware**. Aucune capacité native dix zones APEX 4/5
n'a été démontrée ; aucun paquet incompatible n'est essayé pour la deviner.
Une preuve nouvelle nécessiterait une commande officielle APEX correspondante,
une trace du logiciel l'utilisant ou une analyse de firmware identifiable.
Voir `APEX45_FORCEADAPT_RESEARCH.md` pour les sources locales et leurs empreintes.
