#pragma once

#include <QObject>

#include "startstream.h"

class ComputerManager;
class Session;

// Runs the CLI actions with no UI at all. Progress and failures are reported
// as "beam:" status lines on stdout (see beamstatus.h) and through the
// process exit code, never as a window or dialog: the user is looking at
// Beam, which renders errors in its own voice.
namespace CliHeadless
{

class StreamRunner : public QObject
{
    Q_OBJECT

public:
    explicit StreamRunner(CliStartStream::Launcher* launcher, QObject* parent = nullptr);

    void run(ComputerManager* computerManager);

private slots:
    void onSessionCreated(QString appName, Session* session);
    void onLaunchFailed(QString message);
    void onAppQuitRequired(QString appName);
    void onStageFailed(QString stage, int errorCode, QString failingPorts);
    void onDisplayLaunchError(QString text);
    void onSessionFinished(int portTestResult);

private:
    CliStartStream::Launcher* m_Launcher;
    Session* m_Session;
    bool m_ErrorReported;
};

}
