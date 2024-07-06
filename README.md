# harmonica_Midi

------------     Stade idée     ------------------------

## Introduction
Ce projet vise à créer un harminica électromécanique contrôlé via USB MIDI en utilisant un microcontrôleur Arduino, des cartes MCP9685, des transistors et des électroaimants ou des servomoteurs.
l'idée est d'imprimer un systeme de valve avec 2 entrées et une sortie pour chacun des trous de l'harmonica.
Nous utiliserons un moteur pas a pas pour faire varier le volume de deux reservoirs et utiliser l'aspiration ou le soufflage pour les notes midi demandées.
chacun des deux reservoir devras avoir :
- un volume d'air deplacé d'environ 2 a 5 litres
- un capteur de pression
- un capteur fin de course
- un passage d'air vers la distribution/electrovannes
- un systeme de valve de securité pour pression ou vide trop important 
- une electrovanne pour controller l'ouverture du reservoir 
![Schéma reservoirs_Air](images/scheme.png)
Les reservoirs  pourrons etre fait avec des systeme de soufflets ou avec un tube (transparent) et des systemes de joints torique ou plat adapté.


il est possible d'utiliser des electrovanne du marché mais cela va enormement augmenter le coup total, je vais donc essayer de modeliser et imprimer des systeme de valves




## Schéma electrique
![Schéma Électrique](images/scheme.png)

## Installation et Configuration


