#include "relay_session.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <windows.h>

#include <utility>

namespace {

constexpr auto server_config = R"(logLevel: info
logDestinations: [stdout]
readTimeout: 3s
writeTimeout: 3s
writeQueueSize: 64
rtsp: false
hls: false
webrtc: false
srt: false
moq: false
rtmp: true
rtmpAddress: "0.0.0.0:%1"
authInternalUsers:
  - user: any
    ips: ["127.0.0.1"]
    permissions:
      - action: publish
        path: live/game
  - user: any
    permissions:
      - action: read
        path: live/game
paths:
  live/game:
    overridePublisher: false
)";

} // namespace

RelaySession::RelaySession()
{
    server_.setProcessChannelMode(QProcess::MergedChannels);
    server_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    connect(&server_, &QProcess::started, this, [this] {
        // Tie the child to OBS so an unexpected OBS exit also closes the listener.
        const auto process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                         static_cast<DWORD>(server_.processId()));
        const auto job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (process && job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            && AssignProcessToJobObject(job, process)) {
            server_job_ = job;
        } else {
            blog(LOG_WARNING, "[rmtpserver] Could not attach the RTMP service to the OBS process lifetime");
            if (job)
                CloseHandle(job);
        }
        if (process)
            CloseHandle(process);
    });
    connect(&server_, &QProcess::readyReadStandardOutput, this, &RelaySession::read_server_output);
    connect(&server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            blog(LOG_WARNING, "[rmtpserver] Could not start RTMP service: %s",
                 server_.errorString().toUtf8().constData());
            error_ = ServerFailed;
            stop_server();
        }
    });
    connect(&server_, &QProcess::finished, this, [this] {
        read_server_output();
        if (using_server_) {
            if (error_ == OBS_OUTPUT_SUCCESS)
                error_ = ServerFailed;
            stop_server();
            relay_.reset();
        }
    });

    startup_deadline_.setSingleShot(true);
    connect(&startup_deadline_, &QTimer::timeout, this, [this] {
        blog(LOG_WARNING, "[rmtpserver] Timed out waiting for the RTMP listener");
        error_ = ServerFailed;
        stop_server();
    });
    connect(&monitor_, &QTimer::timeout, this, &RelaySession::monitor_output);
}

RelaySession::~RelaySession()
{
    reset();
}

int RelaySession::state() const
{
    if (error_ != OBS_OUTPUT_SUCCESS)
        return error_;
    if (starting_)
        return StartingServer;
    const int output_state = relay_.state();
    if (using_server_ && output_state == Relay::Sending && !publishing_)
        return PreparingStream;
    return output_state;
}

void RelaySession::start(RelaySettings settings, int listen_port)
{
    if (state() > 0)
        return;
    reset();
    local_output_ = std::move(settings);
    local_output_.server = "rtmp://127.0.0.1:" + std::to_string(listen_port) + "/live";
    start_server(listen_port);
}

void RelaySession::start_server(int port)
{
    char *file = obs_module_file("mediamtx/mediamtx.exe");
    const QString executable = QString::fromUtf8(file);
    bfree(file);
    if (!QFileInfo(executable).isFile()) {
        error_ = ServerMissing;
        return;
    }

    config_ = std::make_unique<QTemporaryFile>(QDir::temp().filePath(QStringLiteral("obs-rtmp-XXXXXX.yml")));
    const auto data = QString::fromLatin1(server_config).arg(port).toUtf8();
    if (!config_->open() || config_->write(data) != data.size() || !config_->flush()) {
        blog(LOG_WARNING, "[rmtpserver] Could not write RTMP configuration: %s",
             config_->errorString().toUtf8().constData());
        config_.reset();
        error_ = ServerFailed;
        return;
    }
    config_->close();

    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : environment.keys()) {
        if (key.startsWith(QStringLiteral("MTX_"), Qt::CaseInsensitive))
            environment.remove(key);
    }
    server_.setProcessEnvironment(environment);
    starting_ = true;
    using_server_ = true;
    startup_deadline_.start(5000);
    server_.start(executable, {config_->fileName()});
}

void RelaySession::read_server_output()
{
    while (server_.canReadLine()) {
        const auto line = server_.readLine().trimmed();
        blog(LOG_INFO, "[rmtpserver RTMP] %s", line.constData());
        if (!using_server_)
            continue;
        if (line.contains("is publishing to path 'live/game'")) {
            publishing_ = true;
        } else if (!starting_) {
            continue;
        } else if (line.contains("listen tcp") && line.contains(": bind:")) {
            error_ = PortUnavailable;
        } else if (line.contains("[RTMP] started with listener on ") && server_.state() == QProcess::Running) {
            startup_deadline_.stop();
            starting_ = false;
            if (!relay_.start(local_output_)) {
                error_ = relay_.state() == OBS_OUTPUT_INVALID_STREAM ? IncompatibleEncoder : NoEncoder;
                stop_server();
                return;
            }
            monitor_.start(250);
        }
    }
}

void RelaySession::monitor_output()
{
    if (relay_.state() <= 0)
        stop_server();
}

void RelaySession::stop()
{
    relay_.stop();
}

void RelaySession::reset()
{
    relay_.reset();
    stop_server();
    error_ = OBS_OUTPUT_SUCCESS;
}

void RelaySession::stop_server()
{
    using_server_ = false;
    starting_ = false;
    publishing_ = false;
    startup_deadline_.stop();
    monitor_.stop();
    if (server_job_) {
        CloseHandle(server_job_);
        server_job_ = nullptr;
    }
    if (server_.state() != QProcess::NotRunning) {
        server_.kill();
        server_.waitForFinished(1000);
    }
    config_.reset();
}
