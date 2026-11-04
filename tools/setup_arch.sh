#!/usr/bin/env bash
# Prépare un PC Arch Linux pour compiler SaveSync :
# dépôt pacman devkitPro + chaîne de compilation Switch (libnx, nxlink, portlibs).
# À lancer avec : sudo bash tools/setup_arch.sh
# Note : le -Syu peut mettre à jour le noyau ; redémarre le PC ensuite.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "Lance ce script avec sudo : sudo bash tools/setup_arch.sh" >&2
    exit 1
fi

DKP_KEY=BC26F752D25B92CE272E0F44F7FD5492264BB9D0

echo "==> Clé de signature devkitPro"
pacman-key --recv-keys "$DKP_KEY" --keyserver keyserver.ubuntu.com
pacman-key --lsign-key "$DKP_KEY"
pacman -U --noconfirm --needed https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst

echo "==> Dépôts devkitPro dans /etc/pacman.conf"
if ! grep -q '^\[dkp-libs\]' /etc/pacman.conf; then
    cp /etc/pacman.conf /etc/pacman.conf.bak-devkitpro
    cat >> /etc/pacman.conf <<'EOF'

[dkp-libs]
Server = https://pkg.devkitpro.org/packages

[dkp-linux]
Server = https://pkg.devkitpro.org/packages/linux/$arch/
EOF
fi

echo "==> Installation de switch-dev et des bibliothèques (curl, mbedtls, zlib...)"
pacman -Syu --needed switch-dev switch-curl switch-mbedtls switch-zlib

echo
echo "Terminé. devkitPro est dans /opt/devkitpro."
