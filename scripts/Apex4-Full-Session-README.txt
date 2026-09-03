ApexSenseBridge - test de session complete APEX 4
=================================================

Ce petit paquet ne contient pas l'installation complete. Il exige toutefois
que usbip-win2 0.9.7.5 a 0.9.7.7 et HidHide 1.5.230 soient deja installes,
puis que Windows ait ete redemarre.

1. Extraire tout le ZIP dans un dossier.
2. Connecter l'APEX 4 par cable ou dongle 2,4 GHz en mode DInput.
3. Fermer Flydigi Space Station.
4. Pour un jeu Steam, desactiver Steam Input dans ses proprietes.
5. Double-cliquer sur Test-Apex4-Full-Session.cmd et accepter l'UAC.

Le script cree un DualSense virtuel nomme "Wireless Controller" et cache
temporairement les interfaces manette et souris auxiliaire de l'APEX. Pour
verifier l'absence de double entree, il demande d'abord de bouger les sticks,
les gachettes et les boutons pendant 6 secondes. Il controle alors les
evenements reellement recus par un processus non autorise ; la simple presence
d'une interface dans une liste Windows n'est plus consideree comme une fuite.

Le script ouvre ensuite joy.cpl. Tester tous les controles dans les proprietes
de Wireless Controller. Pour valider les retours du jeu, lancer ensuite un
titre PC compatible DualSense pendant que la session reste active.

Avec le dongle, l'identite est verifiee directement par cette session puis
conservee jusqu'a l'arret ; ne pas lancer un autre outil APEX en parallele.

Revenir dans la fenetre du test et appuyer sur Entree pour arreter. Le script
remet les gachettes au repos, arrete les vibrations, detache le DualSense et
restaure la visibilite de l'APEX. Une securite arrete aussi la session apres
10 minutes au maximum.

Renvoyer le fichier Apex4-Full-Session-Test-*.zip cree sur le Bureau. Meme si
une verification echoue avant le demarrage du bridge, la version 1.5.1 conserve
l'erreur dans un ZIP et garde la fenetre ouverte. Si le Bureau n'est pas
accessible, le ZIP est place directement dans le dossier du testeur.

Si le script annonce que les pilotes sont absents, utiliser une seule fois
Install-Drivers.cmd du paquet ApexSenseBridge-Portable, redemarrer Windows,
puis revenir a ce petit paquet.
