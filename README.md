

> [!NOTE]
> c'est juste une idée !

------------------------------------------------------------------------------------------------

## Introduction
Ce projet vise à créer un harminica électromécanique contrôlé via USB MIDI en utilisant un microcontrôleur Arduino, des cartes MCP9685, des transistors et des électroaimants ou des servomoteurs.
L'objectif est de créer un systeme electromecanique qui permet de jouer parfaitement les notes midi recue (via usb)

#### Les messages midi executé

Nous commencerons par implementer les messages midi classique tel que les messages NoteOn ou NoteOff sans prendre en compte la velocité de la note dans un premier temps .
La seconde etape est d'ajouter la gestion de la velocité puis du volume.

On pourra ajouter facilement la gestion d'autre type demessage midi :
- Modulation (CC 1) : Utile pour simuler des effets comme le vibrato naturel de l'harmonica
- Breath Control (CC 2) : Modifie la dynamique ou l'intensité du son en fonction de la force du souffle.
- Expression (CC 11) : Permet des transitions fluides entre différentes dynamiques de jeu.
- Portamento (CC 5) : Ppour simuler des glissandos et des bends naturels dans le jeu d'un instrument à vent.

## Choix technique
en utilisant les pression d'air d'air d'un etre humain, on pêut estimer l'aspiration de -0.1 a -0.5KPa et le soufflage de 0.1 a 0.5KPa.


## Systeme de pompes

Le plus simple serait d'utiliser un tube de diametre 20cm et d'une hauteur de 35cm environ afin d'avoir autour de 10 litres d'airs dans les reservoirs.
il faut choisir le moteur et la tige fileté pour avoir un debit maximum de 10 a 15 litres par minutes =>  une tige fileté normale devrais sufire ? 
pour eviter les fuites nous pouvons utiliser des joints toriques pour sceller les assemblages.
On peut utiliser un joint plat decoupé pour isoler les deux reservoirs.
En jouant sur de design du piston, on peut utiliser le joint comme soupape de securité (avec un joint plat suffisament epais) 

<img src="img/design1.png" width=70% height=70%>

l'idée est d'utiliser le moteur pas a pas pour alterner l'aspiration et le soufflage sur les deux reservoirs en fonction de la postion du piston.
idealement il ne faut pas utiliser les fin de courses, l'idée est d'initialiser la position du piston sur l'un des deux coté au demarage puis de centrer le piston sans jouer de note (=> donc les deux valves open R1 et R2 ouverte) 
le code viendra alterner le sens de deplacement du piston en fonction de la distance restante des reservoir R1 et R2, l'objectif est de rester au centre le plus possible (l'harmonica jouant des melodies alternant le soufflage et l'aspiration, on evite donc trop d'utilisation des valves de distribution par la meme occasion )

## Distribution

il est possible d'utiliser des electrovanne du marché mais cela va enormement augmenter le coup total  
l'idée est d'imprimer un systeme de valve avec 2 entrées et une sortie pour chacun des trous de l'harmonica afin de selectionner l'aspiration ou le soufflage en fonction de la note midi demandé.

<img src="img/valve.png" width=40% height=40%>
Le deplacement de chaque valve sera fait avec un servomoteur.  
  
il faut respecter des contraintes de taille pour chaque passage d'air :
- pour chaque trous vers l'harmonica, il faudrait utiliser une surface de 7 a 15 mm² ( soit un diametre de 3 a 4.5mm) 
- pour chaque arrivé d'air, il faut adapter le diametre en fonction du nombre de note jouable en meme temps ( a adapter en fonction de la vitesse de deplacement de la partie centrale du reservoir) un diametre de 10 a 15mm devrais etre amplement sufisant pour un harmonica classique de 10 trous


## Materiel electrique

- un nema 17 
- un driver pour moteur pas a pas (idealement un pour deplacement silencieux  comme le DRV8825 ou TMC2208 ) 
- deux fin de course mecanique (le second est la par securité dans le cas ou il y aurait une perte de pas )
- deux capteurs de pression adapté pour chaque coté ( BMP180/BMP280  ou MPX2010 )
- un pca9685
- 12 servomoteurs (1 pour chaque trou d'harmonica et 2 pour les valves d'ouverture des reservoirs)
- alimentation 12v et 5v adapté
- arduino ou autre microcontroleur

## Schéma electrique

![Schéma Électrique](images/scheme.png)



