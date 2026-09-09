#!/bin/sh
set -eu

dispak=${DISPAK:-dispak}
besmtool=${BESMTOOL:-besmtool}
source_besm6=${BESM6_DISK_DIR:-"${HOME}/.besm6"}
state_home=${POPLAN_DISPAK_HOME:-"${TMPDIR:-/tmp}/re-poplan-dispak-home"}
state_besm6="${state_home}/.besm6"
source_volume="${source_besm6}/2148"
state_volume="${state_besm6}/2148"
source_poplib=`pwd`/poplib.bin
state_poplib="${state_besm6}/2157"

mkdir -p "$state_besm6"

if [ ! -e "$source_volume" ]; then
    echo "error: BESM-6 volume 2148 not found: $source_volume" >&2
    exit 1
fi

if [ ! -e "$state_volume" ] && [ ! -L "$state_volume" ]; then
    ln -s "$source_volume" "$state_volume"
fi

if [ -e "$source_poplib" ]; then
    if [ -e "$state_poplib" ] || [ -L "$state_poplib" ]; then
        unlink "$state_poplib"
    fi
    : > "$state_poplib"
    HOME=$state_home "$besmtool" write 2157 --start=0 \
        --from-file="$source_poplib" >/dev/null
elif [ ! -e "$state_poplib" ] && [ ! -L "$state_poplib" ]; then
    echo "warning: POPLIB image $source_poplib not found, using /dev/zero" >&2
    ln -s /dev/zero "$state_poplib"
fi

HOME=$state_home
export HOME
exec "$dispak" "$@"
