# rmtpserver

游戏机 OBS 通过 `rmtpserver` 提供 RTMP 拉流地址，副机 OBS 接收画面和声音后推送到抖音。安装包适用于 Windows x64 和 OBS Studio 33.0.3。

## 安装

1. 完全退出 OBS。
2. 解压整个安装包，运行 `install.cmd` 安装或更新插件。
3. 重新打开 OBS，进入「工具 → rmtpserver」。

如果提示访问被拒绝，右键 `install.cmd` 并选择「以管理员身份运行」。便携版 OBS 在解压目录运行下面的命令，将路径改为 OBS 根目录。

```powershell
.\install.cmd -PortablePath "D:\OBS Portable"
```

## 游戏机配置

1. 在 OBS 场景中添加「游戏捕获」。
2. 在「高级音频属性」中，将游戏声音勾选到输出使用的音轨，通常为音轨 1。
3. 在「设置 → 输出」中选 H.264 或 H.265（HEVC）视频编码器和 AAC 音频编码器。副机使用 H.265 拉流时需要支持 Enhanced RTMP。
4. 启动 OBS 录制或直播，再打开「工具 → rmtpserver」，点击「启动服务」。默认监听 TCP `1935`。

插件优先复用直播输出的编码器，直播输出尚无编码器时尝试复用录制输出。若窗口提示编码器未初始化，确认录制或直播已经开始并重试。结束时在插件窗口点击「停止服务」。

## 副机配置

1. 将两台电脑接入同一局域网，在游戏机防火墙放行副机访问插件监听的 TCP 端口。
2. 在副机 OBS 添加「媒体源」，取消勾选「本地文件」，把插件显示的拉流地址填入「输入」，例如 `rtmp://192.168.1.10:1935/live/game`。输入格式填 `flv`。
3. 确认画面和声音正常后，在副机 OBS 配置抖音推流地址和密钥，再点击「开始直播」。

拉流失败时，确认插件显示服务已启动，并检查拉流地址是否使用了副机能访问的游戏机局域网 IP 和实际监听端口。

## 60 FPS 与低延迟

- 两台电脑的 OBS 都在「设置 → 视频」选择 60 FPS，应用设置后重新启动各自的输出。抖音端的播放帧率取决于平台的处理和播放档位。
- 优先使用有线网和硬件编码。需要降低编码延迟时，可在编码器选项中将 B 帧设为 0，关闭 Look-ahead，将关键帧间隔设为 1～2 秒。
- 关闭副机 OBS 的串流延迟。若副机预览已明显落后，可降低媒体源的网络缓冲，并检查画面是否流畅。若副机预览正常但抖音播放仍落后，延迟还可能来自平台转码和播放缓冲。

## 从源码构建

需要 MSVC x64、Windows SDK、CMake 3.22 或更高版本、Ninja，以及与目标 OBS 版本配套的 OBS 源码和 Qt SDK。OBS 33.0.3 使用 Qt 6.11.1，将 SDK 解压到 `.build/obs-qt`，使其中包含 `lib/cmake/Qt6/Qt6Config.cmake`。 [OBS 官方 Qt x64 SDK](https://github.com/obsproject/obs-deps/releases/download/2026-10-23/windows-deps-qt6-2026-10-23-x64.zip)

在 Visual Studio 的「x64 Native Tools Command Prompt」中，从本项目根目录运行以下命令。如 OBS 安装在其他目录，调整 `OBS_INSTALL_DIR`。

```bat
git clone --depth 1 --branch 33.0.3 https://github.com/obsproject/obs-studio.git .build/obs-source
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DOBS_SOURCE_DIR=.build/obs-source" "-DCMAKE_PREFIX_PATH=.build/obs-qt" "-DOBS_INSTALL_DIR=C:/Program Files/obs-studio"
cmake --build build
cmake --install build --prefix dist
```

构建产物和安装脚本位于 `dist`。CMake 默认获取并校验 MediaMTX v1.21.4。使用已解压的 Windows x64 发布包时，在配置命令中追加 `"-DMEDIAMTX_DIR=C:/path/to/mediamtx"`。
