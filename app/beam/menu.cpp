#include "beam/menu.h"

#include "beam/control.h"
#include "beam/hud.h"
#include "beam/pixelart.h"
#include "beamstatus.h"
#include "streaming/session.h"

#include <Limelight.h>

#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {

// ------------------------------------------------------------------------------------- the look ---

// Sizes on a 1080-line screen, scaled with the window above that. Beam's Help list -- a near-black
// panel, an uppercase heading, icons beside each row, rows that light on hover -- drawn larger than
// in Beam, to be read over a game from across a desk.
constexpr double kTile = 54, kTileRadius = 11, kMarkScale = 1, kEdgeMargin = 10, kDragSlop = 5;
constexpr double kGap = 9, kListWidth = 302, kSubWidth = 206, kPad = 4.5, kRadius = 7;
constexpr double kStatsH = 52, kRowH = 36, kRowRadius = 4.5, kIcon = 16.5, kText = 14.5, kLabel = 11;
constexpr double kInset = 9;  // from a row's edge to its icon, and from its value to the edge

const QColor kSpace(2, 2, 2);                 // the button's tile: Beam's own background, its sky
const QColor kSpacePressed(18, 17, 15);       // a shade lighter, under the cursor or with the list open
const QColor kPanel(2, 2, 2, 242);             // Beam's FRAME: its background at 95%
const QColor kBorder(0x1f, 0x1d, 0x1a);
const QColor kForeground(0xed, 0xe9, 0xe2);
const QColor kMuted(0x8f, 0x89, 0x7f);
const QColor kFaint(0x57, 0x52, 0x4b);
const QColor kHover(255, 255, 255, 16);
const QColor kDanger(0xf0, 0x6a, 0x76);

constexpr Uint32 kRefreshMs = 1000;  // the numbers, and the button's place after anything moved it

// ----------------------------------------------------------------------------------- the rows ---

enum Item {
    ItemNone, ItemHide, ItemWindow, ItemSound, ItemCodec, ItemDecoder, ItemVsync, ItemMuteBg,
    ItemMouseMode, ItemLocks, ItemDisconnect,
    // In the smaller list beside it:
    ItemChoice,
};

enum Sub { SubNone, SubCodec, SubDecoder, SubVsync, SubLocks };

struct Row {
    Item item;
    int choice;          // for ItemChoice: which one
    const char* icon;    // a file in res/menu, or nullptr
    QString label;
    QString value;       // shown at the right, muted
    bool opensSub;       // a chevron at the right
    bool enabled;
    bool on;             // a choice in use, or a lock that is on
    bool danger;
    QRect rect;          // in the renderer's pixels, from the screen's top left
};

enum Edge { EdgeTop, EdgeLeft, EdgeRight };

// --------------------------------------------------------------------------------- the state ---

Session* s_Session = nullptr;
SDL_TimerID s_Timer = 0;
Edge s_Edge = EdgeTop;
double s_Along = 0.5;            // where along its edge, 0 to 1
bool s_Hidden = false;           // "Hide button", until the hotkey
bool s_Open = false;
Sub s_Sub = SubNone;
bool s_SavedCapture = false;     // the input as it was when the list opened
bool s_Pressing = false, s_Dragging = false;
QPoint s_PressAt;
QPoint s_DragAt;                 // the button's top left while it is dragged
Item s_PressedItem = ItemNone;
QPoint s_Mouse(-1, -1);
bool s_SwallowB = false, s_SwallowEscape = false;
bool s_Shown = false;

QRect s_Button, s_List, s_SubList;
std::vector<Row> s_Rows;         // the list's rows and, when one is open, the smaller list's
double s_K = 1;                  // the scale

std::vector<PixelArt::Cell> s_Mark;
QRect s_MarkCells;               // the cells the mark draws, in its grid
QString s_Family;

int px(double v) { return int(std::lround(v * s_K)); }

QImage icon(const char* name, const QColor& colour, int size)
{
    static std::map<QString, QImage> cache;
    const QString key = QString("%1/%2/%3").arg(name).arg(colour.rgba()).arg(size);
    auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second;
    }

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QFile file(QString(":/res/menu/%1.svg").arg(name));
    if (file.open(QIODevice::ReadOnly)) {
        // Tabler's outline icons, coloured here and drawn at Beam's stroke weight.
        QByteArray svg = file.readAll();
        svg.replace("currentColor", colour.name().toUtf8());
        svg.replace("stroke-width=\"2\"", "stroke-width=\"2.25\"");
        QSvgRenderer renderer(svg);
        QPainter painter(&image);
        renderer.render(&painter);
    }
    cache[key] = image;
    return image;
}

QFont font(double size, QFont::Weight weight)
{
    QFont f(s_Family);
    f.setPixelSize(std::max(1, px(size)));
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

void windowPixels(int& w, int& h)
{
    SDL_GetWindowSizeInPixels(BeamSessionControl::window(s_Session), &w, &h);
}

// A mouse position from SDL's window units into the renderer's pixels.
QPoint toPixels(int x, int y)
{
    int pw, ph, w, h;
    SDL_GetWindowSize(BeamSessionControl::window(s_Session), &w, &h);
    windowPixels(pw, ph);
    return QPoint(w > 0 ? x * pw / w : x, h > 0 ? y * ph / h : y);
}

bool buttonVisible()
{
    if (s_Hidden) {
        return false;
    }
    // With the cursor: desktop mouse mode, input released, or the list open.
    return s_Open || BeamSessionControl::mouseModeDesktop(s_Session) || !BeamSessionControl::captureActive(s_Session);
}

QString codecInUse()
{
    const int format = BeamSessionControl::videoFormat(s_Session);
    if (format & VIDEO_FORMAT_MASK_AV1) return "AV1";
    if (format & VIDEO_FORMAT_MASK_H265) return "HEVC";
    if (format & VIDEO_FORMAT_MASK_H264) return "H.264";
    return "—";
}

QString decoderName(StreamingPreferences::VideoDecoderSelection d)
{
    return d == StreamingPreferences::VDS_FORCE_HARDWARE ? "Hardware"
         : d == StreamingPreferences::VDS_FORCE_SOFTWARE ? "Software" : "Auto";
}

int vsyncChoice()
{
    return !BeamSessionControl::vsync(s_Session) ? 0 : BeamSessionControl::framePacing(s_Session) ? 2 : 1;
}

QString vsyncName(int choice)
{
    return choice == 0 ? "Off" : choice == 1 ? "On" : "On + pacing";
}

QString onOff(bool on) { return on ? "On" : "Off"; }

// --------------------------------------------------------------------------------- the layout ---

void layout()
{
    int w, h;
    windowPixels(w, h);
    s_K = std::max(1.0, h / 1080.0);

    // The button, on its edge -- or under the cursor while it is dragged.
    const int tile = px(kTile), margin = px(kEdgeMargin);
    QPoint at;
    if (s_Dragging) {
        at = s_DragAt;
    }
    else if (s_Edge == EdgeTop) {
        at = QPoint(margin + int(s_Along * (w - tile - 2 * margin)), margin);
    }
    else {
        at = QPoint(s_Edge == EdgeLeft ? margin : w - tile - margin, margin + int(s_Along * (h - tile - 2 * margin)));
    }
    s_Button = QRect(at, QSize(tile, tile));

    s_Rows.clear();
    s_List = s_SubList = QRect();
    if (!s_Open) {
        return;
    }

    // The list's rows, top to bottom.
    const bool desktop = BeamSessionControl::mouseModeDesktop(s_Session);
    const bool mouseLock = BeamSessionControl::mouseLocked(s_Session);
    const bool keyboardLock = BeamSessionControl::keyboardLocked(s_Session);
    const QString locks = mouseLock && keyboardLock ? "Both" : mouseLock ? "Mouse" : keyboardLock ? "Keyboard" : "None";
    std::vector<Row> rows = {
        {ItemHide, 0, "eye-off", "Hide button", "", false, true, false, false, {}},
        {ItemWindow, 0, "app-window", "Window", BeamSessionControl::isFullScreen(s_Session) ? "Full screen" : "Windowed", false, true, false, false, {}},
        {ItemSound, 0, BeamSessionControl::soundOff() ? "volume-off" : "volume", "Sound", onOff(!BeamSessionControl::soundOff()), false, true, false, false, {}},
        {ItemCodec, 0, "movie", "Codec", codecInUse(), true, true, false, false, {}},
        {ItemDecoder, 0, "cpu", "Decoder", decoderName(BeamSessionControl::decoder(s_Session)), true, true, false, false, {}},
        {ItemVsync, 0, "wave-sine", "V-Sync", vsyncName(vsyncChoice()), true, true, false, false, {}},
        {ItemMuteBg, 0, "ear-off", "Mute when not in front", onOff(BeamSessionControl::muteInBackground(s_Session)), false, true, false, false, {}},
        {ItemMouseMode, 0, "mouse", "Mouse mode", desktop ? "Desktop" : "Game", false, true, false, false, {}},
        {ItemLocks, 0, "lock", "Input locks", locks, true, true, false, false, {}},
        {ItemDisconnect, 0, "power", "Disconnect", "", false, true, false, true, {}},
    };

    const int listW = px(kListWidth), pad = px(kPad), rowH = px(kRowH);
    // The rows, a gap and a rule before Disconnect, another before the numbers at the bottom.
    const int listH = pad + int(rows.size()) * rowH + pad * 2 + pad * 2 + px(kStatsH) + pad;
    const int gap = px(kGap);
    QPoint listAt;
    if (s_Edge == EdgeTop && !s_Dragging) {
        listAt = QPoint(std::clamp(s_Button.center().x() - listW / 2, margin, std::max(margin, w - listW - margin)),
                        s_Button.bottom() + 1 + gap);
    }
    else {
        const bool rightOfButton = s_Button.center().x() < w / 2;
        listAt = QPoint(rightOfButton ? s_Button.right() + 1 + gap : s_Button.left() - gap - listW,
                        std::clamp(s_Button.top(), margin, std::max(margin, h - listH - margin)));
    }
    s_List = QRect(listAt, QSize(listW, listH));

    // The rows from the top; a gap before Disconnect, under its own rule.
    int y = s_List.top() + pad;
    for (Row& row : rows) {
        if (row.item == ItemDisconnect) {
            y += pad * 2;
        }
        row.rect = QRect(s_List.left() + pad, y, listW - 2 * pad, rowH);
        y += rowH;
        s_Rows.push_back(row);
    }

    if (s_Sub == SubNone) {
        return;
    }

    // The smaller list, beside the row that opened it.
    std::vector<Row> subRows;
    const Item opener = s_Sub == SubCodec ? ItemCodec : s_Sub == SubDecoder ? ItemDecoder
                      : s_Sub == SubVsync ? ItemVsync : ItemLocks;
    if (s_Sub == SubCodec) {
        // Changing the codec restarts the stream; until Beam can do that, the choices are shown only.
        const QString inUse = codecInUse();
        int i = 0;
        for (const char* name : {"H.264", "HEVC", "AV1"}) {
            subRows.push_back({ItemChoice, i++, nullptr, name, "", false, false, inUse == name, false, {}});
        }
    }
    else if (s_Sub == SubDecoder) {
        const auto current = BeamSessionControl::decoder(s_Session);
        const StreamingPreferences::VideoDecoderSelection choices[] = {
            StreamingPreferences::VDS_AUTO, StreamingPreferences::VDS_FORCE_HARDWARE, StreamingPreferences::VDS_FORCE_SOFTWARE};
        for (int i = 0; i < 3; i++) {
            subRows.push_back({ItemChoice, i, nullptr, decoderName(choices[i]), "", false, true, current == choices[i], false, {}});
        }
    }
    else if (s_Sub == SubVsync) {
        for (int i = 0; i < 3; i++) {
            subRows.push_back({ItemChoice, i, nullptr, vsyncName(i), "", false, true, vsyncChoice() == i, false, {}});
        }
    }
    else {
        subRows.push_back({ItemChoice, 0, "mouse", "Mouse lock", "", false, true, mouseLock, false, {}});
        subRows.push_back({ItemChoice, 1, "keyboard", "Keyboard lock", "", false, true, keyboardLock, false, {}});
    }

    QRect openerRect;
    for (const Row& row : s_Rows) {
        if (row.item == opener) openerRect = row.rect;
    }
    const int subW = px(kSubWidth);
    const int subH = 2 * pad + int(subRows.size()) * rowH;
    const bool roomRight = s_List.right() + 1 + gap + subW <= w - margin;
    const int subX = roomRight ? s_List.right() + 1 + gap : s_List.left() - gap - subW;
    const int subY = std::clamp(openerRect.top() - pad, margin, std::max(margin, h - subH - margin));
    s_SubList = QRect(subX, subY, subW, subH);
    y = subY + pad;
    for (Row& row : subRows) {
        row.rect = QRect(subX + pad, y, subW - 2 * pad, rowH);
        y += rowH;
        s_Rows.push_back(row);
    }
}

// ----------------------------------------------------------------------------------- drawing ---

void drawPanel(QPainter& p, const QRect& r)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), px(kRadius), px(kRadius));
    p.fillPath(path, kPanel);
    p.setPen(QPen(kBorder, 1));
    p.drawPath(path);
}

void drawRow(QPainter& p, const Row& row, bool hovered)
{
    const QRect r = row.rect;
    if (hovered && row.enabled) {
        QPainterPath path;
        path.addRoundedRect(QRectF(r), px(kRowRadius), px(kRowRadius));
        p.fillPath(path, kHover);
    }

    // Colour: red for Disconnect; faint for a choice not available or a lock that is off.
    const bool isLock = row.item == ItemChoice && s_Sub == SubLocks;
    QColor text = row.danger ? kDanger : (!row.enabled || (isLock && !row.on)) ? kFaint : kForeground;

    int x = r.left() + px(kInset);
    if (row.icon) {
        const int size = px(kIcon);
        p.drawImage(QPoint(x, r.top() + (r.height() - size) / 2), icon(row.icon, text, size));
        x += size + px(kInset);
    }
    p.setFont(font(kText, QFont::Medium));
    p.setPen(text);
    p.drawText(QRect(x, r.top(), r.right() - x, r.height()), Qt::AlignVCenter | Qt::AlignLeft, row.label);

    // At the right: the value and a chevron, or the check of the choice in use.
    int right = r.right() - px(kInset);
    if (row.opensSub) {
        const int size = px(kIcon);
        p.drawImage(QPoint(right - size, r.top() + (r.height() - size) / 2), icon("chevron-right", kMuted, size));
        right -= size + px(6);
    }
    if (row.item == ItemChoice && !isLock && row.on) {
        const int size = px(kIcon);
        p.drawImage(QPoint(right - size, r.top() + (r.height() - size) / 2), icon("check", kForeground, size));
    }
    if (!row.value.isEmpty()) {
        p.setPen(kMuted);
        p.drawText(QRect(x, r.top(), right - x, r.height()), Qt::AlignVCenter | Qt::AlignRight, row.value);
    }
}

void drawNumbers(QPainter& p)
{
    int ping;
    double encodeMs, decodeMs;
    BeamHud::latest(ping, encodeMs, decodeMs);
    const QString values[] = {
        ping >= 0 ? QString("%1 ms").arg(ping) : "—",
        encodeMs >= 0 ? QString("%1 ms").arg(encodeMs, 0, 'f', 1) : "—",
        decodeMs >= 0 ? QString("%1 ms").arg(decodeMs, 0, 'f', 1) : "—",
    };
    const char* labels[] = {"PING", "ENCODE", "DECODE"};

    const int pad = px(kPad);
    const int top = s_List.bottom() + 1 - pad - px(kStatsH);  // the numbers sit at the bottom
    const int colW = (s_List.width() - 2 * pad) / 3;
    for (int i = 0; i < 3; i++) {
        const QRect col(s_List.left() + pad + i * colW + px(kInset), top, colW - px(kInset), px(kStatsH));
        QFont label = font(kLabel, QFont::DemiBold);
        label.setLetterSpacing(QFont::AbsoluteSpacing, px(0.8));
        p.setFont(label);
        p.setPen(kMuted);
        p.drawText(QRect(col.left(), col.top(), col.width(), col.height() / 2), Qt::AlignLeft | Qt::AlignBottom, labels[i]);
        p.setFont(font(kText + 2, QFont::Medium));
        p.setPen(kForeground);
        p.drawText(QRect(col.left(), col.top() + col.height() / 2 + px(2), col.width(), col.height() / 2), Qt::AlignLeft | Qt::AlignTop, values[i]);
    }

    // A rule above the numbers, and one above Disconnect.
    p.setPen(QPen(kBorder, 1));
    const int ruleY = top - pad;
    p.drawLine(s_List.left() + pad, ruleY, s_List.right() - pad, ruleY);
    for (const Row& row : s_Rows) {
        if (row.item == ItemDisconnect) {
            const int y = row.rect.top() - pad;
            p.drawLine(s_List.left() + pad, y, s_List.right() - pad, y);
        }
    }
}

void drawButton(QPainter& p)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(s_Button), px(kTileRadius), px(kTileRadius));
    p.fillPath(path, s_Open || s_Button.contains(s_Mouse) ? kSpacePressed : kSpace);

    p.setPen(QPen(kBorder, 1));
    p.drawPath(path);

    // Beam's mark, the cells it draws, at a whole multiple of its grid -- once on a 1080-line screen,
    // as Beam's own top bar draws it -- so every pixel is the same size and none blurs.
    const QRect cells = s_MarkCells;
    const int scale = std::max(1, int(std::lround(kMarkScale * s_K)));
    auto edge = [scale](int cell) { return int(std::lround(cell * scale)); };
    const int x0 = s_Button.left() + (s_Button.width() - (edge(cells.right() + 1) - edge(cells.left()))) / 2 - edge(cells.left());
    const int y0 = s_Button.top() + (s_Button.height() - (edge(cells.bottom() + 1) - edge(cells.top()))) / 2 - edge(cells.top());
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const PixelArt::Cell& c : s_Mark) {
        p.fillRect(x0 + edge(c.x), y0 + edge(c.y), edge(c.x + c.w) - edge(c.x), edge(c.y + c.h) - edge(c.y), QColor(c.r, c.g, c.b));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
}

void redraw()
{
    if (s_Session == nullptr) {
        return;
    }
    Overlay::OverlayManager& overlays = s_Session->getOverlayManager();
    const bool visible = buttonVisible();
    if (!visible && !s_Open) {
        if (s_Shown) {
            overlays.setOverlaySurface(Overlay::OverlayBeamMenu, nullptr);
            s_Shown = false;
        }
        return;
    }

    layout();
    QRect box = s_Button;
    if (s_Open) {
        box = box.united(s_List);
        if (!s_SubList.isNull()) box = box.united(s_SubList);
    }

    QImage image(box.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.translate(-box.topLeft());
        drawButton(p);
        if (s_Open) {
            drawPanel(p, s_List);
            drawNumbers(p);
            if (!s_SubList.isNull()) {
                drawPanel(p, s_SubList);
            }
            for (const Row& row : s_Rows) {
                drawRow(p, row, row.rect.contains(s_Mouse));
            }
        }
    }

    // The renderers take straight alpha, as SDL_ttf's surfaces have it.
    const QImage straight = image.convertToFormat(QImage::Format_ARGB32);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, straight.width(), straight.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return;
    }
    for (int y = 0; y < straight.height(); y++) {
        memcpy((Uint8*)surface->pixels + y * surface->pitch, straight.constScanLine(y), straight.width() * 4);
    }
    overlays.setOverlayPosition(Overlay::OverlayBeamMenu, box.left(), box.top());
    overlays.setOverlaySurface(Overlay::OverlayBeamMenu, surface);
    s_Shown = true;
}

// ----------------------------------------------------------------------------------- actions ---

void openList()
{
    if (s_Open) {
        return;
    }
    s_Open = true;
    s_Hidden = false;
    s_Sub = SubNone;
    // The mouse and keyboard are this PC's while the list is open; given back as they were.
    s_SavedCapture = BeamSessionControl::captureActive(s_Session);
    BeamSessionControl::setCapture(s_Session, false);
    BeamSessionControl::raiseKeys(s_Session);
    BeamStatus::state("menu", "open");
}

void closeList()
{
    if (!s_Open) {
        return;
    }
    s_Open = false;
    s_Sub = SubNone;
    if (s_SavedCapture) {
        BeamSessionControl::setCapture(s_Session, true);
    }
    BeamStatus::state("menu", "closed");
}

void toggleSub(Sub sub)
{
    s_Sub = s_Sub == sub ? SubNone : sub;
}

void activate(const Row& row)
{
    Session* s = s_Session;
    switch (row.item) {
    case ItemHide:
        s_Hidden = true;
        closeList();
        BeamStatus::state("button", "hidden");
        break;
    case ItemWindow:      BeamSessionControl::setFullScreen(s, !BeamSessionControl::isFullScreen(s)); break;
    case ItemSound:       BeamSessionControl::setSound(BeamSessionControl::soundOff()); break;
    case ItemCodec:       toggleSub(SubCodec); break;
    case ItemDecoder:     toggleSub(SubDecoder); break;
    case ItemVsync:       toggleSub(SubVsync); break;
    case ItemMuteBg:      BeamSessionControl::setMuteInBackground(s, !BeamSessionControl::muteInBackground(s)); break;
    case ItemMouseMode:   BeamSessionControl::setMouseMode(s, !BeamSessionControl::mouseModeDesktop(s)); break;
    case ItemLocks:       toggleSub(SubLocks); break;
    case ItemDisconnect:
        s_SavedCapture = false;
        closeList();
        BeamSessionControl::disconnect();
        break;
    case ItemChoice:
        if (s_Sub == SubDecoder) {
            const StreamingPreferences::VideoDecoderSelection choices[] = {
                StreamingPreferences::VDS_AUTO, StreamingPreferences::VDS_FORCE_HARDWARE, StreamingPreferences::VDS_FORCE_SOFTWARE};
            BeamSessionControl::setDecoder(s, choices[row.choice]);
        }
        else if (s_Sub == SubVsync) {
            BeamSessionControl::setVsync(s, row.choice > 0, row.choice == 2);
        }
        else if (s_Sub == SubLocks) {
            if (row.choice == 0) BeamSessionControl::setMouseLock(s, !BeamSessionControl::mouseLocked(s));
            else BeamSessionControl::setKeyboardLock(s, !BeamSessionControl::keyboardLocked(s));
        }
        break;
    case ItemNone:
        break;
    }
}

const Row* rowAt(const QPoint& at)
{
    for (const Row& row : s_Rows) {
        if (row.rect.contains(at)) return &row;
    }
    return nullptr;
}

// Where a dragged button settles: the nearest of the top, left and right edges.
void snap()
{
    int w, h;
    windowPixels(w, h);
    const int tile = px(kTile), margin = px(kEdgeMargin);
    const QPoint c = s_Button.center();
    const int toTop = c.y(), toLeft = c.x(), toRight = w - c.x();
    if (toTop <= toLeft && toTop <= toRight) {
        s_Edge = EdgeTop;
        s_Along = std::clamp(double(s_Button.left() - margin) / std::max(1, w - tile - 2 * margin), 0.0, 1.0);
    }
    else {
        s_Edge = toLeft < toRight ? EdgeLeft : EdgeRight;
        s_Along = std::clamp(double(s_Button.top() - margin) / std::max(1, h - tile - 2 * margin), 0.0, 1.0);
    }
    BeamStatus::state("button", s_Edge == EdgeTop ? "top" : s_Edge == EdgeLeft ? "left" : "right");
}

Uint32 postRefresh(Uint32 interval, void*)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = BeamMenu::kRedrawCode;
    SDL_PushEvent(&event);
    return interval;
}

bool isHotkey(const SDL_KeyboardEvent& key)
{
    const Uint16 mods = key.keysym.mod;
    return key.keysym.sym == SDLK_b && (mods & KMOD_CTRL) && (mods & KMOD_ALT) && (mods & KMOD_SHIFT);
}

} // namespace

namespace BeamMenu
{

void begin(Session* session)
{
    s_Session = session;
    if (s_Family.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(":/res/menu/DMSans-Regular.ttf");
        QFontDatabase::addApplicationFont(":/res/menu/DMSans-Medium.ttf");
        QFontDatabase::addApplicationFont(":/res/menu/DMSans-SemiBold.ttf");
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        s_Family = families.isEmpty() ? QString("Segoe UI") : families.first();
        s_Mark = PixelArt::load(":/res/menu/beam-mark.svg");
        for (const PixelArt::Cell& c : s_Mark) {
            s_MarkCells = s_MarkCells.united(QRect(c.x, c.y, c.w, c.h));
        }
    }
    if (s_Timer == 0) {
        s_Timer = SDL_AddTimer(kRefreshMs, postRefresh, nullptr);
    }
    // The cursor from the first moment, so the button can be reached.
    BeamSessionControl::showCursor(session);
    redraw();
}

void end()
{
    if (s_Timer != 0) {
        SDL_RemoveTimer(s_Timer);
        s_Timer = 0;
    }
    s_Open = false;
    s_Session = nullptr;
}

bool handleEvent(Session* session, const SDL_Event& event)
{
    if (s_Session == nullptr || session != s_Session) {
        return false;
    }

    switch (event.type) {
    case SDL_USEREVENT:
        if (event.user.code != kRedrawCode) {
            return false;
        }
        redraw();
        return true;

    case SDL_KEYDOWN:
        if (isHotkey(event.key)) {
            if (!event.key.repeat) {
                if (s_Open) closeList(); else openList();
                redraw();
            }
            s_SwallowB = true;
            return true;
        }
        if (s_Open && event.key.keysym.sym == SDLK_ESCAPE) {
            closeList();
            redraw();
            s_SwallowEscape = true;
            return true;
        }
        return false;

    case SDL_KEYUP:
        if (s_SwallowB && event.key.keysym.sym == SDLK_b) {
            s_SwallowB = false;
            return true;
        }
        if (s_SwallowEscape && event.key.keysym.sym == SDLK_ESCAPE) {
            s_SwallowEscape = false;
            return true;
        }
        return false;

    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && s_Open) {
            closeList();
            redraw();
        }
        return false;

    case SDL_MOUSEMOTION: {
        const QPoint at = toPixels(event.motion.x, event.motion.y);
        const QPoint was = s_Mouse;
        s_Mouse = at;
        if (s_Pressing && !s_Dragging && !s_Open && s_PressedItem == ItemNone &&
                (at - s_PressAt).manhattanLength() > px(kDragSlop)) {
            s_Dragging = true;
        }
        if (s_Dragging) {
            int w, h;
            windowPixels(w, h);
            const int tile = s_Button.width();
            s_DragAt = QPoint(std::clamp(at.x() - tile / 2, 0, std::max(0, w - tile)),
                              std::clamp(at.y() - tile / 2, 0, std::max(0, h - tile)));
            redraw();
            return true;
        }
        if (s_Open) {
            // Repainted only when what is under the cursor changes.
            const Row* now = rowAt(at);
            const Row* before = rowAt(was);
            if (now != before || s_Button.contains(at) != s_Button.contains(was)) {
                redraw();
            }
            return true;
        }
        if (buttonVisible() && s_Button.contains(at) != s_Button.contains(was)) {
            redraw();
        }
        return s_Pressing;
    }

    case SDL_MOUSEBUTTONDOWN: {
        const QPoint at = toPixels(event.button.x, event.button.y);
        s_Mouse = at;
        const bool onButton = buttonVisible() && s_Button.contains(at);
        if (!s_Open && !onButton) {
            return false;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            s_Pressing = true;
            s_PressAt = at;
            const Row* row = onButton ? nullptr : rowAt(at);
            s_PressedItem = onButton ? ItemNone : row ? row->item : ItemNone;
            if (!onButton && row == nullptr && !s_List.contains(at) && !s_SubList.contains(at)) {
                // A press outside the list closes it, and goes no further.
                s_Pressing = false;
                closeList();
                redraw();
            }
        }
        return true;
    }

    case SDL_MOUSEBUTTONUP: {
        if (!s_Pressing) {
            return s_Open;
        }
        const QPoint at = toPixels(event.button.x, event.button.y);
        s_Pressing = false;
        if (event.button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (s_Dragging) {
            // From where it was let go: the last redraw put the button there.
            snap();
            s_Dragging = false;
        }
        else if (s_Button.contains(at) && s_Button.contains(s_PressAt)) {
            if (s_Open) closeList(); else openList();
        }
        else if (const Row* row = rowAt(at)) {
            if (row->enabled && row->item == s_PressedItem) {
                activate(*row);
            }
        }
        redraw();
        return true;
    }

    case SDL_MOUSEWHEEL:
        return s_Open;

    default:
        return false;
    }
}

}
