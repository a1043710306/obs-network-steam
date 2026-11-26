# rmtpserver
初衷 解决抖音无法使用obs录制的问题

`rmtpserver` 是一个 OBS Studio 插件。它只提供 RTMP 服务端，游戏机上的 OBS 编码一次 H.264/H.265 和 AAC，副机通过 RTMP 拉流。服务端使用随插件提供的 MediaMTX 转发编码后的数据，不在转发阶段重新编码。

## 安装

1. 完全退出 OBS。
2. 解压安装包，完全退出 OBS 后运行 `install.cmd`。脚本会先移除旧的 `rmtpserver.dll`，再复制新版文件到 `C:\ProgramData\obs-studio\plugins\rmtpserver`。如果提示访问被拒绝，请右键 `install.cmd` 选择「以管理员身份运行」。
3. 重新打开 OBS，进入「工具 → rmtpserver」。

便携版 OBS 可以运行：

```powershell
.\install.cmd -PortablePath "D:\OBS Portable"
```

## 使用

1. 在游戏机 OBS 的场景中添加「游戏捕获」，并在「高级音频属性」中确认游戏声音进入音轨 1。
2. 打开「工具 → rmtpserver」。选择监听端口，然后点击「启动服务」。插件优先复用 OBS 当前直播输出的 H.264/H.265 + AAC 编码器和配置，没有直播编码器时回退复用 OBS 当前录制输出，不会再次编码。需要调整码率、B 帧或编码器时，请到 OBS 的直播或录制输出设置中修改。优先选择 NVENC、QuickSync 或 AMD 等硬件编码器。

如果窗口提示“没有已初始化的视频/音频输出”，先在 OBS 中启动一次录制或直播，让 OBS 初始化对应输出，再停止它并启动 rmtpserver。插件只能复用已经存在的 OBS 编码器，不能从未初始化的录制设置中读取并替代创建编码器。

录制或直播输出的视频编码可以是 H.264 或 H.265，音频必须是 AAC。H.265 需要 OBS、MediaMTX 和副机播放器都支持 Enhanced RTMP。Opus、PCM、FLAC 等音频编码不能直接封装到普通 RTMP 播放流中。若副机兼容性优先，使用 H.264。
3. 在副机 OBS 添加「媒体源」，取消「本地文件」，把插件显示的「副机拉流地址」填入输入。例如 `rtmp://192.168.1.10:1935/live/game`。输入格式填 `flv`。
4. 副机确认画面和声音后，用副机 OBS 推送到直播平台。副机可以断开后重新拉流。
5. 结束时回到游戏机的「工具 → rmtpserver」，点击「停止服务」。

插件默认监听 TCP 1935。请在游戏机防火墙允许副机访问这个端口。若端口被占用，在窗口中换一个端口，副机地址会同步更新。

RTMP 服务只转发 OBS 已生成的 H.264/H.265 + AAC，不会二次编码。MediaMTX 使用独立进程和内部并发处理多个拉流连接。远端拉流断开后只释放对应连接，服务端和发布端继续运行。游戏机 OBS 或发布端停止时，插件会停止 MediaMTX、释放输出和编码器引用。提高 OBS 码率可以提高画质和占用更多网络带宽，实际值应低于局域网稳定可用带宽。

## 从源码构建

需要 MSVC x64、Windows SDK、CMake 3.22 以上、Ninja、与目标 OBS 版本配套的 OBS 源码和 Qt SDK。CMake 会获取并校验 MediaMTX v1.21.4 Windows x64 发布包，也可以用 `-DMEDIAMTX_DIR` 指向已解压的 MediaMTX 目录。

```bat
git clone --depth 1 --branch 33.0.3 https://github.com/obsproject/obs-studio.git .build/obs-source
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DOBS_SOURCE_DIR=.build/obs-source" "-DCMAKE_PREFIX_PATH=.build/obs-qt" "-DOBS_INSTALL_DIR=C:/Program Files/obs-studio"
cmake --build build
cmake --install build --prefix dist
```
