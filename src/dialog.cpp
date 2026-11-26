#include "relay_session.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rmtpserver", "en-US")

namespace {

struct Preferences {
    int listen_port = 1935;
};

RelaySession *session = nullptr;
Preferences preferences;

QString localized(const char *key)
{
    return QString::fromUtf8(obs_module_text(key));
}

void load_preferences()
{
    char *path = obs_module_config_path("settings.json");
    if (!path)
        return;
    obs_data_t *data = os_file_exists(path) ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
    bfree(path);
    if (!data)
        return;

    obs_data_set_default_int(data, "listen_port", preferences.listen_port);
    const auto port = obs_data_get_int(data, "listen_port");
    if (port >= 1 && port <= 65535)
        preferences.listen_port = static_cast<int>(port);
    obs_data_release(data);
}

void save_preferences()
{
    char *directory = obs_module_config_path("");
    char *path = obs_module_config_path("settings.json");
    if (directory && path && os_mkdirs(directory) != MKDIR_ERROR) {
        obs_data_t *data = obs_data_create();
        if (data) {
            obs_data_set_int(data, "listen_port", preferences.listen_port);
            if (!obs_data_save_json_safe(data, path, "tmp", "bak"))
                blog(LOG_WARNING, "[rmtpserver] Could not save settings");
            obs_data_release(data);
        } else {
            blog(LOG_WARNING, "[rmtpserver] Could not allocate settings data");
        }
    }
    bfree(directory);
    bfree(path);
}

class RmtpServerDialog final : public QDialog {
public:
    explicit RmtpServerDialog(QWidget *parent);

private:
    void update_playback_urls();
    void update_status();
    void start_or_stop();

    Preferences draft_ = preferences;
    QFormLayout *form_ = new QFormLayout;
    QSpinBox *listen_port_ = new QSpinBox(this);
    QComboBox *playback_url_ = new QComboBox(this);
    QLabel *status_ = new QLabel(this);
    QPushButton *toggle_ = nullptr;
    QStringList local_addresses_;
};

RmtpServerDialog::RmtpServerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(localized("Title"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    listen_port_->setRange(1, 65535);
    listen_port_->setValue(draft_.listen_port);
    playback_url_->setEditable(true);
    playback_url_->lineEdit()->setReadOnly(true);

    for (const auto &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback() && !address.isLinkLocal())
            local_addresses_.append(address.toString());
    }
    local_addresses_.removeDuplicates();
    if (local_addresses_.isEmpty())
        local_addresses_.append(localized("GamePcIp"));

    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->addRow(localized("ListenPort"), listen_port_);
    form_->addRow(localized("PlaybackUrl"), playback_url_);

    auto *buttons = new QDialogButtonBox(this);
    toggle_ = buttons->addButton(localized("StartServer"), QDialogButtonBox::ActionRole);
    toggle_->setAutoDefault(false);
    auto *close = buttons->addButton(QDialogButtonBox::Close);
    close->setText(localized("Close"));
    close->setAutoDefault(false);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form_);
    auto *info = new QLabel(localized("EncoderInfo"), this);
    info->setWordWrap(true);
    layout->addWidget(info);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    connect(listen_port_, &QSpinBox::valueChanged, this, &RmtpServerDialog::update_playback_urls);
    connect(toggle_, &QPushButton::clicked, this, &RmtpServerDialog::start_or_stop);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &RmtpServerDialog::update_status);
    timer->start(400);

    update_playback_urls();
    update_status();
    resize(500, sizeHint().height());
}

void RmtpServerDialog::update_playback_urls()
{
    const int selected = playback_url_->currentIndex();
    playback_url_->clear();
    for (const auto &address : local_addresses_)
        playback_url_->addItem(QStringLiteral("rtmp://%1:%2/live/game").arg(address).arg(listen_port_->value()));
    if (selected >= 0 && selected < playback_url_->count())
        playback_url_->setCurrentIndex(selected);
}

void RmtpServerDialog::update_status()
{
    const int state = session->state();
    const bool idle = state <= 0;
    const char *status = "Idle";
    if (state == Relay::Sending)
        status = session->reconnecting() ? "Reconnecting" : "ServerReady";
    else if (state == RelaySession::StartingServer)
        status = "StartingServer";
    else if (state == RelaySession::PreparingStream)
        status = "PreparingStream";
    else if (state == Relay::Connecting)
        status = "Connecting";
    else if (state == Relay::Stopping)
        status = "Stopping";
    else if (state == RelaySession::PortUnavailable)
        status = "PortUnavailable";
    else if (state == RelaySession::ServerMissing)
        status = "ServerMissing";
    else if (state == RelaySession::NoEncoder)
        status = "NoEncoder";
    else if (state == RelaySession::IncompatibleEncoder)
        status = "IncompatibleEncoder";
    else if (state == RelaySession::ServerFailed)
        status = "ServerFailed";
    else if (state < 0)
        status = "Failed";

    status_->setText(localized(status));
    toggle_->setText(localized(idle ? "StartServer" : "StopServer"));
    toggle_->setEnabled(idle || state == Relay::Sending || state == RelaySession::PreparingStream);
    listen_port_->setEnabled(idle);
}

void RmtpServerDialog::start_or_stop()
{
    if (session->state() > 0) {
        session->stop();
        update_status();
        return;
    }

    draft_.listen_port = listen_port_->value();
    preferences = draft_;
    save_preferences();
    session->start({}, draft_.listen_port);
    update_status();
}

QPointer<RmtpServerDialog> dialog;

void show_dialog(void *)
{
    if (!dialog) {
        auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
        dialog = new RmtpServerDialog(parent);
    }
    if (dialog->isMinimized())
        dialog->showNormal();
    else
        dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void frontend_event(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        if (session)
            session->reset();
        if (dialog) {
            dialog->close();
            dialog->deleteLater();
            dialog = nullptr;
        }
        delete session;
        session = nullptr;
    } else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING && session) {
        session->reset();
    }
}

} // namespace

bool obs_module_load(void)
{
    load_preferences();
    session = new RelaySession;
    obs_frontend_add_tools_menu_item(obs_module_text("Title"), show_dialog, nullptr);
    obs_frontend_add_event_callback(frontend_event, nullptr);
    return true;
}

const char *obs_module_description(void)
{
    return "rmtpserver provides an RTMP server for pulling the OBS program from another computer.";
}
