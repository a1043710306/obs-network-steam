#pragma once

#include "relay.hpp"

#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>

class RelaySession final : public QObject {
public:
    enum {
        StartingServer = 4,
        PreparingStream,
        ServerFailed = -1000,
        PortUnavailable = -1001,
        ServerMissing = -1002,
        NoEncoder = -1003,
        IncompatibleEncoder = -1004
    };

    RelaySession();
    ~RelaySession() override;
    void start(RelaySettings settings, int listen_port);
    void stop();
    void reset();
    int state() const;
    bool reconnecting() const { return relay_.reconnecting(); }

private:
    void start_server(int port);
    void stop_server();
    void read_server_output();
    void monitor_output();

    Relay relay_;
    QProcess server_;
    QTimer startup_deadline_;
    QTimer monitor_;
    std::unique_ptr<QTemporaryFile> config_;
    RelaySettings local_output_;
    void *server_job_ = nullptr;
    bool starting_ = false;
    bool using_server_ = false;
    bool publishing_ = false;
    int error_ = OBS_OUTPUT_SUCCESS;
};
