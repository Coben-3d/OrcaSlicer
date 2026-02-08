# Releasing macOS Beta (AI Branch)

Ce guide explique comment construire et empaqueter une beta macOS de OrcaSlicer-AI.

## Prerequis

- macOS avec Xcode/Command Line Tools installes.
- Dependances de build deja configurees pour OrcaSlicer.
- Depot a jour et propre (`git status`).

## Build + Package (script unique)

Depuis la racine du repo:

```bash
./scripts/release/macos_beta_package.sh
```

Le script effectue:

1. `./build_release_macos.sh`
2. Detection de `OrcaSlicer.app` genere
3. Generation de `BUILD_INFO.txt` (hash git, date, macOS, architecture)
4. Creation d'une archive zip versionnee:
   - Exemple: `OrcaSlicer-AI-beta-arm64-YYYYMMDD-<hash7>.zip`

Sortie attendue:

- `build/release/OrcaSlicer-AI-beta-*.zip`

## Validation rapide avant publication

- Ouvrir l'app locale et verifier l'ecran principal.
- Verifier que le panneau AI est present.
- Verifier que `BUILD_INFO.txt` est bien inclus dans le zip.

## Publication beta (exemple)

1. Uploader le zip comme asset d'une pre-release GitHub.
2. Ajouter les points de test attendus dans les release notes.
3. Inclure commit hash et date de build.

## Optionnel: Code signing / Notarization (placeholders)

Selon votre process de release, vous pouvez ajouter:

- Signature de `OrcaSlicer.app` avec certificat Developer ID.
- Notarization Apple du paquet.
- Stapling du ticket notarization.

Placeholder commandes:

```bash
# codesign placeholder
codesign --deep --force --verify --verbose --sign "Developer ID Application: <TEAM>" build/.../OrcaSlicer.app

# notarization placeholder
xcrun notarytool submit <archive.zip> --keychain-profile "<PROFILE>" --wait

# staple placeholder
xcrun stapler staple build/.../OrcaSlicer.app
```
