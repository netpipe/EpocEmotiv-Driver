/*****************************************************************
 * OpenBCI LSL Qt Demo with Auto-Scaling Wave Display
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

// ------------------------------------------------------------------
// Ring Buffer for efficient, allocation-free sample history storage
// ------------------------------------------------------------------
struct RingBuffer {
    std::vector<double> data;
    int head = 0;
    int count = 0;
    int capacity;

    RingBuffer(int cap = 1000) : capacity(cap) {
        data.resize(cap, 0.0);
    }

    void push(double val) {
        data[head] = val;
        head = (head + 1) % capacity;
        if (count < capacity) count++;
    }

    void clear() {
        head = 0;
        count = 0;
        std::fill(data.begin(), data.end(), 0.0);
    }
};

// ------------------------------------------------------------------
// Custom Waveform Display Widget (OpenBCI Style with Auto-Scaling)
// ------------------------------------------------------------------
class WaveDisplayWidget : public QWidget {
public:
    WaveDisplayWidget(int numChannels = 8, int secondsOfHistory = 5, double sampleRate = 256.0, QWidget *parent = nullptr)
        : QWidget(parent), numChannels(numChannels) {
        int maxSamples = static_cast<int>(secondsOfHistory * sampleRate);
        buffers.resize(numChannels, RingBuffer(maxSamples));

        // Distinct colors for each channel
        colors = {
            QColor(255, 100, 100), // Red
            QColor(100, 255, 100), // Green
            QColor(100, 150, 255), // Blue
            QColor(255, 255, 100), // Yellow
            QColor(255, 100, 255), // Magenta
            QColor(100, 255, 255), // Cyan
            QColor(255, 200, 100), // Orange
            QColor(200, 200, 200)  // Gray
        };
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
        for (int i = 0; i < n; ++i) {
            buffers[i].push(sample[i]);
        }
    }

    void clear() {
        QMutexLocker locker(&mutex);
        for (int i = 0; i < numChannels; ++i) {
            buffers[i].clear();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QMutexLocker locker(&mutex);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Dark background similar to OpenBCI GUI
        painter.fillRect(rect(), QColor(30, 30, 30));

        if (numChannels == 0) return;

        int w = width();
        int h = height();
        int channelHeight = h / numChannels;

        for (int ch = 0; ch < numChannels; ++ch) {
            if (buffers[ch].count == 0) continue;

            int count = buffers[ch].count;
            int capacity = buffers[ch].capacity;
            int head = buffers[ch].head;

            // 1. AUTO-SCALING: Find min and max in the current visible buffer
            double minVal = buffers[ch].data[0];
            double maxVal = buffers[ch].data[0];

            for (int i = 0; i < count; ++i) {
                int actualIndex = (head - count + i + capacity) % capacity;
                double val = buffers[ch].data[actualIndex];
                if (val < minVal) minVal = val;
                if (val > maxVal) maxVal = val;
            }

            // Prevent division by zero if the signal is perfectly flat (e.g., disconnected electrode)
            double range = maxVal - minVal;
            if (range < 1.0) range = 1.0;

            // 2. Draw channel baseline
            int yCenter = ch * channelHeight + channelHeight / 2;
            painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::DotLine));
            painter.drawLine(0, yCenter, w, yCenter);

            // 3. Draw waveform
            painter.setPen(QPen(colors[ch % colors.size()], 1.5));

            QPainterPath path;
            for (int i = 0; i < count; ++i) {
                int actualIndex = (head - count + i + capacity) % capacity;
                double val = buffers[ch].data[actualIndex];

                double x = (count > 1) ? ((double)i / (count - 1) * w) : (w / 2.0);

                // Normalize to 0.0 - 1.0 based on the min/max of visible data
                double normalizedY = (val - minVal) / range;

                // Map to channel height, leaving 10% margin top and bottom
                double y = (ch * channelHeight) + (channelHeight * 0.1) + (1.0 - normalizedY) * (channelHeight * 0.8);

                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            painter.drawPath(path);

            // 4. Draw channel label and current raw value range (helpful for debugging)
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            painter.drawText(10, ch * channelHeight + 20,
                QString("CH %1 (%.0f to %.0f)").arg(ch + 1).arg(minVal).arg(maxVal));
        }
    }

private:
    int numChannels;
    std::vector<RingBuffer> buffers;
    std::vector<QColor> colors;
    QMutex mutex;
};

// ------------------------------------------------------------------
// Main Window
// ------------------------------------------------------------------
class MainWindow : public QWidget {
public:
    MainWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle("OpenBCI LSL Demo with Auto-Scaling Wave Display");
        resize(900, 600);

        connectButton = new QPushButton("Connect");
        disconnectButton = new QPushButton("Disconnect");
        disconnectButton->setEnabled(false);

        statusLabel = new QLabel("Disconnected");
        sampleLabel = new QLabel("Samples: 0");

        waveDisplay = new WaveDisplayWidget(8, 5, 256.0, this);

        logWindow = new QTextEdit();
        logWindow->setReadOnly(true);

        auto layout = new QVBoxLayout(this);
        auto buttons = new QHBoxLayout();
        buttons->addWidget(connectButton);
        buttons->addWidget(disconnectButton);
        layout->addLayout(buttons);
        layout->addWidget(statusLabel);
        layout->addWidget(sampleLabel);
        layout->addWidget(new QLabel("Wave Display (Auto-Scaled Time Series)"));
        layout->addWidget(waveDisplay, 1); // Give wave display expanding stretch
        layout->addWidget(new QLabel("Log"));
        layout->addWidget(logWindow, 0); // Log window gets less stretch

        connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectStream);
        connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectStream);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::updateDisplay);
        timer->start(50); // Update display at 20 FPS for smooth waveform rendering
    }

    ~MainWindow() {
        disconnectStream();
    }

private:
    QPushButton *connectButton;
    QPushButton *disconnectButton;
    QLabel *statusLabel;
    QLabel *sampleLabel;
    WaveDisplayWidget *waveDisplay;
    QTextEdit *logWindow;
    QTimer *timer;

    std::unique_ptr<lsl::stream_inlet> inlet;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> samples{0};
    QMutex mutex;

    void log(const QString &text) {
        logWindow->append(text);
    }

    void connectStream() {
        log("Searching for LSL streams...");
        std::vector<lsl::stream_info> streams;
        try {
            streams = lsl::resolve_streams(2.0);
        } catch (...) {
            QMessageBox::critical(this, "LSL", "Unable to search for LSL streams.");
            return;
        }

        if (streams.empty()) {
            QMessageBox::information(this, "LSL", "No LSL streams found.");
            return;
        }

        QStringList choices;
        for (size_t i = 0; i < streams.size(); ++i) {
            const auto &s = streams[i];
            choices << QString("%1 [%2] %3 ch %.1f Hz")
                           .arg(QString::fromStdString(s.name()))
                           .arg(QString::fromStdString(s.type()))
                           .arg(s.channel_count())
                           .arg(s.nominal_srate());
        }

        bool ok = false;
        QString selected = QInputDialog::getItem(this, "Select LSL Stream", "Available Streams:", choices, 0, false, &ok);
        if (!ok) return;

        int index = choices.indexOf(selected);
        if (index < 0) return;

        try {
            inlet.reset(new lsl::stream_inlet(streams[index]));
        } catch (...) {
            QMessageBox::critical(this, "LSL", "Unable to open stream.");
            return;
        }

        int numCh = streams[index].channel_count();
        double srate = streams[index].nominal_srate();
        if (srate <= 0) srate = 256.0; // Fallback sample rate

        waveDisplay->setNumChannels(numCh, srate, 5); // 5 seconds of history
        waveDisplay->clear();
        samples = 0;
        running = true;
        worker = std::thread(&MainWindow::readerThread, this);

        statusLabel->setText(QString("Connected: %1").arg(QString::fromStdString(streams[index].name())));
        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);
        log(QString("Connected to '%1'").arg(QString::fromStdString(streams[index].name())));
    }

    void disconnectStream() {
        if (!running) return;
        running = false;
        if (worker.joinable()) worker.join();
        inlet.reset();
        statusLabel->setText("Disconnected");
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
        log("Disconnected.");
    }

    void readerThread() {
        while (running) {
            if (!inlet) break;
            std::vector<double> sample;
            try {
                double ts = inlet->pull_sample(sample, 0.5);
                if (ts == 0.0) continue;

                // Push to wave display (thread-safe)
                waveDisplay->pushSample(sample);
                samples++;
            } catch (...) {
                running = false;
                break;
            }
        }
    }

    void updateDisplay() {
        sampleLabel->setText(QString("Samples: %1").arg(samples.load()));

        // Trigger wave display repaint
        waveDisplay->update();

        // Keep the log scrolled to the bottom
        logWindow->moveCursor(QTextCursor::End);

        // If the reader thread stopped because of an error, update the UI once.
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
    MainWindow window;
    window.show();
    return app.exec();
}
