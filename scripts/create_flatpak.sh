#!/usr/bin/env bash
# Please set this up to be run from the scripts folder #
cd ..
mkdir flatpakBuild
cd flatpakBuild
mkdir bin
mkdir share 
mkdir share/applications
mkdir share/applications/org.anie.AnimeEffects
mkdir share/icons
mkdir share/icons/hicolor
mkdir share/icons/hicolor/scalable
mkdir share/icons/hicolor/256x256
cd ..
cp dist/AnimeEffects.desktop flatpakBuild/share/applications/org.anie.AnimeEffects
cp dist/AnimeEffects.svg flatpakBuild/share/icons/hicolor/scalable
cp dist/AnimeEffects.png flatpakBuild/share/icons/hicolor/256x256
cp -r data flatpakBuild
cp src/AnimeEffects flatpakBuild
cp scripts/flatpak.yaml flatpakBuild
cd flatpakBuild
flatpak-builder org.anie.AnimeEffects flatpak.yaml --force-clean
