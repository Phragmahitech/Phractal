# Phractal — by Phragma

Effet audio VST3 pour macOS et Windows : découpe le son en slices calées sur le tempo et les rejoue en boucles glitch, d'une mesure entière jusqu'au 1/4096 de note.

**v0.2.0 — version simplifiée, sur le modèle de Ninja Slicer.** Une seule chance pour tous les pas (au lieu d'une par pas), une durée de slice, une enveloppe attaque / relâchement, un gate en pulse régulier, et un pitch élargi à ±48 demi-tons. Tout est vérifié par des mesures (voir « Tests »), mais rien n'a été réglé à l'oreille. Les trois dés de modulation aléatoire et les presets viendront ensuite.

## Principe

Phractal se pose en insert. La mesure est découpée en 1 à 32 pas (GRID). À chaque pas, un tirage au sort (CHANCE) décide si le pas devient une **slice** : le son qui entre à cet instant est enregistré et rejoué en boucle pendant DURATION. La longueur de la boucle est tirée au hasard pour chaque slice, entre LONG et SHORT.

- Le premier passage d'une slice est le son qui entre, tel quel : rien n'est lu dans le futur, il n'y a pas de latence.
- Une boucle plus courte qu'environ 10 ms ne s'entend plus comme une répétition mais comme une note, dont la hauteur suit le tempo. À 190 BPM : 1/128 ≈ 100 Hz, 1/512 ≈ 405 Hz, 1/1024 ≈ 811 Hz, 1/4096 ≈ 3,2 kHz.
- La grille suit la mesure de l'hôte quand son transport tourne ; à l'arrêt, elle avance seule.
- Tout éteint, le son passe intact, à l'échantillon près.

## Les cinq modules

- **SLICE**
  - **SLICE** : allume les slices.
  - **CHANCE** (grand potard) : la chance qu'un pas devienne une slice, la même pour tous les pas.
  - **THRU** : ce qu'on entend sans slice (et après elle). **OFF** : rien. **ON** : l'entrée. **RANDOM** : un tirage à chaque pas.
  - **GRID** : 1 à 32 pas par mesure (la mesure suit la signature de l'hôte).
  - **DUR** : la part du pas que dure la slice (5 à 100 %). Après, THRU prend le relais.
  - **ATTACK** / **RELEASE** : fondus d'entrée et de sortie de la slice, en part de sa durée. Le relâchement finit exactement à la fin de la slice, avec un minimum d'1 ms.
- **LOOP**
  - **LONG** (grand potard) et **SHORT** : chaque slice tire sa boucle entre les deux, bornes comprises, pointées et triolets compris. LONG = SHORT : toujours la même longueur.
  - **SYNC** allumé : de 1/1 à 1/4096. Éteint : de 1 s à 0,1 ms, tirage logarithmique.
  - L'afficheur **RANGE** montre la plage, sa durée au tempo actuel (en Hz pour une boucle très courte) et la dernière longueur tirée.
- **PITCH**
  - Transpose les slices de **-48 à +48 demi-tons**, durée conservée. L'entrée qui passe en THRU n'est pas transposée. **SEMI** : par demi-tons. **FREE** : au centième.
  - Au-delà d'une octave vers l'aigu, les grains du pitch-shifter raccourcissent (jusqu'à 4 ms) pour que le son transposé n'arrive jamais plus de ~60 ms en retard (environ 20 ms mesurés). Le son devient plus granuleux, et les transitoires plus raides : c'est la contrepartie des extrêmes. Pas de filtre anti-repliement vers l'aigu.
  - **OUTPUT** : **MIX** (dry / wet), **LEVEL** (-24 à +12 dB) et **LIMITER** (0 dBFS, en toute fin de chaîne, allumé par défaut).
- **GATE**
  - Un pulse régulier sur tout ce qui sort, de 1/1 à 1/64 (**RATE**), en normal, triolet ou pointé.
  - **LENGTH** : part du pas où le gate est ouvert. **SMOOTH** : lissage des fronts, jusqu'à un demi-pas.
- **FILTER**
  - Passe-haut, passe-bande ou passe-bas, 12 ou 24 dB par octave (24 par défaut), avec résonance, sur tout ce qui sort.

L'interface se redimensionne de 60 % à 150 % (coin en bas à droite ou bouton de taille dans l'en-tête). La taille choisie est gardée avec le projet.

Les projets enregistrés avec la v0.1 ne retrouvent pas leurs réglages (les paramètres ont changé).

## Presets et taille de la fenêtre

En haut de la façade : `‹  NOM ▾  ›  SAVE  100 %`.

- **‹ ›** : preset précédent / suivant (DEFAULT, puis les presets enregistrés, par ordre alphabétique, en boucle).
- **Nom** : menu des presets, SAVE AS…, DELETE (le fichier part à la corbeille), SHOW PRESETS FOLDER. Une étoile `*` signale un preset modifié.
- **SAVE** : panneau dans la façade ; Entrée = enregistrer, Échap = annuler. Si le nom existe, le bouton devient REPLACE.
- **100 %** : taille de la fenêtre (60, 75, 100, 125, 150 %). On peut aussi tirer le coin en bas à droite. La taille est gardée avec le projet.

Les presets sont des fichiers `.phcpreset` (XML, valeurs en unités réelles) :
- macOS : `~/Library/Audio/Presets/Phragma/Phractal/`
- Windows : `%APPDATA%\Phragma\Phractal\Presets\`

Un preset garde tous les paramètres. Le projet du DAW garde le nom du preset en cours. Un paramètre absent du fichier revient à sa valeur par défaut ; une valeur hors plage est bornée ; un fichier illisible est refusé sans rien changer.

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
  - le pitch, de -48 à +48 ;
  - l'enveloppe ;
  - le THRU sans bosse ni clic ;
  - la durée des slices ;
  - le calage sur l'hôte ;
  - le gate ;
  - le tirage des longueurs ;
  - l'arrêt ;
  - des réglages au hasard de 44,1 à 192 kHz.
- `PhractalPluginTests`, le plugin complet : paramètres, slices calées sur la mesure (avec DURATION), gate, mix, niveau, limiteur, micro-boucles et pitch, sauvegarde, bypass et interface. Si on lui donne un dossier, il y écrit des captures de l'interface.
- `PhractalRender <dossier>` : rend des extraits audio d'une boucle synthétique à 190 BPM.
