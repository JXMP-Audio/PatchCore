#!/usr/bin/env bash
# PatchCore launcher.
#
#   ./run-patchcore.sh                 start PatchCore (empty rack)
#   ./run-patchcore.sh demo            the JXMP ambient patch (also --demo)
#   ./run-patchcore.sh showcase        the JXMP showcase voice (also --showcase)
#   ./run-patchcore.sh my-patch.vcv    open your own patch
#   ./run-patchcore.sh --build         fetch the sources and build PatchCore first (also done
#                                      automatically when it has not been built yet)
#   ./run-patchcore.sh --install       add PatchCore to the application menu
#
# The standalone is a JACK client. Under PipeWire it does not connect itself to the speakers,
# so this script links its first two audio outputs to the default output. Started on the host,
# it runs inside the distrobox the app is built in (PATCHCORE_CONTAINER, default CLAUDE).
set -euo pipefail

core="$(cd "$(dirname "$0")" && pwd)"
bin="$core/bin/Cardinal"

# PatchCore is built in the Arch distrobox "CLAUDE" (newer glibc than the host). Started on the host,
# the script runs itself again inside that container.
container="${PATCHCORE_CONTAINER:-CLAUDE}"
if [ -z "${CONTAINER_ID:-}" ] && [ ! -e /run/.containerenv ] && command -v distrobox > /dev/null; then
	exec distrobox enter "$container" -- "$core/run-patchcore.sh" "$@"
fi

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
}

patch=""
case "${1:-}" in
	--build)
		build
		exit 0
		;;
	--install)
		install_desktop
		exit 0
		;;
	-h|--help)
		sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	demo|--demo)
		patch="$core/patches/jxmp-ambient.vcv"
		;;
	showcase|--showcase)
		patch="$core/patches/jxmp-showcase.vcv"
		;;
	"")
		;;
	-*)
		echo "Unknown option: $1 (try --help)" >&2
		exit 1
		;;
	*)
		patch="$(realpath -e "$1")" || { echo "Patch not found: $1" >&2; exit 1; }
		;;
esac

[ -x "$bin" ] || build

cd "$core"
if [ -n "$patch" ]; then
	PATCHCORE_DEV_PATCH="$patch" "$bin" &
else
	"$bin" &
fi
pid=$!

# Connect the audio to the default output (PipeWire / PulseAudio)
if command -v pw-link > /dev/null && command -v pactl > /dev/null; then
	sink="$(pactl get-default-sink 2>/dev/null || true)"
	for _ in $(seq 50); do
		pw-link -o 2>/dev/null | grep -q '^Cardinal:audio_out_1$' && break
		sleep 0.2
	done
	if [ -n "$sink" ]; then
		mapfile -t ports < <(pw-link -i | grep "^$sink:playback_" | head -2)
		if [ "${#ports[@]}" -ge 1 ]; then
			pw-link Cardinal:audio_out_1 "${ports[0]}" 2>/dev/null || true
			pw-link Cardinal:audio_out_2 "${ports[1]:-${ports[0]}}" 2>/dev/null || true
			echo "Audio: PatchCore → $sink"
		fi
	fi
else
	echo "pw-link/pactl not found: connect PatchCore's outputs yourself (e.g. with qpwgraph)."
fi
wait $pid
