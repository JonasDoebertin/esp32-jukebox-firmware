#!/usr/bin/env bash

cd "$1" || exit
rm -rf ./.Spotlight-V100 ||:
rm -rf ./.fseventsd
rm -rf ./.Trashes
dot_clean -mn "."