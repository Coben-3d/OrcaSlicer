#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Erreur: ce script doit etre execute sur macOS." >&2
    exit 1
fi

cd "${REPO_ROOT}"

echo "[1/4] Build macOS..."
./build_release_macos.sh

echo "[2/4] Recherche de OrcaSlicer.app..."
app_path=""
for candidate in \
    "${REPO_ROOT}/build/arm64/src/Release/OrcaSlicer.app" \
    "${REPO_ROOT}/build/x86_64/src/Release/OrcaSlicer.app"; do
    if [[ -d "${candidate}" ]]; then
        app_path="${candidate}"
        break
    fi
done

if [[ -z "${app_path}" ]]; then
    app_path="$(find "${REPO_ROOT}/build" -type d -path '*/src/Release/OrcaSlicer.app' | head -n 1 || true)"
fi

if [[ -z "${app_path}" || ! -d "${app_path}" ]]; then
    echo "Erreur: OrcaSlicer.app introuvable apres build." >&2
    exit 1
fi

arch="$(uname -m)"
if [[ "${app_path}" == *"/build/arm64/"* ]]; then
    arch="arm64"
elif [[ "${app_path}" == *"/build/x86_64/"* ]]; then
    arch="x86_64"
fi

git_hash="$(git rev-parse HEAD)"
build_date_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
date_tag="$(date -u '+%Y%m%d')"
macos_version="$(sw_vers -productVersion)"

artifacts_dir="${REPO_ROOT}/build/release"
mkdir -p "${artifacts_dir}"

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/orcaslicer-beta.XXXXXX")"
cleanup() {
    rm -rf "${stage_dir}"
}
trap cleanup EXIT

echo "[3/4] Preparation du package..."
cp -R "${app_path}" "${stage_dir}/OrcaSlicer.app"

cat > "${stage_dir}/BUILD_INFO.txt" <<EOF
project=OrcaSlicer-AI
git_hash=${git_hash}
build_date_utc=${build_date_utc}
macos=${macos_version}
arch=${arch}
app_path=${app_path}
EOF

zip_name="OrcaSlicer-AI-beta-${arch}-${date_tag}-${git_hash:0:7}.zip"
zip_path="${artifacts_dir}/${zip_name}"

echo "[4/4] Creation de l'archive..."
(
    cd "${stage_dir}"
    /usr/bin/zip -qry "${zip_path}" "OrcaSlicer.app" "BUILD_INFO.txt"
)

echo "ZIP cree: ${zip_path}"
