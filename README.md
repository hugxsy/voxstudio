# Vox Studio

Mixeur audio et traitement de la voix pour Windows, alternative simple à Voicemeeter.

- Voix : anti-bruit IA (RNNoise), noise gate, timbre, niveleur, compresseur, anti-sifflantes, brillance, chaleur analogique, hauteur, écho, réverb, effets spéciaux, limiteur. Sortie vers VB-Cable.
- Mixeur : volume et coupure par application.
- Sorties : sortie principale en un clic, diffusion sur plusieurs sorties.
- Mises à jour automatiques depuis `release/latest.json`.

## Compiler (depuis Linux, MinGW-w64)

    sudo apt install mingw-w64
    ./build.sh

## Publier une version

1. Monter `VOX_VERSION` dans `src/updater.h`, compiler.
2. Copier `build/VoxStudio.exe` dans `release/VoxStudio-X.Y.Z.exe` (un nom par version, pour éviter le cache), mettre à jour `release/latest.json`.
3. Commit + push sur `main` : les installations existantes proposent la mise à jour.
