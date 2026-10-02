#!/bin/bash
# Install a user .desktop entry so GNOME Dock shows the QEMU icon
# instead of a generic gear when launched from the terminal (Wayland).
install_toyos_dock_icon() {
    # CI / headless：勿写宿主桌面文件
    if [ "${TOY_HEADLESS:-0}" = 1 ]; then
        return 0
    fi
    local Apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    local Desktop="$Apps/toyos-qemu.desktop"
    local Icon=qemu
    if [ ! -f /usr/share/icons/hicolor/scalable/apps/qemu.svg ] && \
       [ ! -f /usr/share/icons/hicolor/32x32/apps/qemu.png ]; then
        Icon=computer
    fi
    mkdir -p "$Apps" 2>/dev/null || return 0
    cat > "$Desktop" <<EOF || return 0
[Desktop Entry]
Type=Application
Name=ToyOS (QEMU)
Comment=ToyOS virtual machine
Exec=qemu-system-x86_64
Icon=$Icon
Terminal=false
Categories=System;Emulator;
StartupNotify=true
StartupWMClass=qemu-system-x86_64
NoDisplay=true
EOF
    # Refresh desktop DB if available (ignore failures)
    update-desktop-database "$Apps" 2>/dev/null || true
}
