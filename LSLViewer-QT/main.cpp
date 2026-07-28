/****************************************************************************
    OpenBCI LSL Qt Demo
    Qt 5.12
    Single source file
    macOS compatible

    Requires:

        liblsl
        Qt Widgets

****************************************************************************/

#include <QtWidgets>

#include <lsl_cpp.h>

#include <atomic>
#include <thread>
#include <vector>
#include <memory>

class MainWindow : public QWidget
{
public:

    MainWindow(QWidget *parent=nullptr)
        : QWidget(parent)
    {
        setWindowTitle("OpenBCI LSL Demo");

        resize(700,500);

        connectButton=new QPushButton("Connect");
        disconnectButton=new QPushButton("Disconnect");

        disconnectButton->setEnabled(false);

        statusLabel=new QLabel("Disconnected");
        sampleLabel=new QLabel("Samples: 0");

        channelList=new QListWidget();

        logWindow=new QTextEdit();
        logWindow->setReadOnly(true);

        auto layout=new QVBoxLayout(this);

        auto buttons=new QHBoxLayout();

        buttons->addWidget(connectButton);
        buttons->addWidget(disconnectButton);

        layout->addLayout(buttons);

        layout->addWidget(statusLabel);
        layout->addWidget(sampleLabel);

        layout->addWidget(new QLabel("Channels"));

        layout->addWidget(channelList);

        layout->addWidget(new QLabel("Log"));

        layout->addWidget(logWindow);

        connect(connectButton,
                &QPushButton::clicked,
                this,
                &MainWindow::connectStream);

        connect(disconnectButton,
                &QPushButton::clicked,
                this,
                &MainWindow::disconnectStream);

        timer=new QTimer(this);

        connect(timer,
                &QTimer::timeout,
                this,
                &MainWindow::updateDisplay);

        timer->start(100);
    }

    ~MainWindow()
    {
        disconnectStream();
    }

private:

    QPushButton *connectButton;
    QPushButton *disconnectButton;

    QLabel *statusLabel;
    QLabel *sampleLabel;

    QListWidget *channelList;

    QTextEdit *logWindow;

    QTimer *timer;

    std::unique_ptr<lsl::stream_inlet> inlet;

    std::thread worker;

    std::atomic<bool> running{false};

    std::atomic<uint64_t> samples{0};

    std::vector<double> latest;

    QMutex mutex;

    void log(const QString &text)
    {
        logWindow->append(text);
    }

    void connectStream()
    {
        log("Searching for LSL streams...");

        std::vector<lsl::stream_info> streams;

        try
        {
            // Find every visible LSL stream
            streams = lsl::resolve_streams(2.0);
        }
        catch(...)
        {
            QMessageBox::critical(this,
                                  "LSL",
                                  "Unable to search for LSL streams.");
            return;
        }

        if(streams.empty())
        {
            QMessageBox::information(this,
                                     "LSL",
                                     "No LSL streams found.");
            return;
        }

        QStringList choices;

        for(size_t i=0;i<streams.size();++i)
        {
            const auto &s = streams[i];

            choices << QString("%1   [%2]   %3 ch   %.1f Hz")
                       .arg(QString::fromStdString(s.name()))
                       .arg(QString::fromStdString(s.type()))
                       .arg(s.channel_count())
                       .arg(s.nominal_srate());
        }

        bool ok=false;

        QString selected =
            QInputDialog::getItem(this,
                                  "Select LSL Stream",
                                  "Available Streams:",
                                  choices,
                                  0,
                                  false,
                                  &ok);

        if(!ok)
            return;

        int index = choices.indexOf(selected);

        if(index < 0)
            return;

        try
        {
            inlet.reset(new lsl::stream_inlet(streams[index]));
        }
        catch(...)
        {
            QMessageBox::critical(this,
                                  "LSL",
                                  "Unable to open stream.");
            return;
        }

        latest.assign(streams[index].channel_count(),0.0);

        channelList->clear();

        for(int i=0;i<streams[index].channel_count();++i)
            channelList->addItem("");

        samples = 0;

        running = true;

        worker = std::thread(&MainWindow::readerThread,this);

        statusLabel->setText(
            QString("Connected: %1")
            .arg(QString::fromStdString(streams[index].name())));

        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);

        log(QString("Connected to '%1'")
            .arg(QString::fromStdString(streams[index].name())));
    }

    void disconnectStream()
    {
        if(!running)
            return;

        running=false;

        if(worker.joinable())
            worker.join();

        inlet.reset();

        statusLabel->setText("Disconnected");

        connectButton->setEnabled(true);

        disconnectButton->setEnabled(false);

        log("Disconnected.");
    }

    void readerThread()
    {
        while(running)
        {
            if(!inlet)
                break;

            std::vector<double> sample;

            try
            {
                double ts=inlet->pull_sample(
                            sample,
                            0.5);

                if(ts==0.0)
                    continue;

                {
                    QMutexLocker locker(&mutex);

                    latest=sample;
                }

                samples++;

            }
            catch(...)
            {
                running=false;
                break;
            }
        }
    }

    void updateDisplay();
};

/****************************************************************************
    Remaining implementation
****************************************************************************/

void MainWindow::updateDisplay()
{
    sampleLabel->setText(
        QString("Samples: %1").arg(samples.load()));

    QMutexLocker locker(&mutex);

    // If the number of channels changes unexpectedly,
    // resize the list to match.
    while (channelList->count() < (int)latest.size())
        channelList->addItem("");

    while (channelList->count() > (int)latest.size())
        delete channelList->takeItem(channelList->count() - 1);

    for (int i = 0; i < (int)latest.size(); ++i)
    {
        QListWidgetItem *item = channelList->item(i);

        item->setText(
            QString("CH%1 : %2")
                .arg(i + 1, 2)
                .arg(latest[i], 0, 'f', 3));
    }

    // Optional: keep the log scrolled to the bottom.
    logWindow->moveCursor(QTextCursor::End);

    // If the reader thread stopped because of an error,
    // update the UI once.
    if (!running && disconnectButton->isEnabled())
    {
        statusLabel->setText("Connection Lost");

        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);

        if (worker.joinable())
            worker.join();

        inlet.reset();

        log("Reader thread stopped.");
    }
}

/****************************************************************************
    main()
****************************************************************************/

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
