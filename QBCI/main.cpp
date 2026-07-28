/*****************************************************************
 * OpenBCI LSL Qt Demo with Auto-Scaling Wave Display & FFT Brainwaves
 * Qt 5.12+ / Qt 6 compatible
 * Single source file
 *
 * Requires:
 * - liblsl
 * - Qt Widgets
 *****************************************************************/

#include <QtWidgets>
#include <QMutex>
#include <lsl_cpp.h>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <algorithm>
#include <complex>
#include <cmath>
#include <deque>

// ------------------------------------------------------------------
// Lightweight In-Place Radix-2 FFT
// ------------------------------------------------------------------
void fft(std::vector<std::complex<double>>& x) {
    int n = x.size();
    if (n == 0) return;
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * 3.14159265358979323846 / len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1, 0);
            for (int k = 0; k < len / 2; k++) {
                std::complex<double> u = x[i+k];
                std::complex<double> v = x[i+k+len/2] * w;
                x[i+k] = u + v;
                x[i+k+len/2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ------------------------------------------------------------------
// Ring Buffer for Waveform Display
// ------------------------------------------------------------------
struct RingBuffer {
    std::vector<double> data;
    int head = 0;
    int count = 0;
    int capacity;

    RingBuffer(int cap = 1000) : capacity(cap) { data.resize(cap, 0.0); }

    void push(double val) {
        data[head] = val;
        head = (head + 1) % capacity;
        if (count < capacity) count++;
    }

    void clear() {
        head = 0; count = 0;
        std::fill(data.begin(), data.end(), 0.0);
    }
};

// ------------------------------------------------------------------
// Waveform Display Widget
// ------------------------------------------------------------------
class WaveDisplayWidget : public QWidget {
public:
    WaveDisplayWidget(int numChannels = 8, int secondsOfHistory = 5, double sampleRate = 256.0, QWidget *parent = nullptr)
        : QWidget(parent), numChannels(numChannels) {
        int maxSamples = static_cast<int>(secondsOfHistory * sampleRate);
        buffers.resize(numChannels, RingBuffer(maxSamples));
        colors = { QColor(255, 100, 100), QColor(100, 255, 100), QColor(100, 150, 255),
                   QColor(255, 255, 100), QColor(255, 100, 255), QColor(100, 255, 255),
                   QColor(255, 200, 100), QColor(200, 200, 200) };
    }

    void setNumChannels(int n, double sampleRate = 256.0, int secondsOfHistory = 5) {
        QMutexLocker locker(&mutex);
        numChannels = n;
        int maxSamples = static_cast<int>(secondsOfHistory * sampleRate);
        buffers.resize(numChannels, RingBuffer(maxSamples));
    }

    void pushSample(const std::vector<double>& sample) {
        QMutexLocker locker(&mutex);
        int n = std::min((int)sample.size(), numChannels);
        for (int i = 0; i < n; ++i) buffers[i].push(sample[i]);
    }

    void clear() {
        QMutexLocker locker(&mutex);
        for (int i = 0; i < numChannels; ++i) buffers[i].clear();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QMutexLocker locker(&mutex);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(30, 30, 30));

        if (numChannels == 0) return;
        int w = width(), h = height(), channelHeight = h / numChannels;

        for (int ch = 0; ch < numChannels; ++ch) {
            if (buffers[ch].count == 0) continue;
            int count = buffers[ch].count, capacity = buffers[ch].capacity, head = buffers[ch].head;

            double minVal = buffers[ch].data[0], maxVal = buffers[ch].data[0];
            for (int i = 0; i < count; ++i) {
                int idx = (head - count + i + capacity) % capacity;
                if (buffers[ch].data[idx] < minVal) minVal = buffers[ch].data[idx];
                if (buffers[ch].data[idx] > maxVal) maxVal = buffers[ch].data[idx];
            }
            double range = maxVal - minVal;
            if (range < 1.0) range = 1.0;

            int yCenter = ch * channelHeight + channelHeight / 2;
            painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::DotLine));
            painter.drawLine(0, yCenter, w, yCenter);

            painter.setPen(QPen(colors[ch % colors.size()], 1.5));
            QPainterPath path;
            for (int i = 0; i < count; ++i) {
                int idx = (head - count + i + capacity) % capacity;
                double val = buffers[ch].data[idx];
                double x = (count > 1) ? ((double)i / (count - 1) * w) : (w / 2.0);
                double normalizedY = (val - minVal) / range;
                double y = (ch * channelHeight) + (channelHeight * 0.1) + (1.0 - normalizedY) * (channelHeight * 0.8);
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            painter.drawPath(path);

            // FIX: Corrected QString::arg syntax (using %1, %2, %3 instead of printf %.0f)
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            painter.drawText(10, ch * channelHeight + 20,
                QString("CH %1 (%2 to %3)").arg(ch + 1).arg((int)minVal).arg((int)maxVal));
        }
    }

private:
    int numChannels;
    std::vector<RingBuffer> buffers;
    std::vector<QColor> colors;
    QMutex mutex;
};

// ------------------------------------------------------------------
// Brainwave Activity Indicator Widget
// ------------------------------------------------------------------
class BrainWaveIndicator : public QWidget {
public:
    BrainWaveIndicator(QWidget *parent = nullptr) : QWidget(parent) {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(5, 5, 5, 5);

        titleLabel = new QLabel("<b>Global Brainwave Activity</b><br><span style='font-size:10pt; font-weight:normal;'>(All Channels Avg)</span>", this);
        titleLabel->setAlignment(Qt::AlignCenter);

        alphaBar = new QProgressBar(this);
        alphaBar->setRange(0, 100); alphaBar->setValue(50); alphaBar->setTextVisible(true);
        alphaBar->setFormat("Alpha (8-12 Hz): %p%");
        alphaBar->setStyleSheet("QProgressBar { border: 1px solid gray; border-radius: 5px; text-align: center; background: #444; color: white; }"
                                "QProgressBar::chunk { background-color: #4CAF50; border-radius: 4px; }");

        betaBar = new QProgressBar(this);
        betaBar->setRange(0, 100); betaBar->setValue(50); betaBar->setTextVisible(true);
        betaBar->setFormat("Beta (13-30 Hz): %p%");
        betaBar->setStyleSheet("QProgressBar { border: 1px solid gray; border-radius: 5px; text-align: center; background: #444; color: white; }"
                               "QProgressBar::chunk { background-color: #2196F3; border-radius: 4px; }");

        ratioLabel = new QLabel("Alpha/Beta Ratio: 1.00", this);
        ratioLabel->setAlignment(Qt::AlignCenter);
        ratioLabel->setFont(QFont("Arial", 11, QFont::Bold));
        ratioLabel->setStyleSheet("color: white; margin-top: 5px;");

        layout->addWidget(titleLabel);
        layout->addWidget(alphaBar);
        layout->addWidget(betaBar);
        layout->addWidget(ratioLabel);
    }

    void updateValues(double alphaPct, double betaPct, double ratio) {
        currentAlpha = currentAlpha * 0.8 + alphaPct * 0.2;
        currentBeta = currentBeta * 0.8 + betaPct * 0.2;

        alphaBar->setValue(static_cast<int>(std::round(currentAlpha)));
        betaBar->setValue(static_cast<int>(std::round(currentBeta)));
        ratioLabel->setText(QString("Alpha/Beta Ratio: %1").arg(ratio, 0, 'f', 2));
    }

private:
    QLabel *titleLabel;
    QProgressBar *alphaBar;
    QProgressBar *betaBar;
    QLabel *ratioLabel;
    double currentAlpha = 50.0;
    double currentBeta = 50.0;
};

// ------------------------------------------------------------------
// Main Window
// ------------------------------------------------------------------
class MainWindow : public QWidget {
public:
    MainWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("OpenBCI LSL Demo with FFT Brainwaves");
        resize(1000, 650);

        connectButton = new QPushButton("Connect");
        disconnectButton = new QPushButton("Disconnect");
        disconnectButton->setEnabled(false);

        statusLabel = new QLabel("Disconnected");
        sampleLabel = new QLabel("Samples: 0");

        waveDisplay = new WaveDisplayWidget(8, 5, 256.0, this);
        brainwaveIndicator = new BrainWaveIndicator(this);
        brainwaveIndicator->setMaximumWidth(320);

        logWindow = new QTextEdit();
        logWindow->setReadOnly(true);

        auto topLayout = new QHBoxLayout();
        topLayout->addWidget(connectButton);
        topLayout->addWidget(disconnectButton);
        topLayout->addStretch();
        topLayout->addWidget(brainwaveIndicator);

        auto mainLayout = new QVBoxLayout(this);
        mainLayout->addLayout(topLayout);
        mainLayout->addWidget(statusLabel);
        mainLayout->addWidget(sampleLabel);
        mainLayout->addWidget(new QLabel("Wave Display (Auto-Scaled Time Series)"));
        mainLayout->addWidget(waveDisplay, 1);
        mainLayout->addWidget(new QLabel("Log"));
        mainLayout->addWidget(logWindow, 0);

        connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectStream);
        connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectStream);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::updateDisplay);
        timer->start(50); // 20 FPS GUI update
    }

    ~MainWindow() { disconnectStream(); }

private:
    QPushButton *connectButton;
    QPushButton *disconnectButton;
    QLabel *statusLabel;
    QLabel *sampleLabel;
    WaveDisplayWidget *waveDisplay;
    BrainWaveIndicator *brainwaveIndicator;
    QTextEdit *logWindow;
    QTimer *timer;

    std::unique_ptr<lsl::stream_inlet> inlet;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> samples{0};

    QMutex fft_mutex;
    std::vector<std::deque<double>> channel_buffers;
    int fft_sample_counter = 0;
    double sample_rate = 128.0;

    std::atomic<double> latestAlpha{50.0};
    std::atomic<double> latestBeta{50.0};
    std::atomic<double> latestRatio{1.0};

    const int fft_size = 256;
    const int hop_size = 128;

    void log(const QString &text) { logWindow->append(text); }

    void connectStream() {
        log("Searching for LSL streams...");
        std::vector<lsl::stream_info> streams;
        try { streams = lsl::resolve_streams(2.0); }
        catch (...) { QMessageBox::critical(this, "LSL", "Unable to search for LSL streams."); return; }

        if (streams.empty()) { QMessageBox::information(this, "LSL", "No LSL streams found."); return; }

        QStringList choices;
        for (size_t i = 0; i < streams.size(); ++i) {
            const auto &s = streams[i];
            // FIX: Corrected QString::arg syntax
            choices << QString("%1 [%2] %3 ch %4 Hz")
                           .arg(QString::fromStdString(s.name()))
                           .arg(QString::fromStdString(s.type()))
                           .arg(s.channel_count())
                           .arg(s.nominal_srate(), 0, 'f', 1);
        }

        bool ok = false;
        QString selected = QInputDialog::getItem(this, "Select LSL Stream", "Available Streams:", choices, 0, false, &ok);
        if (!ok) return;
        int index = choices.indexOf(selected);
        if (index < 0) return;

        try { inlet.reset(new lsl::stream_inlet(streams[index])); }
        catch (...) { QMessageBox::critical(this, "LSL", "Unable to open stream."); return; }

        sample_rate = streams[index].nominal_srate();
        if (sample_rate <= 0) sample_rate = 128.0;

        int numCh = streams[index].channel_count();
        {
            QMutexLocker locker(&fft_mutex);
            channel_buffers.resize(numCh);
            for (auto& buf : channel_buffers) buf.clear();
            fft_sample_counter = 0;
        }

        waveDisplay->setNumChannels(numCh, sample_rate, 5);
        waveDisplay->clear();
        samples = 0;
        running = true;
        worker = std::thread(&MainWindow::readerThread, this);

        // FIX: Corrected QString::arg syntax
        statusLabel->setText(QString("Connected: %1 (%2 ch, %3 Hz)")
            .arg(QString::fromStdString(streams[index].name()))
            .arg(numCh)
            .arg(sample_rate, 0, 'f', 1));

        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);
        log(QString("Connected to '%1'").arg(QString::fromStdString(streams[index].name())));
    }

    void disconnectStream() {
        if (!running) return;
        running = false;
        if (worker.joinable()) worker.join();
        inlet.reset();

        {
            QMutexLocker locker(&fft_mutex);
            for (auto& buf : channel_buffers) buf.clear();
            fft_sample_counter = 0;
        }

        statusLabel->setText("Disconnected");
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
        log("Disconnected.");
    }

    void processBrainwaves() {
        // FIX: Removed QMutexLocker here. The caller (readerThread) already holds the lock.
        // Locking it again would cause a deadlock and freeze the app.
        int numCh = channel_buffers.size();
        if (numCh == 0) return;

        double totalAlpha = 0.0, totalBeta = 0.0;
        double bin_width = sample_rate / fft_size;

        int alpha_start = std::max<int>(1, std::round(8.0 / bin_width));
        int alpha_end = std::min<int>(fft_size / 2 - 1, std::round(12.0 / bin_width));
        int beta_start = std::max<int>(1, std::round(13.0 / bin_width));
        int beta_end = std::min<int>(fft_size / 2 - 1, std::round(30.0 / bin_width));

        for (int ch = 0; ch < numCh; ++ch) {
            if (channel_buffers[ch].size() < fft_size) continue;

            std::vector<std::complex<double>> x(fft_size);
            double sum = 0;
            auto it = channel_buffers[ch].begin();
            for (int i = 0; i < fft_size; ++i, ++it) sum += *it;
            double mean = sum / fft_size;

            it = channel_buffers[ch].begin();
            for (int i = 0; i < fft_size; ++i, ++it) {
                double val = *it - mean;
                double window = 0.5 * (1.0 - std::cos(2 * 3.14159265358979323846 * i / (fft_size - 1)));
                x[i] = std::complex<double>(val * window, 0.0);
            }

            fft(x);

            double alphaPower = 0.0, betaPower = 0.0;
            for (int k = alpha_start; k <= alpha_end; ++k) {
                double mag = std::abs(x[k]);
                alphaPower += mag * mag;
            }
            for (int k = beta_start; k <= beta_end; ++k) {
                double mag = std::abs(x[k]);
                betaPower += mag * mag;
            }

            totalAlpha += alphaPower;
            totalBeta += betaPower;
        }

        double avgAlpha = totalAlpha / numCh;
        double avgBeta = totalBeta / numCh;
        double totalPower = avgAlpha + avgBeta;

        if (totalPower > 0.0) {
            latestAlpha.store((avgAlpha / totalPower) * 100.0);
            latestBeta.store((avgBeta / totalPower) * 100.0);
            latestRatio.store(avgAlpha / avgBeta);
        }
    }

    void readerThread() {
        while (running) {
            if (!inlet) break;
            std::vector<double> sample;
            try {
                double ts = inlet->pull_sample(sample, 0.5);
                if (ts == 0.0) continue;

                waveDisplay->pushSample(sample);
                samples++;

                {
                    QMutexLocker locker(&fft_mutex);
                    int numCh = sample.size();
                    for (int ch = 0; ch < numCh; ++ch) {
                        channel_buffers[ch].push_back(sample[ch]);
                        if (channel_buffers[ch].size() > fft_size) {
                            channel_buffers[ch].pop_front();
                        }
                    }

                    fft_sample_counter++;
                    if (fft_sample_counter >= hop_size) {
                        fft_sample_counter = 0;
                        processBrainwaves();
                    }
                }
            } catch (...) {
                running = false;
                break;
            }
        }
    }

    void updateDisplay() {
        sampleLabel->setText(QString("Samples: %1").arg(samples.load()));
        waveDisplay->update();
        logWindow->moveCursor(QTextCursor::End);

        brainwaveIndicator->updateValues(latestAlpha.load(), latestBeta.load(), latestRatio.load());

        if (!running && disconnectButton->isEnabled()) {
            statusLabel->setText("Connection Lost");
            connectButton->setEnabled(true);
            disconnectButton->setEnabled(false);
            if (worker.joinable()) worker.join();
            inlet.reset();
            log("Reader thread stopped.");
        }
    }
};

/*****************************************************************
 * main()
 *****************************************************************/
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setStyle("Fusion");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    MainWindow window;
    window.show();
    return app.exec();
}
