#pragma once

#include <obs.h>

#include <atomic>
#include <string>

struct RelaySettings {
    std::string server;
    std::string key = "game";
};

class Relay {
public:
    Relay() = default;
    Relay(const Relay &) = delete;
    Relay &operator=(const Relay &) = delete;
    ~Relay();

    enum { Connecting = 1, Sending, Stopping };

    bool start(const RelaySettings &settings);
    void stop();
    void reset();
    int state() const { return state_.load(); }
    bool reconnecting() const;

private:
    static void on_start(void *, calldata_t *);
    static void on_stop(void *, calldata_t *);

    obs_output_t *output_ = nullptr;
    obs_service_t *service_ = nullptr;
    std::atomic<int> state_{OBS_OUTPUT_SUCCESS};
};
