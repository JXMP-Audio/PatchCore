#!/usr/bin/env bash
# PatchCore launcher.
#
#   ./run-patchcore.sh                 the workbench: everything being worked on (newest modules)
#   ./run-patchcore.sh empty           an empty rack (also --empty)
#   ./run-patchcore.sh demo            the JXMP ambient patch (also --demo)
#   ./run-patchcore.sh showcase        the JXMP showcase voice (also --showcase)
#   ./run-patchcore.sh my-patch.vcv    open your own patch
#   ./run-patchcore.sh --build         fetch the sources and build PatchCore first (also done
#                                      automatically when it has not been built yet)
#   ./run-patchcore.sh --update        pull the newest version from GitHub and build it
#   ./run-patchcore.sh --install       add PatchCore to the application menu and a `patchcore`
#                                      command in ~/.local/bin (run it on the host)
#   ./run-patchcore.sh --uninstall     remove both again
#
# PatchCore connects its outputs to the default output itself (Engine → Audio Settings chooses the
# driver and the sound card). Started on the host, the script runs inside the distrobox the app is
# built in (PATCHCORE_CONTAINER, default CLAUDE).
set -euo pipefail

core="$(cd "$(dirname "$(realpath "$0")")" && pwd)"
bin="$core/bin/Cardinal"

# Rebuilds when a source file is newer than the app, so a start always shows the newest work
rebuild_if_needed() {
	local newer
	newer="$(find "$core/src" "$core/plugins/JXMP" "$core/dpf/distrho" "$core/include" \
		\( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name 'plugin.json' -o -name '*.mk' -o -name Makefile \) \
		-newer "$bin" -print -quit 2>/dev/null || true)"
	if [ -n "$newer" ]; then
		echo "Sources changed since the last build: building PatchCore first..."
		make -C "$core" -j"$(nproc)" jack
	fi
}

build() {
	echo "Fetching sources (submodules)..."
	git -C "$core" submodule update --init --recursive
	echo "Building PatchCore, the first build takes a while..."
	make -C "$core" -j"$(nproc)" jack
}

install_desktop() {
	local apps="$HOME/.local/share/applications"
	mkdir -p "$apps"
	cat > "$apps/patchcore.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=PatchCore
Comment=JXMP Audio modular rack
Exec="$core/run-patchcore.sh" %f
Icon=audio-card
Terminal=false
Categories=AudioVideo;Audio;Music;
MimeType=application/x-vcv-rack-patch;
EOF
	echo "Added PatchCore to the application menu ($apps/patchcore.desktop)."
	# A "patchcore" command, so it starts from any directory: patchcore demo
	mkdir -p "$HOME/.local/bin"
	ln -sf "$core/run-patchcore.sh" "$HOME/.local/bin/patchcore"
	echo "Added the command: $HOME/.local/bin/patchcore (e.g. patchcore demo)"
}

uninstall_desktop() {
	rm -f "$HOME/.local/share/applications/patchcore.desktop" "$HOME/.local/bin/patchcore"
	echo "Removed the menu entry and the patchcore command (the PatchCore folder stays)."
}

# --install, --uninstall and --help run where they were started (on the host: the host's home)
case "${1:-}" in
	--install)
		install_desktop
		exit 0
		;;
	--uninstall)
		uninstall_desktop
		exit 0
		;;
	-h|--help)
		sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
esac

# PatchCore is built in the Arch distrobox "CLAUDE" (newer glibc than the host). Started on the host,
# the script runs itself again inside that container.
container="${PATCHCORE_CONTAINER:-CLAUDE}"
if [ -z "${CONTAINER_ID:-}" ] && [ ! -e /run/.containerenv ] && command -v distrobox > /dev/null; then
	exec distrobox enter "$container" -- "$core/run-patchcore.sh" "$@"
fi

patch=""
case "${1:-}" in
	--build)
		build
		exit 0
		;;
	--update)
		echo "Fetching the newest PatchCore from GitHub..."
		git -C "$core" pull --ff-only
		build
		exit 0
		;;
	empty|--empty)
		;;
	demo|--demo)
		patch="$core/patches/jxmp-ambient.vcv"
		;;
	showcase|--showcase)
		patch="$core/patches/jxmp-showcase.vcv"
		;;
	"")
		patch="$core/patches/jxmp-workbench.vcv"
		;;
	-*)
		echo "Unknown option: $1 (try --help)" >&2
		exit 1
		;;
	*)
		patch="$(realpath -e "$1")" || { echo "Patch not found: $1" >&2; exit 1; }
		;;
esac

if [ -x "$bin" ]; then
	rebuild_if_needed
else
	build
fi

cd "$core"
if [ -n "$patch" ]; then
	PATCHCORE_DEV_PATCH="$patch" exec "$bin"
fi
exec "$bin"
