#!/usr/bin/env bash
# Please set this up to be run from the scripts folder #
cd ..
flatpak-builder --repo=anie --force-clean org.anie.AnimeEffects scripts/flatpak.yaml 
flatpak --user remote-add --no-gpg-verify anie anie
flatpak build-bundle anie anie.flatpak org.anie.AnimeEffects
