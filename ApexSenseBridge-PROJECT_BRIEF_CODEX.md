# ApexSenseBridge — fiche projet actuelle

**État : 1er septembre 2026**

**Version : 0.3.0 déployée ; développement post-0.3.0 en cours**

**Cible : Windows 10/11 x64, APEX 5 via dongle 2,4 GHz**

Ce document remplace l'ancien handoff hybride. Le mode où une APEX physique alimentait directement le jeu pendant qu'une DualSense virtuelle ne servait qu'au feedback n'est plus autorisé pour un jeu configuré.

## Architecture verrouillée

```text
Jeu sans profil DualSense
APEX physique ── XInput natif ──► jeu
Aucun processus ApexSenseBridge

Jeu avec profil DualSense
APEX physique ── HID/XInput ──► ApexSenseBridge ──► DualSense virtuelle ──► jeu
                 interfaces physiques masquées par HidHide
```

Règles non négociables du mode DualSense :

- aucun stick, bouton, D-pad ou axe physique n'arrive directement au jeu ;
- le bridge traduit l'état complet vers un rapport DualSense ;
- le backend XInput est un secours interne du bridge, jamais un chemin visible au jeu ;
- HidHide est actif et vérifié avant le signal IPC `Ready` ;
- toute perte de l'isolation ou du proxy est traitée en échec fermé ;
- les jeux non configurés conservent exactement le chemin XInput natif et ne lancent rien.

## Implémentation actuelle

### Entrée physique

L'abstraction `PhysicalInputSource` choisit en priorité une lecture HID overlapped événementielle de `IG_01`, associée à FORCEADAPT par le même `ContainerId`. Une source HID n'est acceptée que si son descripteur permet une représentation sans perte de tous les contrôles.

Le descripteur mesuré sur l'APEX expose X, Y, Rx, Ry, Z et le hat, mais pas Rz. La source HID est donc correctement refusée sur ce firmware et le backend XInput interne est sélectionné. L'association automatique utilise le VID/PID renvoyé par `XInputGetCapabilitiesEx`; `--xinput-index` ne subsiste qu'en diagnostic avancé.

La boucle fixe à 4 ms a été supprimée. Les rapports HID, l'arrêt IPC, la déconnexion et les échéances sont combinés avec des événements Windows et un timer haute résolution pour le fallback XInput. Le mapping complet est sans allocation dans le chemin chaud.

### DualSense virtuelle et feedback

Le bridge crée une DualSense USB virtuelle via VIIPER patché et usbip-win2 signé. Il vérifie le firmware émulé, traduit immédiatement chaque rapport physique complet et utilise seulement un keepalive en absence de rapport.

Le lecteur TCP VIIPER est tamponné. L'audio haptique est agrégé par fenêtres de 5 ms en conservant énergie, pic et transitoire. Les effets de gâchettes contournent l'agrégation. Les commandes FORCEADAPT et vibrations sont dédupliquées, limitées et restaurées à zéro/Normal à l'arrêt. Pour les rapports HID, `HAPTICS_SELECT` choisit seulement le mode haptique et ne valide pas les octets moteurs : une mise à jour grip exige `COMPATIBLE_VIBRATION` ou `COMPATIBLE_VIBRATION2`. Ce correctif évite les vibrations quasi permanentes observées dans Call of Duty.

La release 0.3.0 conserve `VIIPER v0.6.1-steamless9` comme backend validé. La branche de développement contient désormais un prototype reproductible `v0.7.0-asb2`, basé exactement sur le commit upstream `6b71b148a2243fab77ee1a46f4e22e00bd7d5a04`. Il garde l'API publique libVIIPER v0.7, adapte séparément le protocole 0.3.x et réintroduit le rapport complet des gâchettes, le descripteur audio composite et les transferts USB/IP isochrones. Un VIIPER upstream non patché est explicitement refusé afin d'éviter une régression silencieuse du feedback. `asb2` restaure aussi le descripteur HID standard exact de 273 octets ; le descripteur partagé de 427 octets de v0.7 contient des rapports Edge incompatibles avec l'identité PID `0x0CE6` et n'était pas reconnu par Call of Duty.

Le prototype v0.7.0-asb2 passe toute la suite Go upstream, les 13 tests natifs et un cycle matériel réel de trois secondes : firmware virtuel `0x0630` vérifié, rapports HID reçus, état neutralisé, APEX restaurée et aucune perte. Il reste expérimental jusqu'à validation en jeu des gâchettes et vibrations/audio dans Spider-Man 2 et Call of Duty ; il ne remplace donc pas encore le payload de l'installateur.

### Séquence `Ready`

Playnite reçoit `Ready` uniquement après validation simultanée de :

1. identité APEX et source physique ouverte ;
2. DualSense virtuelle créée ;
3. rapport neutre complet accepté ;
4. interfaces physiques effectivement masquées ;
5. watchdog et restauration RunOnce armés.

Le protocole IPC Playnite reste en version 1.

### Arrêt et prévention des relances

Les journaux Playnite ont montré que certains arrêts étaient suivis d'un second démarrage du même jeu 1,6 à 2,9 secondes plus tard. L'extension n'appelait pas elle-même l'API de lancement : un état `A/Cross` résiduel atteignait Playnite Plein écran lors du retour de focus.

La correction est double :

- le moteur envoie un état DualSense neutre pendant 35 ms, puis attend jusqu'à 1,5 s que boutons, D-pad et gâchettes physiques soient relâchés et stables 120 ms avant la restauration HidHide ;
- l'extension refuse pendant quatre secondes une demande de redémarrage du même jeu après `OnGameStopped`.

### Consommation et télémétrie

Les binaires C++ utilisent `/O2`, `/Gy`, LTCG, élimination du code mort et runtime MSVC statique. La télémétrie JSON facultative fournit latences p50/p95/p99, fréquences physiques/virtuelles, pertes/coalescences/keepalives, CPU, working set et étapes d'initialisation.

Mesure matérielle courte après correction du timeout de connexion loopback :

- source physique : 776 à 804 Hz selon les mouvements ;
- latence de traduction avec mouvements : p99 0,635 ms ;
- moteur : 0,48 à 0,52 % CPU et environ 12,4 à 12,6 Mio de working set ;
- working set total mesuré (moteur + watchdog + VIIPER) : environ 39,2 Mio ;
- aucune transition perdue ;
- initialisation totale : 0,628 à 0,794 s.

Le délai précédent d'environ 2,9 s venait d'un `connect()` loopback bloquant : les timeouts de socket ne couvraient que `send/recv`. Le connect non bloquant borné supprime ce coût avant le lancement de VIIPER. La lecture audio initiale ne parcourt plus non plus la topologie de chaque endpoint existant ; seules les propriétés d'un nouvel endpoint sont résolues en parallèle.

Les cibles CPU, mémoire totale, latence p99 et initialisation à chaud sont désormais respectées sur la machine de référence.

## Playnite et UX

L'utilisateur ne choisit plus `ApexSenseBridge.exe` ni un index XInput. `InstallLocator` résout le moteur via `HKLM\Software\ApexSenseBridge`, avec repli Program Files et migration d'un ancien chemin uniquement si nécessaire.

Les profils, le seuil haptique et les préférences existants sont conservés. Les champs historiques `BridgeExecutablePath` et `XInputIndex` restent désérialisables mais ont disparu de l'interface et de la ligne de commande normale.

Le panneau Win32 est lancé à la demande et n'est pas résident. Il permet le diagnostic, le test de l'APEX, la restauration HidHide/WGI, l'ouverture des journaux et la désinstallation complète explicite.

## Installation et désinstallation

`ApexSenseBridge-Setup.exe` est un installateur Inno Setup hors ligne demandant une seule élévation et n'affichant aucun PowerShell. Il embarque :

- moteur et panneau natifs à runtime statique ;
- VIIPER patché et licences/source correspondantes ;
- usbip-win2 0.9.7.7 ;
- HidHide 1.5.230 ;
- extension Playnite 0.3.0 ;
- manifeste précis des ProductCode, INF, services, certificats et hashes.

usbip-win2 0.9.7.8 est explicitement refusé à cause de l'avertissement officiel de corruption mémoire/BSOD.

En cas de crash moteur pendant une session Playnite, le watchdog conserve désormais l'APEX masquée jusqu'au signal de fin du jeu avant de restaurer HidHide. La désinstallation envoie d'abord un événement global d'arrêt et attend l'accusé de fin émis après neutralisation, détachement VIIPER et restauration visibilité/WGI. `taskkill` ne sert plus que de secours borné pour un moteur bloqué. Elle supprime ensuite extension, profils, logs, RunOnce, fichiers et registre, puis retire les dépendances dont ApexSenseBridge est propriétaire. Une dépendance préexistante est conservée par défaut ; l'action de désinstallation complète du panneau accepte explicitement `/REMOVEDEPENDENCIES`.

## Validation

La suite native couvre notamment :

- tous les axes, gâchettes, boutons, huit directions D-pad et combinaisons simultanées ;
- 10 000 transitions de bouton sans perte ;
- traduction de gâchettes, haptique, garde audio et geste touchpad ;
- IPC, arrêt, fausse VIIPER et cycle de la DualSense Windows ;
- port VIIPER paramétrable pour ne pas entrer en collision avec une session réelle.

La suite actuelle comporte 13 tests. La validation matérielle confirme la neutralisation avant restauration, la visibilité finale de l'APEX et l'absence de perte sur la session mesurée.

## Fichiers principaux

```text
src/platform/PhysicalInputSource.h
src/platform/windows/WindowsPhysicalInputSource.cpp
src/platform/windows/WindowsPhysicalControllerIsolation.cpp
src/platform/windows/WindowsVirtualDualSense.cpp
src/platform/SessionControl.cpp
src/main.cpp
playnite/ApexSenseBridge/ApexSenseBridge.cs
playnite/ApexSenseBridge/InstallLocator.cs
installer/ApexSenseBridge.iss
installer/driver-manifest.json
scripts/build-installer.ps1
scripts/build-viiper-070-windows.ps1
third_party/viiper-patches/viiper-v0.7.0-asb.patch
```
