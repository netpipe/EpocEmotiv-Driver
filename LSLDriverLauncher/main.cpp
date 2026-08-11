#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>


//install_name_tool \ -change \ /Users/mac/Desktop/brew/opt/hidapi/lib/libhidapi.0.dylib \ @executable_path/libhidapi.0.dylib \ epoc_lsl

class Launcher : public QWidget
{
public:
    Launcher(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle("Emotiv EPOC LSL Launcher");
        resize(700, 450);

        startButton = new QPushButton("Start");
        stopButton  = new QPushButton("Stop");
        stopButton->setEnabled(false);

        status = new QLabel("Stopped");

        log = new QPlainTextEdit;
        log->setReadOnly(true);

        process = new QProcess(this);

        auto *buttons = new QHBoxLayout;
        buttons->addWidget(startButton);
        buttons->addWidget(stopButton);
        buttons->addStretch();
        buttons->addWidget(status);

        auto *layout = new QVBoxLayout(this);
        layout->addLayout(buttons);
        layout->addWidget(log);

        connect(startButton, &QPushButton::clicked,
                this, [=](){ startDriver(); });

        connect(stopButton, &QPushButton::clicked,
                this, [=](){ stopDriver(); });

        process->setStandardOutputFile(QProcess::nullDevice()); //faster
        process->setStandardErrorFile(QProcess::nullDevice());

//        connect(process, &QProcess::readyReadStandardOutput,
//                this, [=]()
//        {
//            log->appendPlainText(
//                QString::fromLocal8Bit(process->readAllStandardOutput()));
//        });

//        connect(process, &QProcess::readyReadStandardError,
//                this, [=]()
//        {
//            log->appendPlainText(
//                QString::fromLocal8Bit(process->readAllStandardError()));
//        });

        connect(process,
                QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [=](int code, QProcess::ExitStatus)
        {
            Q_UNUSED(code);

            startButton->setEnabled(true);
            stopButton->setEnabled(false);
            status->setText("Stopped");
        });
    }

private:

    void startDriver()
    {
        QString appDir = QCoreApplication::applicationDirPath();

        // Driver executable beside this launcher
        QString executable = appDir + "/epoc_lsl";

        // lsl.framework located in:
        // EmotivLauncher.app/Contents/Frameworks/
        QString frameworkPath =
                QDir(appDir).absoluteFilePath("../Frameworks");

        QProcessEnvironment env =
                QProcessEnvironment::systemEnvironment();

        env.insert("DYLD_FRAMEWORK_PATH", frameworkPath);

        process->setProcessEnvironment(env);

        log->appendPlainText("Starting...");
        log->appendPlainText("Executable: " + executable);
        log->appendPlainText("Frameworks: " + frameworkPath);
        log->appendPlainText("");

        process->start(executable);

        if (!process->waitForStarted(3000))
        {
            log->appendPlainText("Failed to start.");
            return;
        }

        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        status->setText("Running");
    }

    void stopDriver()
    {
        if (process->state() == QProcess::NotRunning)
            return;

        process->terminate();

        if (!process->waitForFinished(3000))
            process->kill();
    }

    QPushButton *startButton;
    QPushButton *stopButton;
    QLabel *status;
    QPlainTextEdit *log;
    QProcess *process;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Launcher w;
    w.show();

    return app.exec();
}
