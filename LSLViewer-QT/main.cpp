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
            streams=lsl::resolve_stream("type","EEG",2.0);
        }
        catch(...)
        {
            QMessageBox::critical(
                        this,
                        "Error",
                        "Unable to search for streams.");
            return;
        }

        if(streams.empty())
        {
            QMessageBox::information(
                        this,
                        "LSL",
                        "No EEG stream found.");
            return;
        }

        try
        {
            inlet.reset(new lsl::stream_inlet(streams[0]));
        }
        catch(...)
        {
            QMessageBox::critical(
                        this,
                        "Error",
                        "Unable to open stream.");
            return;
        }

        int channels=streams[0].channel_count();

        latest.assign(channels,0.0);

        channelList->clear();

        for(int i=0;i<channels;i++)
            channelList->addItem("Channel");

        samples=0;

        running=true;

        worker=std::thread(&MainWindow::readerThread,this);

        statusLabel->setText(
                    QString("Connected : %1")
                    .arg(QString::fromStdString(streams[0].name())));

        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);

        log("Connected.");
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
