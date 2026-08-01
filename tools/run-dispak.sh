#!/bin/sh
set -eu

dispak=${DISPAK:-dispak}
source_besm6=${BESM6_DISK_DIR:-"${HOME}/.besm6"}
state_home=${POPLAN_DISPAK_HOME:-"${TMPDIR:-/tmp}/re-poplan-dispak-home"}
state_besm6="${state_home}/.besm6"
source_volume="${source_besm6}/2148"
state_volume="${state_besm6}/2148"

mkdir -p "$state_besm6"

if [ ! -e "$source_volume" ]; then
    echo "error: BESM-6 volume 2148 not found: $source_volume" >&2
    exit 1
fi

if [ ! -e "$state_volume" ] && [ ! -L "$state_volume" ]; then
    ln -s "$source_volume" "$state_volume"
fi

HOME=$state_home
export HOME
exec "$dispak" "$@"
