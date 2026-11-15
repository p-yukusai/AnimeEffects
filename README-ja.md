[English](./README.md) · 日本語 · [简体中文](./README-zh.md) · [正體中文](./README-zh-t.md) · [Español](./README-es.md)

# AnimeEffects

## ダウンロード

|                                                                                                                                                               安定版(v1.6)                                                                                                                                                                |                                                                                                                                                                                    ナイトリー                                                                                                                                                                                    |                                         ソース コード                                          |
| :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :--------------------------------------------------------------------------------------------: |
| [Windows](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-Installer-Windows.exe) - [MacOS](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-MacOS.zip) - [Linux](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-Linux.zip) | [Windows](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build-windows.yaml/master/AnimeEffects-Windows-x64.zip) - [MacOS](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build_mac_intel.yaml/master/AnimeEffects-MacOS.zip) - [Linux](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build_linux.yaml/master/AnimeEffects-Linux.zip) | [Download ZIP](https://github.com/AnimeEffectsDevs/AnimeEffects/archive/refs/heads/master.zip) |

## 説明

慎重な思考計画を必要としない 2D アニメーションツールであり、ポリゴンメッシュの変形に基づくさまざまな機能を提供することでアニメーションを簡素化します。<br>
もともとは秀福によって開発され、コミュニティによって開発され、維持されています。

- 公式サイト:<br>

  - <https://animeeffectsdevs.github.io/>

- 公式社会：<br>

  - Discord: <a href='https://discord.gg/sKp8Srm'>AnimeEffects</a> (礼儀 [José Moreno](https://github.com/Jose-Moreno))<br>
  - X (Twitter): <a href='https://x.com/anime_effects'>@anime_effects</a> (礼儀 [p_yukusai](https://github.com/p-yukusai))<br>
  - Reddit: <a href='https://www.reddit.com/r/AnimeEffects/'>r/AnimeEffects</a> (礼儀 [visterical](https://www.tumblr.com/visterical))<br>

- You can support us through:<br><br>
  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/V7V04YLC3) &nbsp;&nbsp; <a href="https://yukusai.itch.io/animeeffects" target="_blank"> <img src="https://static.itch.io/images/badge-color.svg" alt="itch.io" style="width:100px" /> </a>

注意：現時点では互換性のない変更がなされている可能性があります。これらは発生した場合、影響を受けるリリースで既知になります。<br>
**_何か問題があったり、新しい機能を提案したい場合は、お気軽に私たちの社会に連絡してください!_**

## リリース

AnimeEffects は、安定版リリースが出てきたらすぐに通知します。

- 私たちの安定したビルドは [here](https://github.com/AnimeEffectsDevs/AnimeEffects/releases) 利用可能です。<br>
- 私たちの不安定なビルドは [here](https://github.com/p-yukusai/AnimeEffects/releases) 利用可能です。<br>
- 私たちのナイトリービルドは [here](https://github.com/AnimeEffectsDevs/AnimeEffects/actions) 利用できます。

## 開発要件

- Windows/Linux/Mac
  - 以下の互換性のあるバージョンを参照してください
- プロセッサ: 64 ビット CPU
- RAM: 4 GB
- グラフィックカード：OpenGL 4.0 以降をサポートする GPU/iGPU
- [FFmpeg](https://ffmpeg.org/download.html) (ビデオエクスポートの場合は必要ありません。 パス上に配置するか、「/tools」フォルダにコピーすることができます。実行ファイルが存在しない場合は、このフォルダを作成します。

## OS ターゲット

#### これは私たちがソフトウェアをコンパイルしてテストしているものです。古いバージョンでは動作するかもしれませんが、これは推奨されません

- Windows 10 以降。
- Ubuntu LTS 以降。
- MacOS Big Sur 以降

## 開発要件

- Qt 6.6.X
- Qt の `LinguistTools` CMake コンポーネント（`aqtinstall` に含まれます。システム Qt を使用する場合は、ディストリビューションの Qt `-dev` パッケージをインストールしてください）
- Vulkan Headers
- CMake 3.16 以降
- MSVC/GCC/CLang (64 ビット)

## Linux (Debian)

### コンパイルと AppImage の作成

- これらの依存関係のほとんどは、あなたの気晴らしに来るので不要ですので、独自のパッケージをチェックしてください。

```
sudo apt update && sudo apt upgrade -y
sudo apt install -y software-properties-common g++ make cmake ninja-build wget rsync build-essential libglib2.0-0
sudo apt install -y libgl1-mesa-dev file libvulkan-dev openssl python3 python3-pip libxcb-cursor0 libxrandr2 wget
pip install -U pip
pip install aqtinstall
aqt install-qt linux desktop 6.6.2 gcc_64 -m qtimageformats qtmultimedia qt5compat

> システム Qt（`aqt` ではなく）を使用する場合は、`qt6-tools-dev` もインストールしてください（`LinguistTools` CMake コンポーネントが含まれます）。
git clone https://github.com/AnimeEffectsDevs/AnimeEffects
cd AnimeEffects
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Release
cd build/src/gui/Release
mkdir -p appdir
cp AnimeEffects appdir
cp -R ../../../../dist appdir/dist
cp ../../../../dist/AnimeEffects.png appdir
find appdir/
export APPIMAGE_EXTRACT_AND_RUN=1
wget -c -nv "https://github.com/p-yukusai/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt-continuous-x86_64.AppImage
./linuxdeployqt-continuous-x86_64.AppImage appdir/dist/AnimeEffects.desktop -extra-plugins=imageformats,multimedia,core5compat -appimage -verbose=2
chmod a+x AnimeEffects-x86_64.AppImage
```

## Windows

### コンパイルとフォルダの作成

- インストール手順は、インストーラーを通じてすべての要件をインストールし、パスにそれらを持っていることを前提とします

```powershell
git clone https://github.com/AnimeEffectsDevs/AnimeEffects
cd AnimeEffects
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Release
cd build/src/gui/Release
mkdir .\AnimeEffects-Windows-x64
windeployqt --dir .\AnimeEffects-Windows-x64 .\AnimeEffects.exe
Copy-Item ".\AnimeEffects.exe" ".\AnimeEffects-Windows-x64\"
```

## Linux

### コンパイルと .app の作成

- これらのステップは、xcode、brew、wget、python 3、pip がシステムにインストールされていると仮定します

```bash
brew install cmake ninja vulkan-headers
pip install -U pip
pip install aqtinstall
aqt install-qt mac desktop 6.6.2 clang_64 -m qtimageformats qtmultimedia qt5compat
git clone https://github.com/AnimeEffectsDevs/AnimeEffects
cd AnimeEffects
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Release
cd build/src/gui/Release
mkdir -p appdir/usr/lib
cp -R AnimeEffects.app appdir/AnimeEffects.app
cp -R ../../../../dist appdir/dist
find appdir/
cd appdir
macdeployqt AnimeEffects.app
wget https://raw.githubusercontent.com/OpenZWave/ozw-admin/master/scripts/macdeployqtfix.py && chmod a+x macdeployqtfix.py
./macdeployqtfix.py AnimeEffects.app /usr/local/Cellar/qt/*/
```
