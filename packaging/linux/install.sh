#!/bin/sh
# Installs ApexSenseBridge for the current user. The udev rules are the only
# step that needs root, and they are the only thing written outside $HOME.
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
SOURCE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SOURCE_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

if [ ! -x "$BUILD_DIR/ApexSenseBridge" ]; then
    echo "Build the engine first:" >&2
    echo "  cmake -S '$REPO_ROOT' -B '$BUILD_DIR' -DCMAKE_BUILD_TYPE=Release" >&2
    echo "  cmake --build '$BUILD_DIR' -j\$(nproc)" >&2
    exit 1
fi

mkdir -p "$PREFIX/bin" "$PREFIX/share/apexsensebridge"
install -m 0755 "$BUILD_DIR/ApexSenseBridge" "$PREFIX/bin/ApexSenseBridge"
install -m 0755 "$SOURCE_DIR/asb-run" "$PREFIX/bin/asb-run"
install -m 0644 "$REPO_ROOT/data/supported_games.json" \
    "$PREFIX/share/apexsensebridge/supported_games.json"
echo "Installed engine and asb-run into $PREFIX/bin"

# Every rule in udev/ is installed, not one by name, so a rule added later is
# not silently left out of an upgrade.
stale=""
for src in "$SOURCE_DIR"/udev/*.rules; do
    dest="/etc/udev/rules.d/$(basename "$src")"
    if [ ! -f "$dest" ] || ! cmp -s "$src" "$dest"; then
        stale="$stale $src"
    fi
done

if [ -n "$stale" ]; then
    echo "Installing udev rules (needs root)..."
    for src in $stale; do
        dest="/etc/udev/rules.d/$(basename "$src")"
        sudo install -m 0644 "$src" "$dest"
        echo "Installed $dest"
    done
    sudo udevadm control --reload
    sudo udevadm trigger --subsystem-match=hidraw --subsystem-match=input
else
    echo "udev rules already current"
fi

# The libVIIPER backend needs the USB/IP client module, group access to its two
# sysfs control files, and a writable place for usbip(8) to record connections.
# Without the last one usbip attaches the device and then fails on the record,
# which surfaces as an attach failure. The uhid backend needs none of this.
for dir in modules-load.d tmpfiles.d; do
    [ -d "$SOURCE_DIR/$dir" ] || continue
    for src in "$SOURCE_DIR/$dir"/*.conf; do
        [ -f "$src" ] || continue
        dest="/etc/$dir/$(basename "$src")"
        if [ ! -f "$dest" ] || ! cmp -s "$src" "$dest"; then
            echo "Installing $dest (needs root)..."
            sudo install -m 0644 "$src" "$dest"
        fi
    done
done

if ! lsmod 2>/dev/null | grep -q '^vhci_hcd'; then
    echo "Loading vhci-hcd now (needs root)..."
    sudo modprobe vhci-hcd || echo "  could not load vhci-hcd; the uhid backend still works"
fi
sudo systemd-tmpfiles --create /etc/tmpfiles.d/apexsensebridge-vhci.conf 2>/dev/null || true
# --action=add, not the default 'change': 72-apexsensebridge-vhci.rules matches
# ACTION=="add", so on a machine where vhci_hcd was already loaded - which is
# every machine after the first install - a 'change' event matches nothing and
# the permissions are silently never granted. settle so this script does not
# finish before the rule has actually run.
sudo udevadm trigger --action=add \
    --subsystem-match=platform --attr-match=driver=vhci_hcd 2>/dev/null || true
sudo udevadm settle --timeout=10 2>/dev/null || true

# The kernel-side prerequisites above are what makes the automatic choice prefer
# libVIIPER, but the library itself is not built or shipped by this script. Say
# so plainly: a silent mismatch here used to end as a failed session rather than
# a session without haptics.
if [ -f "$PREFIX/bin/libVIIPER.so" ]; then
    echo "libVIIPER.so found; DualSense audio haptics are available."
else
    echo
    echo "No libVIIPER.so in $PREFIX/bin - the bridge will use the uhid backend."
    echo "  Adaptive triggers, PlayStation prompts and rumble all work."
    echo "  DualSense audio haptics do not: uhid creates no audio endpoint."
    echo "  Build libVIIPER as a c-shared library and drop it beside the engine"
    echo "  to enable them. Force the plain backend any time with:"
    echo "      ApexSenseBridge bridge-triggers --virtual-backend uhid"
fi

if ! id -nG | tr ' ' '\n' | grep -qx input; then
    echo
    echo "WARNING: $(id -un) is not in the 'input' group, which /dev/uhid needs."
    echo "  sudo usermod -aG input $(id -un)   # then log out and back in"
fi

echo
echo "Verify with:  $PREFIX/bin/ApexSenseBridge list"
