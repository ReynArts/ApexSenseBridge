# Extension Playnite ApexSenseBridge

Cette extension détecte automatiquement les jeux disposant d'un profil DualSense vérifié. Elle attend que l'APEX soit entièrement isolée et que la DualSense virtuelle soit prête avant d'autoriser Playnite à lancer le jeu, puis restaure proprement le contrôleur à l'arrêt.

Les jeux inconnus ou explicitement désactivés utilisent directement l'APEX en XInput natif et ne démarrent aucun processus ApexSenseBridge.

## Utilisation

1. Installer `ApexSenseBridge-Setup.exe`, puis redémarrer Windows si l'installateur le demande.
2. Lancer normalement un jeu reconnu depuis Playnite Bureau ou Plein écran : le profil est sélectionné automatiquement.
3. Pour forcer un profil ou désactiver le bridge, faire un clic droit sur le jeu dans Playnite Bureau et ouvrir `ApexSenseBridge`.

Il n'y a plus de fichier `ApexSenseBridge.exe` ni d'index XInput à sélectionner. Le chemin du moteur est lu automatiquement dans `HKLM\Software\ApexSenseBridge`. Les anciens champs sont uniquement conservés pour migrer une configuration existante.

Pendant un profil DualSense, tous les sticks, boutons, gâchettes et directions transitent par le bridge ; HidHide empêche le jeu de voir les interfaces physiques. Si cette isolation ou le proxy intégral échoue, le lancement est annulé.

À la fermeture du jeu, l'extension neutralise d'abord la DualSense virtuelle et le moteur attend le relâchement des commandes avant de rendre l'APEX physique visible. Une fenêtre anti-rebond de quatre secondes bloque aussi un faux second lancement du même jeu provoqué par un appui `A/Cross` résiduel dans l'interface Plein écran.

Le profil Spider-Man 2 applique en plus la correction WGI temporaire et refuse le lancement si Steam conserve déjà un handle vers l'APEX physique. Aucun fichier du jeu n'est modifié.

Les profils tactiles disponibles sont Spider-Man 2, Miles Morales, Ghost of
Tsushima et Warframe. Un jeu inconnu ne reçoit aucun swipe synthétique : `View`
reste simplement le clic du touchpad. Le profil Warframe suppose le layout Xbox
par défaut et doit rester désactivé si les boutons du jeu ont été remappés.

La détection compare une version normalisée du titre Playnite, puis le nom du
dossier d'installation. Un override manuel est toujours prioritaire. Le choix
`Désactiver pour ce jeu` est persistant ; `Utiliser la détection automatique`
supprime cet override. La détection globale peut aussi être coupée dans les
paramètres de l'extension.

## Compilation

Le projet cible .NET Framework 4.6.2 et Playnite SDK 6.16. Depuis la racine du dépôt :

```powershell
.\scripts\build-playnite-extension.ps1
```

Le script utilise l'installation Playnite locale lorsqu'elle existe. Sur une machine de CI vierge, il télécharge et vérifie le paquet NuGet officiel PlayniteSDK épinglé, puis crée le paquet `.pext` compatible avec l'installateur ZIP officiel de Playnite.
