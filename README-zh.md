[English](./README.md) · [日本語](./README-ja.md) · 简体中文 · [正體中文](./README-zh-t.md) · [Español](./README-es.md)

# AnimeEffects

## 下载

|                                                                                                                                                              稳定版本 (v1.6)                                                                                                                                                              |                                                                                                                                                                                     开发版本                                                                                                                                                                                     |                                           源代码                                           |
| :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :----------------------------------------------------------------------------------------: |
| [Windows](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-Installer-Windows.exe) - [MacOS](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-MacOS.zip) - [Linux](https://github.com/AnimeEffectsDevs/AnimeEffects/releases/download/v1.6/AnimeEffects-Linux.zip) | [Windows](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build-windows.yaml/master/AnimeEffects-Windows-x64.zip) - [MacOS](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build_mac_intel.yaml/master/AnimeEffects-MacOS.zip) - [Linux](https://nightly.link/AnimeEffectsDevs/AnimeEffects/workflows/build_linux.yaml/master/AnimeEffects-Linux.zip) | [下载 ZIP](https://github.com/AnimeEffectsDevs/AnimeEffects/archive/refs/heads/master.zip) |

## 描述

一款无需精心规划的 2D 动画工具，其通过基于多边形网格变形的各种功能来简化动画制作。<br>
最早由 hidefuku 开发，现在其社区负责开发及维护。

- 官方网站：
  - <https://animeeffectsdevs.github.io/>
- 官方社群：
  - Discord: [AnimeEffects](https://discord.gg/sKp8Srm)（感谢 [José Moreno](https://github.com/Jose-Moreno) 之支持）
  - X (Twitter): [@anime_effect](https://x.com/anime_effects) （感谢 [p_yukusai](https://github.com/p-yukusai) 之支持）
  - 红迪： [r/AnimeEffects](https://www.reddit.com/r/AnimeEffects/) （感谢 [visterical](https://www.tumblr.com/visterical) 之支持）

- 您还可以通过以下方式支持我们：<br><br>
  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/V7V04YLC3) &nbsp;&nbsp; <a href="https://yukusai.itch.io/animeeffects" target="_blank"> <img src="https://static.itch.io/images/badge-color.svg" alt="itch.io" style="width:100px" /> </a>

注意：目前可能会有不兼容的更改，如果发生此类更改，我们将在受影响的版本中予以说明。<br>
**_如果你有任何问题或希望建议新的功能，随时通过社群联系我们！_**

## 发布

当新版本被发布时， AnimeEffects 将通过以下渠道来通知您可用的稳定版本。

- 我们可用的稳定版本[在这里](https://github.com/AnimeEffectsDevs/AnimeEffects/releases)。
- 我们可用的不稳定版本[在这里](https://github.com/p-yukusai/AnimeEffects/releases)。
- 我们可用的最新版本[在这里](https://github.com/AnimeEffectsDevs/AnimeEffects/actions)。

## 设备要求

- Windows/Linux/Mac
  - 具体查看下面的「适配系统」
- 处理器： 64 位 CPU
- RAM: 4 GB
- 显卡：支持 OpenGL 4.0 或更高版本的 GPU/iGPU
- [FFmpeg](https://ffmpeg.org/download.html) （视频导出时需要用到。您可以将其注册到系统的环境变量上，或者复制到 `"/tools"` 文件夹——如果文件夹不存在，请在可执行文件的同级目录下创建此文件夹）

## 适配系统

#### 这是我们通过编译和测试的版本，应用可能会在更旧的系统上运行，但我们并不鼓励这样做。

- Windows 10（此版本即将终止支持，建议尽快升级或更换操作系统）或更高版本
- Ubuntu LTS 或类似发行版
- MacOS Monterey 或更高版本

## 开发要求

- Qt 6.6.X
- 需要 Qt 的 `LinguistTools` CMake 组件（`aqtinstall` 已包含；若使用系统 Qt，请安装发行版的 Qt `-dev` 包）
- Vulkan Headers
- CMake 3.16 或更高版本
- MSVC/GCC/CLang （64 位）

## Linux (Debian)

### 编译和构建 AppImage

- 大部分依赖项系统已自带，请对照检查你的软件包：

```
sudo apt update && sudo apt upgrade -y
sudo apt install -y software-properties-common g++ make cmake ninja-build wget rsync build-essential libglib2.0-0
sudo apt install -y libgl1-mesa-dev file libvulkan-dev openssl python3 python3-pip libxcb-cursor0 libxrandr2 wget
pip install -U pip
pip install aqtinstall
aqt install-qt linux desktop 6.6.2 gcc_64 -m qtimageformats qtmultimedia qt5compat

> 若使用系统 Qt（而非 `aqt`），还需安装 `qt6-tools-dev`（它提供 `LinguistTools` CMake 组件）。
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

### 编译和构建文件夹

- 安装步骤假设你已通过安装程序完成所有必备组件的安装，并已添加至系统路径

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

## MacOS

### 编译和构建 `.app`

- 操作步骤假设你的系统已安装 xcode、brew、wget、python 3 和 pip

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
