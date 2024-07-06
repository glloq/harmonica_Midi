# harmonica_Midi

------------     Stade idée     ------------------------

## Introduction
Ce projet vise à créer un harminica électromécanique contrôlé via USB MIDI en utilisant un microcontrôleur Arduino, des cartes MCP9685, des transistors et des électroaimants ou des servomoteurs.
l'idée est d'imprimer un systeme de valve avec 2 entrées et une sortie pour chacun des trous de l'harmonica.
Nous utiliserons un moteur pas a pas pour faire varier le volume de deux reservoirs et utiliser l'aspiration ou le soufflage pour les notes midi demandées.
chacun des deux reservoir devras avoir :
- un capteur de pression
- un capteur fin de course
- un passage d'air vers les electrovannes
- un systeme de valve de securité pour pression ou vide trop important


## Matériel Nécessaire
- Arduino (ex: Uno, Mega)
- Cartes MCP9685
- Transistors (ex: NPN)
- Électroaimants
- Câbles et connecteurs
- Source d'alimentation

## Schéma de Principe
![Schéma Électrique](images/scheme.png)

## Installation et Configuration

