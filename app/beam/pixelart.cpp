#include "beam/pixelart.h"

#include <QFile>
#include <QRegularExpression>

namespace PixelArt
{

std::vector<Cell> load(const char* path)
{
    std::vector<Cell> cells;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Beam: cannot read %s", path);
        return cells;
    }
    const QString svg = QString::fromUtf8(file.readAll());

    static const QRegularExpression pathRe(QStringLiteral("<path fill=\"#([0-9a-fA-F]{6})\" d=\"([^\"]+)\""));
    static const QRegularExpression rectRe(QStringLiteral("M(\\d+) (\\d+)h(\\d+)v(\\d+)h-\\d+z"));
    for (auto paths = pathRe.globalMatch(svg); paths.hasNext();) {
        const auto shape = paths.next();
        const uint rgb = shape.captured(1).toUInt(nullptr, 16);
        for (auto rects = rectRe.globalMatch(shape.captured(2)); rects.hasNext();) {
            const auto rect = rects.next();
            cells.push_back({rect.captured(1).toInt(), rect.captured(2).toInt(),
                             rect.captured(3).toInt(), rect.captured(4).toInt(),
                             Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb)});
        }
    }
    return cells;
}

}
