#include "relay.hpp"

#include <obs-frontend-api.h>

#include <cstring>

Relay::~Relay()
{
    reset();
}

bool Relay::start(const RelaySettings &settings)
{
    if (state() > 0)
        return false;
    reset();

    const std::string service_json = "{\"server\":\"" + settings.server +
        "\",\"key\":\"" + settings.key + "\"}";
    obs_data_t *service_settings = obs_data_create_from_json(service_json.c_str());
    if (!service_settings) {
        blog(LOG_WARNING, "[rmtpserver] Could not allocate RTMP service settings");
        state_ = OBS_OUTPUT_ERROR;
        return false;
    }
    service_ = obs_service_create("rtmp_custom", "rmtpserver service", service_settings, nullptr);
    obs_data_release(service_settings);

    output_ = obs_output_create("rtmp_output", "rmtpserver", nullptr, nullptr);
    obs_output_t *source_output = obs_frontend_get_streaming_output();
    if (!source_output || !obs_output_get_video_encoder(source_output) ||
        !obs_output_get_audio_encoder(source_output, 0)) {
        if (source_output)
            obs_output_release(source_output);
        source_output = obs_frontend_get_recording_output();
        blog(LOG_INFO, "[rmtpserver] Reusing OBS recording output encoders");
    } else {
        blog(LOG_INFO, "[rmtpserver] Reusing OBS streaming output encoders");
    }
    obs_encoder_t *video = source_output ? obs_output_get_video_encoder(source_output) : nullptr;
    obs_encoder_t *audio = source_output ? obs_output_get_audio_encoder(source_output, 0) : nullptr;

    if (!service_ || !output_ || !source_output || !video || !audio) {
        blog(LOG_WARNING, "[rmtpserver] OBS must initialize an H.264/H.265 video encoder and AAC audio encoder before starting");
        if (source_output)
            obs_output_release(source_output);
        reset();
        state_ = OBS_OUTPUT_INVALID_STREAM;
        return false;
    }

    const char *video_codec = obs_encoder_get_codec(video);
    const char *audio_codec = obs_encoder_get_codec(audio);
    const bool supported_video = video_codec &&
        (strcmp(video_codec, "h264") == 0 || strcmp(video_codec, "hevc") == 0);
    if (!supported_video || !audio_codec || strcmp(audio_codec, "aac") != 0) {
        blog(LOG_WARNING, "[rmtpserver] RTMP requires H.264/H.265 video and AAC audio, got %s/%s",
             video_codec ? video_codec : "unknown", audio_codec ? audio_codec : "unknown");
        obs_output_release(source_output);
        reset();
        state_ = OBS_OUTPUT_INVALID_STREAM;
        return false;
    }

    obs_output_set_video_encoder(output_, video);
    obs_output_set_audio_encoder(output_, audio, 0);
    obs_output_release(source_output);

    obs_output_set_service(output_, service_);
    obs_output_set_reconnect_settings(output_, 12, 2);
    auto *signals = obs_output_get_signal_handler(output_);
    signal_handler_connect(signals, "start", on_start, this);
    signal_handler_connect(signals, "stop", on_stop, this);

    state_ = Connecting;
    if (!obs_output_start(output_)) {
        reset();
        state_ = OBS_OUTPUT_ERROR;
        return false;
    }
    return true;
}

void Relay::stop()
{
    int expected = Sending;
    if (state_.compare_exchange_strong(expected, Stopping))
        obs_output_stop(output_);
}

void Relay::reset()
{
    if (output_) {
        auto *signals = obs_output_get_signal_handler(output_);
        signal_handler_disconnect(signals, "start", on_start, this);
        signal_handler_disconnect(signals, "stop", on_stop, this);
        if (obs_output_active(output_))
            obs_output_force_stop(output_);
        obs_output_release(output_);
        output_ = nullptr;
    }
    obs_service_release(service_);
    service_ = nullptr;
    state_ = OBS_OUTPUT_SUCCESS;
}

bool Relay::reconnecting() const
{
    return output_ && obs_output_reconnecting(output_);
}

void Relay::on_start(void *data, calldata_t *)
{
    auto &relay = *static_cast<Relay *>(data);
    int expected = Connecting;
    relay.state_.compare_exchange_strong(expected, Sending);
}

void Relay::on_stop(void *data, calldata_t *params)
{
    auto &relay = *static_cast<Relay *>(data);
    relay.state_ = static_cast<int>(calldata_int(params, "code"));
}
