ApexSenseBridge - paquet de validation APEX 4 v1.3
================================================

Ce petit dossier ne s'installe pas et ne demande pas les droits administrateur.

Avant le test :
1. Brancher uniquement l'APEX 4 par cable USB ou dongle 2,4 GHz.
2. Mettre la manette en mode DInput (FN + A environ 3 secondes, ou menu ecran).
3. Fermer Flydigi Space Station.

Double-cliquer sur : Test-Apex4-Port.cmd

Le script demande d'abord l'autorisation pour les deux tests doux : vibration
des poignees, puis resistance temporaire de RT. Il execute ensuite identite,
entrees et effets dans une seule session afin de conserver l'identite verifiee
avec le dongle. Le programme remet les gachettes en mode Normal apres le test.

A la fin, renvoyer le fichier Apex4-Port-Test-*.zip cree sur le Bureau.

En cas d'echec de detection, lancer Collect-Apex4-Diagnostics.cmd et renvoyer
egalement son ZIP.

Ce paquet valide le nouveau transport materiel APEX 4. Il ne contient pas
l'installation complete du controleur DualSense virtuel ni ses pilotes.
