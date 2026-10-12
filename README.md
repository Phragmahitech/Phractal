# Phractal — by Phragma

Effet audio VST3 pour macOS et Windows : découpe le son en slices calées sur le tempo et les rejoue en boucles glitch, d'une mesure entière jusqu'au 1/4096 de note.

**v0.1.0 — première version à écouter.** Tout ce qui est décrit ici a été vérifié par des mesures (voir « Tests »), mais rien n'a encore été réglé à l'oreille. Les trois dés de modulation aléatoire et les presets viendront en v0.2.

## Principe

Phractal se pose en insert. La mesure est découpée en 1 à 32 pas. À chaque pas, un tirage au sort (la chance de ce pas) décide si le pas devient une **slice** : le son qui entre à cet instant est enregistré et rejoué en boucle jusqu'à la fin du pas. La longueur de la boucle est tirée au hasard pour chaque slice, entre MIN et MAX.

- Le premier passage d'une slice est le son qui entre, tel quel : rien n'est lu dans le futur, il n'y a pas de latence.
- Une boucle plus courte qu'environ 10 ms ne s'entend plus comme une répétition mais comme une note, dont la hauteur suit le tempo. À 190 BPM : 1/128 ≈ 100 Hz, 1/512 ≈ 405 Hz, 1/1024 ≈ 811 Hz, 1/4096 ≈ 3,2 kHz.
- Les grilles suivent la mesure de l'hôte quand son transport tourne ; à l'arrêt, elles avancent seules.
- Tout éteint, le son passe intact, à l'échantillon près.

## Les cinq modules

- **SLICE**
  - **SLICE** : allume les slices. Le premier part au pas suivant.
  - **STEPS** : 1 à 32 pas par mesure (la mesure suit la signature de l'hôte, 4/4 par défaut).
  - **THRU** : ce qu'on entend pendant les pas sans slice. **SILENT** : rien. **INPUT** : l'entrée. **RANDOM** : un tirage à chaque pas, l'un ou l'autre.
  - **ENVELOPE** : ATT, DEC, SUS, REL. Attaque, déclin et relâchement en pourcentage du pas, maintien en niveau. Le relâchement finit exactement à la fin du pas, avec un minimum d'1 ms.
- **LOOP**
  - **MAX** (grand potard) et **MIN** : les longueurs entre lesquelles chaque slice tire sa boucle. Tourner à droite raccourcit. **SYNC** : de 1/1 à 1/4096, avec pointées et triolets. **FREE** : de 1 s à 0,1 ms, tirage logarithmique.
  - **RANDOM** : éteint, la boucle fait toujours MAX.
  - **TRIPLET** / **DOTTED** : triolets et pointées admis ou non dans le tirage. Les deux bornes restent toujours admises.
  - L'afficheur **RANGE** montre la plage, sa durée au tempo actuel (en Hz pour une boucle très courte) et la dernière longueur tirée.
- **PITCH**
  - Transpose les slices de -24 à +24 demi-tons, durée conservée. L'entrée qui passe en THRU n'est pas transposée. C'est le pitch-shifter de Phragments.
  - **SEMI** : par demi-tons. **FREE** : au centième.
  - **PITCH** : allume et éteint la transposition.
- **GATE**
  - Gate rythmique sur tout ce qui sort : 2 à 32 pas (**STEPS**), de 1/1 à 1/64 (**RATE**), en normal, triolet ou pointé.
  - **LENGTH** : part du pas où le gate est ouvert. **SMOOTH** : lissage des fronts, jusqu'à un demi-pas.
  - Le motif se dessine dans la rangée **GATE** en bas.
- **FILTER**
  - Passe-haut, passe-bande ou passe-bas, 12 ou 24 dB par octave (24 par défaut), avec résonance, sur tout ce qui sort.
  - **OUTPUT** : **MIX** (dry / wet), **LEVEL** (-24 à +12 dB) et **LIMITER** (0 dBFS, en toute fin de chaîne, allumé par défaut).

En bas, la **séquence** :

- **CHANCE** : la chance de chaque pas, dessinée à la souris. Un glissé règle tous les pas qu'il traverse ; un double-clic remet un pas à 50 %. Le pas en cours s'allume, et un trait marque les pas devenus slices au dernier passage.
- **GATE** : un clic allume ou éteint un pas. Un glissé peint tous les pas traversés dans l'état inverse du premier.

L'interface se redimensionne de 60 % à 150 % par le coin en bas à droite. La taille choisie est gardée avec le projet.

## Trajet du son

entrée → slices (ou THRU) → pitch des slices → gate → filtre → mix avec l'entrée → niveau → limiteur

## Compiler

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Phractal_VST3
```

JUCE 8.0.15 est téléchargé automatiquement. Pour une copie locale : `-DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE`.

## Tests

Avec `-DPHRACTAL_BUILD_TESTS=ON` :

- `PhractalEngineTests`, le moteur seul, sans JUCE. Il couvre :
  - le passage intact ;
  - la répétition exacte du premier passage ;
  - la hauteur des micro-boucles ;
  - le pitch ;
  - l'enveloppe ;
  - le THRU sans bosse ni clic ;
  - le calage sur l'hôte ;
  - le gate ;
  - le tirage des longueurs ;
  - l'arrêt ;
  - des réglages au hasard de 44,1 à 192 kHz.
- `PhractalPluginTests`, le plugin complet : paramètres, slices calées sur la mesure, gate, mix, niveau, limiteur, micro-boucles et pitch, sauvegarde, bypass et interface. Si on lui donne un dossier, il y écrit des captures de l'interface.
- `PhractalRender <dossier>` : rend des extraits audio d'une boucle synthétique à 190 BPM.
