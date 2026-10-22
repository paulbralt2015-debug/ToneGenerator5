#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QIODevice>

#include <array>
#include <cmath>
#include <cstring>

static constexpr int CHANNELS = 6;
static constexpr int SAMPLE_RATE = 48000;

static const std::array<QString, CHANNELS> CHANNEL_NAMES = {
    "Gauche", "Droite", "Centre",
    "Surround gauche", "Surround droite", "LFE / graves"
};

class ToneGenerator : public QIODevice
{
public:
    ToneGenerator() {
        open(QIODevice::ReadOnly);
        frequencies.fill(440.0);
        volumes.fill(0.25);
        enabled.fill(true);
        enabled[5] = false;
        phases.fill(0.0);
    }

    void setChannel(int channel, double frequency,
                    double volume, bool active) {
        if (channel < 0 || channel >= CHANNELS)
            return;

        QMutexLocker lock(&mutex);
        frequencies[channel] = frequency;
        volumes[channel] = volume;
        enabled[channel] = active;
    }

    void synchronize(double frequency) {
        QMutexLocker lock(&mutex);
        frequencies.fill(frequency);
    }

    bool isSequential() const override {
        return true;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        QMutexLocker lock(&mutex);

        const qint64 samples = maxSize / 2;
        auto *out = reinterpret_cast<qint16 *>(data);

        for (qint64 i = 0; i < samples; ++i) {
            const int channel = int(sampleCounter % CHANNELS);
            double value = 0.0;

            if (enabled[channel]) {
                value = std::sin(phases[channel] * 2.0 * M_PI)
                        * volumes[channel];

                phases[channel] +=
                    frequencies[channel] / double(SAMPLE_RATE);

                if (phases[channel] >= 1.0)
                    phases[channel] -= std::floor(phases[channel]);
            }

            const double limited =
                value < -1.0 ? -1.0 :
                value > 1.0 ? 1.0 : value;

            out[i] = static_cast<qint16>(limited * 32767.0);
            ++sampleCounter;
        }

        return samples * 2;
    }

    qint64 writeData(const char *, qint64) override {
        return -1;
    }

private:
    QMutex mutex;
    std::array<double, CHANNELS> frequencies;
    std::array<double, CHANNELS> volumes;
    std::array<bool, CHANNELS> enabled;
    std::array<double, CHANNELS> phases;
    quint64 sampleCounter = 0;
};

class MainWindow : public QWidget
{
public:
    MainWindow() {
        setWindowTitle("Tone Generator 5");
        resize(760, 650);

        auto *root = new QVBoxLayout(this);

        auto *title = new QLabel(
            "<h1>Tone Generator 5</h1>"
            "<p>Générateur de tonalités multicanal</p>");
        title->setTextFormat(Qt::RichText);
        root->addWidget(title);

        auto *deviceBox = new QGroupBox("Sortie audio");
        auto *deviceLayout = new QHBoxLayout(deviceBox);

        outputType = new QComboBox;
        outputType->addItem("Périphérique physique");
        outputType->addItem("Périphérique virtuel");

        outputDevice = new QComboBox;
        deviceLayout->addWidget(new QLabel("Type :"));
        deviceLayout->addWidget(outputType);
        deviceLayout->addWidget(new QLabel("Périphérique :"));
        deviceLayout->addWidget(outputDevice, 1);
        root->addWidget(deviceBox);

        auto *help = new QLabel(
            "La sortie virtuelle nécessite un pilote audio virtuel "
            "compatible déjà installé. Le programme ne crée pas de pilote.");
        help->setWordWrap(true);
        root->addWidget(help);

        auto *channelsBox = new QGroupBox("Réglages des canaux");
        auto *channelsLayout = new QVBoxLayout(channelsBox);

        for (int i = 0; i < CHANNELS; ++i) {
            auto *line = new QHBoxLayout;

            active[i] = new QCheckBox(CHANNEL_NAMES[i]);
            active[i]->setChecked(i != 5);

            frequency[i] = new QSpinBox;
            frequency[i]->setRange(1, 20000);
            frequency[i]->setValue(440);
            frequency[i]->setSuffix(" Hz");
            frequency[i]->setMinimumWidth(120);

            volume[i] = new QSlider(Qt::Horizontal);
            volume[i]->setRange(0, 100);
            volume[i]->setValue(25);
            volume[i]->setMinimumWidth(100);

            volumeLabel[i] = new QLabel("25 %");
            volumeLabel[i]->setMinimumWidth(45);

            line->addWidget(active[i], 2);
            line->addWidget(new QLabel("Fréquence :"));
            line->addWidget(frequency[i]);
            line->addWidget(new QLabel("Volume :"));
            line->addWidget(volume[i], 1);
            line->addWidget(volumeLabel[i]);

            channelsLayout->addLayout(line);

            connect(frequency[i],
                    &QSpinBox::valueChanged, this,
                    [this, i](int) { updateChannel(i); });

            connect(volume[i], &QSlider::valueChanged,
                    this, [this, i](int value) {
                volumeLabel[i]->setText(
                    QString("%1 %").arg(value));
                updateChannel(i);
            });

            connect(active[i], &QCheckBox::toggled,
                    this, [this, i](bool) {
                updateChannel(i);
            });
        }

        root->addWidget(channelsBox);

        auto *buttons = new QHBoxLayout;

        auto *syncButton = new QPushButton(
            "Synchroniser les fréquences");
        auto *startButton = new QPushButton("▶ Démarrer");
        auto *stopButton = new QPushButton("■ Arrêter");

        buttons->addWidget(syncButton);
        buttons->addStretch();
        buttons->addWidget(startButton);
        buttons->addWidget(stopButton);
        root->addLayout(buttons);

        status = new QLabel("Prêt.");
        root->addWidget(status);

        connect(outputType,
                &QComboBox::currentIndexChanged,
                this, [this](int) { refreshDevices(); });

        connect(syncButton, &QPushButton::clicked,
                this, [this]() {
            const int hz = frequency[0]->value();
            for (int i = 1; i < CHANNELS; ++i)
                frequency[i]->setValue(hz);
            generator.synchronize(hz);
            status->setText(
                QString("Toutes les fréquences sont réglées sur %1 Hz.")
                    .arg(hz));
        });

        connect(startButton, &QPushButton::clicked,
                this, [this]() { startAudio(); });

        connect(stopButton, &QPushButton::clicked,
                this, [this]() { stopAudio(); });

        refreshDevices();
        for (int i = 0; i < CHANNELS; ++i)
            updateChannel(i);
    }

    ~MainWindow() override {
        stopAudio();
    }

private:
    static bool looksVirtual(const QString &name) {
        const QString n = name.toLower();
        return n.contains("virtual")
            || n.contains("voicemeeter")
            || n.contains("vb-audio")
            || n.contains("cable")
            || n.contains("loopback");
    }

    void refreshDevices() {
        outputDevice->clear();

        const bool wantVirtual = outputType->currentIndex() == 1;
        const auto devices = QMediaDevices::audioOutputs();

        for (const QAudioDevice &device : devices) {
            const bool virtualDevice =
                looksVirtual(device.description());

            if (virtualDevice == wantVirtual) {
                outputDevice->addItem(
                    device.description(),
                    QString::fromUtf8(device.id()));
            }
        }

        if (outputDevice->count() == 0) {
            outputDevice->addItem(
                wantVirtual
                    ? "Aucun périphérique virtuel détecté"
                    : "Aucun périphérique physique détecté",
                QString());
        }
    }

    void updateChannel(int i) {
        generator.setChannel(
            i,
            frequency[i]->value(),
            volume[i]->value() / 100.0,
            active[i]->isChecked());
    }

    void startAudio() {
        stopAudio();

        const QString id = outputDevice->currentData().toString();
        if (id.isEmpty()) {
            QMessageBox::warning(
                this, "Sortie indisponible",
                "Aucun périphérique de ce type n'a été détecté. "
                "Vérifie les périphériques audio de Windows.");
            return;
        }

        QAudioDevice selected;
        bool found = false;

        for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
            if (QString::fromUtf8(device.id()) == id) {
                selected = device;
                found = true;
                break;
            }
        }

        if (!found) {
            QMessageBox::warning(
                this, "Périphérique introuvable",
                "Le périphérique audio sélectionné n'est plus disponible.");
            refreshDevices();
            return;
        }

        QAudioFormat format;
        format.setSampleRate(SAMPLE_RATE);
        format.setChannelCount(CHANNELS);
        format.setChannelConfig(
            QAudioFormat::ChannelConfigSurround5Dot1);
        format.setSampleFormat(QAudioFormat::Int16);

        if (!selected.isFormatSupported(format)) {
            QMessageBox::warning(
                this, "Format audio non pris en charge",
                "Ce périphérique ne prend pas en charge le format "
                "PCM 16 bits, 48 kHz, 5.1 canaux demandé. "
                "Choisis un périphérique compatible avec la sortie 5.1.");
            return;
        }

        for (int i = 0; i < CHANNELS; ++i)
            updateChannel(i);

        sink = new QAudioSink(selected, format, this);
        connect(sink, &QAudioSink::stateChanged,
                this, [this](QAudio::State state) {
            if (state == QAudio::IdleState)
                status->setText("Lecture en cours — signal silencieux possible.");
            else if (state == QAudio::StoppedState
                     && sink && sink->error() != QAudio::NoError)
                status->setText("Erreur de sortie audio.");
        });

        sink->start(&generator);

        if (sink->error() != QAudio::NoError) {
            QMessageBox::warning(
                this, "Erreur audio",
                "Impossible de démarrer la sortie audio.");
            stopAudio();
            return;
        }

        status->setText(
            "Lecture en cours : 6 canaux, 48 kHz, PCM 16 bits.");
    }

    void stopAudio() {
        if (sink) {
            sink->stop();
            delete sink;
            sink = nullptr;
        }
        status->setText("Lecture arrêtée.");
    }

    QComboBox *outputType = nullptr;
    QComboBox *outputDevice = nullptr;
    std::array<QCheckBox *, CHANNELS> active{};
    std::array<QSpinBox *, CHANNELS> frequency{};
    std::array<QSlider *, CHANNELS> volume{};
    std::array<QLabel *, CHANNELS> volumeLabel{};
    QLabel *status = nullptr;

    ToneGenerator generator;
    QAudioSink *sink = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
