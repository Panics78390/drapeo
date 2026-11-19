# Consignes pour Claude — projet Drapeo

- Répondre à l'utilisateur en français.
- **Chaque mise à jour du logiciel est publiée à deux endroits :**
  1. Ce dépôt GitHub (`Panics78390/drapeo`, branche `main`) : code source + `windows/drapeo.exe` recompilé.
  2. Le PC de l'utilisateur : `C:\Users\marin\Downloads\Drapeo\drapeo.exe` (remplacer le fichier existant,
     ne jamais créer de copies `-v2`). Si Windows refuse parce que le fichier est verrouillé, demander
     à l'utilisateur de fermer Drapeo (Gestionnaire des tâches si besoin), puis réessayer.
- Exe Windows : compilation croisée mingw-w64 (toolchain `x86_64-w64-mingw32-g++-posix`, liens statiques),
  puis `x86_64-w64-mingw32-strip`. L'exe ne doit dépendre que des DLL système.
- Lancer `drapeo_tests` avant de publier.
- Logo : `assets/logo/drapeo-logo.png` ; régénérer l'icône et `src/app/LogoData.h` avec `python3 tools/make_logo_assets.py`.
- Ne pas copier l'apparence, la marque ou les fichiers propriétaires d'un autre logiciel.
