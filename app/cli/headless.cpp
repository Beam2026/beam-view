#include "headless.h"

#include "beamstatus.h"
#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QCoreApplication>

namespace CliHeadless
{

StreamRunner::StreamRunner(CliStartStream::Launcher* launcher, QObject* parent)
    : QObject(parent),
      m_Launcher(launcher),
      m_Session(nullptr),
      m_ErrorReported(false)
{
    connect(launcher, &CliStartStream::Launcher::sessionCreated,
            this, &StreamRunner::onSessionCreated);
    connect(launcher, &CliStartStream::Launcher::failed,
            this, &StreamRunner::onLaunchFailed);
    connect(launcher, &CliStartStream::Launcher::appQuitRequired,
            this, &StreamRunner::onAppQuitRequired);
}

void StreamRunner::run(ComputerManager* computerManager)
{
    BeamStatus::connecting();
    m_Launcher->execute(computerManager);
}

void StreamRunner::onSessionCreated(QString appName, Session* session)
{
    Q_UNUSED(appName);

    m_Session = session;

    connect(session, &Session::stageFailed,
            this, &StreamRunner::onStageFailed);
    connect(session, &Session::displayLaunchError,
            this, &StreamRunner::onDisplayLaunchError);
    connect(session, &Session::sessionFinished,
            this, &StreamRunner::onSessionFinished);
    connect(session, &Session::readyForDeletion,
            session, &Session::deleteLater);

    // There is no Qt window in headless mode. Session tolerates a null
    // window and falls back to the primary display for sizing.
    if (!session->initialize(nullptr)) {
        // initialize() already emitted displayLaunchError with the details
        if (!m_ErrorReported) {
            BeamStatus::error(BeamStatus::ErrorLaunchFailed,
                              QStringLiteral("Session initialization failed"));
        }
        BeamStatus::ended(QStringLiteral("error"));
        QCoreApplication::exit(1);
        return;
    }

    session->start();
}

void StreamRunner::onLaunchFailed(QString message)
{
    BeamStatus::error(BeamStatus::ErrorLaunchFailed, message);
    BeamStatus::ended(QStringLiteral("error"));
    QCoreApplication::exit(1);
}

void StreamRunner::onAppQuitRequired(QString appName)
{
    // The GUI asks for confirmation here. Beam's answer is always yes: the
    // user asked for a session, so whatever is running on the host yields.
    Q_UNUSED(appName);
    m_Launcher->quitRunningApp();
}

void StreamRunner::onStageFailed(QString stage, int errorCode, QString failingPorts)
{
    QString text = QString("Starting %1 failed with error %2").arg(stage).arg(errorCode);
    if (!failingPorts.isEmpty()) {
        text += QString(" (check firewall rules for port(s): %1)").arg(failingPorts);
    }

    m_ErrorReported = true;
    BeamStatus::error(BeamStatus::ErrorStageFailed, text);
}

void StreamRunner::onDisplayLaunchError(QString text)
{
    m_ErrorReported = true;
    BeamStatus::error(BeamStatus::ErrorSession, text);
}

void StreamRunner::onSessionFinished(int portTestResult)
{
    Q_UNUSED(portTestResult);

    BeamStatus::ended(m_ErrorReported ? QStringLiteral("error")
                                      : QStringLiteral("clean"));
    QCoreApplication::exit(m_ErrorReported ? 1 : 0);
}

}
