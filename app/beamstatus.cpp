#include "beamstatus.h"

#include <atomic>
#include <cstdio>

static void emitLine(const QString& line)
{
    // Beam's reader is line-oriented, so a status line must stay one line
    QString sanitized = line;
    sanitized.replace('\r', ' ');
    sanitized.replace('\n', ' ');

    QByteArray utf8 = sanitized.toUtf8();
    fprintf(stdout, "beam: %s\n", utf8.constData());

    // stdout is a fully buffered pipe when Beam is the parent; a status line
    // sitting in the buffer is as good as never sent
    fflush(stdout);
}

namespace BeamStatus {

void connecting()
{
    emitLine("connecting");
}

void firstFrame()
{
    // Called for every rendered frame from the render thread
    static std::atomic_bool emitted(false);
    if (emitted.exchange(true)) {
        return;
    }
    emitLine("first-frame");
}

void error(ErrorCode code, const QString& text)
{
    emitLine(QString("error %1 %2").arg(code).arg(text));
}

void ended(const QString& reason)
{
    emitLine(QString("ended %1").arg(reason));
}

}
