/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2012-2013 Razor team
 *            2014 LXQt team
 * Authors:
 *   Kuzma Shapran <kuzma.shapran@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "lxqtworldclock.h"

#include <LXQt/Globals>

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#include <QCalendarWidget>
#include <QDate>
#include <QDialog>
#include <QEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScopedArrayPointer>
#include <QSizePolicy>
#include <QTimer>
#include <QWheelEvent>
#include <QToolTip>


LXQtWorldClock::LXQtWorldClock(const ILXQtPanelPluginStartupInfo &startupInfo):
    QObject(),
    ILXQtPanelPlugin(startupInfo),
    mPopup(nullptr),
    mTimer(new QTimer(this)),
    mUpdateInterval(1),
    mTimeZoneWheel(true),
    mAutoRotate(true),
    mShowWeekNumber(true),
    mShowTooltip(false),
    mPopupContent(nullptr),
    mCalendarTimeLabel(nullptr),
    mAnalogClock(nullptr)
{
    mMainWidget = new QWidget();
    mMainWidget->installEventFilter(this);
    mContent = new ActiveLabel();
    mRotatedWidget = new LXQt::RotatedWidget(*mContent, mMainWidget);

    QVBoxLayout *borderLayout = new QVBoxLayout(mMainWidget);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    borderLayout->setSpacing(0);
    borderLayout->addWidget(mRotatedWidget, 0, Qt::AlignCenter);

    mContent->setObjectName(QLatin1String("WorldClockContent"));

    mContent->setAlignment(Qt::AlignCenter);

    settingsChanged();

    mTimer->setTimerType(Qt::PreciseTimer);
    connect(mTimer, &QTimer::timeout, this, &LXQtWorldClock::timeout);

    connect(mContent, &ActiveLabel::wheelScrolled, this, &LXQtWorldClock::wheelScrolled);
}

LXQtWorldClock::~LXQtWorldClock()
{
    delete mMainWidget;
}

void LXQtWorldClock::timeout()
{
    if (QTime::currentTime().msec() > 500)
        restartTimer();
    updateTimeText();
}

void LXQtWorldClock::updateTimeText()
{
    QDateTime now = QDateTime::currentDateTime();
    QString timeZoneName = mActiveTimeZone;
    if (timeZoneName == QLatin1String("local"))
        timeZoneName = QString::fromLatin1(QTimeZone::systemTimeZoneId());
    QTimeZone timeZone(timeZoneName.toLatin1());
    QDateTime tzNow = now.toTimeZone(timeZone);

    bool isUpToDate(true);
    if (!mShownTime.isValid()) // first time or forced update
    {
        isUpToDate = false;
        if (mUpdateInterval < 60000)
            mShownTime = tzNow.addMSecs(-tzNow.time().msec()); // s
        else if (mUpdateInterval < 3600000)
            mShownTime = tzNow.addSecs(-tzNow.time().second()); // m
        else
            mShownTime = tzNow.addSecs(-tzNow.time().minute() * 60 - tzNow.time().second()); // h
    }
    else
    {
        qint64 diff = mShownTime.secsTo(tzNow);
        if (mUpdateInterval < 60000)
        {
            if (diff < 0 || diff >= 1)
            {
                isUpToDate = false;
                mShownTime = tzNow.addMSecs(-tzNow.time().msec());
            }
        }
        else if (mUpdateInterval < 3600000)
        {
            if (diff < 0 || diff >= 60)
            {
                isUpToDate = false;
                mShownTime = tzNow.addSecs(-tzNow.time().second());
            }
        }
        else if (diff < 0 || diff >= 3600)
        {
            isUpToDate = false;
            mShownTime = tzNow.addSecs(-tzNow.time().minute() * 60 - tzNow.time().second());
        }
    }
    if (!isUpToDate)
    {
        const QSize old_size = mContent->sizeHint();
        mContent->setText(QLocale::system().toString(tzNow, preformat(mFormat, timeZone, tzNow)));
        if (old_size != mContent->sizeHint())
            mRotatedWidget->adjustContentSize();
        mRotatedWidget->update();
        updatePopupContent();
    }
    updateCalendarTime();
}

void LXQtWorldClock::setTimeText()
{
    mShownTime = QDateTime(); // force an update
    updateTimeText();
}

void LXQtWorldClock::restartTimer()
{
    mTimer->stop();
    // check the time every second even if the clock doesn't show seconds
    // because otherwise, the shown time might be vey wrong after resume
    mTimer->setInterval(1000);

    int delay = static_cast<int>(1000 - (static_cast<long long>(QTime::currentTime().msecsSinceStartOfDay()) % 1000));
    QTimer::singleShot(delay, Qt::PreciseTimer, this, &LXQtWorldClock::updateTimeText);
    QTimer::singleShot(delay, Qt::PreciseTimer, mTimer, SLOT(start()));
}

void LXQtWorldClock::settingsChanged()
{
    static const QRegularExpression regexp(QLatin1String("'[^']*'"));

    PluginSettings *_settings = settings();

    QString oldFormat = mFormat;

    mTimeZones.clear();

    const QList<QMap<QString, QVariant> > array = _settings->readArray(QLatin1String("timeZones"));
    for (const auto &map : array)
    {
        QString timeZoneName = map.value(QLatin1String("timeZone"), QString()).toString();
        mTimeZones.append(timeZoneName);
        mTimeZoneCustomNames[timeZoneName] = map.value(QLatin1String("customName"),
                                                       QString()).toString();
    }

    if (mTimeZones.isEmpty())
        mTimeZones.append(QLatin1String("local"));

    mDefaultTimeZone = _settings->value(QLatin1String("defaultTimeZone"), QString()).toString();
    if (mDefaultTimeZone.isEmpty())
        mDefaultTimeZone = mTimeZones[0];
    mActiveTimeZone = mDefaultTimeZone;


    bool longTimeFormatSelected = false;

    QString formatType = _settings->value(QLatin1String("formatType"), QString()).toString();
    QString dateFormatType = _settings->value(QLatin1String("dateFormatType"), QString()).toString();
    bool advancedManual = _settings->value(QLatin1String("useAdvancedManualFormat"), false).toBool();

    // backward compatibility
    if (formatType == QLatin1String("custom"))
    {
        formatType = QLatin1String("short-timeonly");
        dateFormatType = QLatin1String("short");
        advancedManual = true;
    }
    else if (formatType == QLatin1String("short"))
    {
        formatType = QLatin1String("short-timeonly");
        dateFormatType = QLatin1String("short");
        advancedManual = false;
    }
    else if ((formatType == QLatin1String("full")) ||
             (formatType == QLatin1String("long")) ||
             (formatType == QLatin1String("medium")))
    {
        formatType = QLatin1String("long-timeonly");
        dateFormatType = QLatin1String("long");
        advancedManual = false;
    }

    if (formatType == QLatin1String("long-timeonly"))
        longTimeFormatSelected = true;

    bool timeShowSeconds = _settings->value(QLatin1String("timeShowSeconds"), false).toBool();
    bool timePadHour = _settings->value(QLatin1String("timePadHour"), false).toBool();
    bool timeAMPM = _settings->value(QLatin1String("timeAMPM"), false).toBool();
    mShowTooltip = _settings->value(QLatin1String("showTooltip"), false).toBool();
    // timezone
    bool showTimezone = _settings->value(QLatin1String("showTimezone"), false).toBool() && !longTimeFormatSelected;

    QString timezonePosition = _settings->value(QLatin1String("timezonePosition"), QString()).toString();
    QString timezoneFormatType = _settings->value(QLatin1String("timezoneFormatType"), QString()).toString();

    mTimeZoneWheel = _settings->value(QLatin1String("timeZoneWheel"), true).toBool();

    // date
    bool showDate = _settings->value(QLatin1String("showDate"), false).toBool();

    QString datePosition = _settings->value(QLatin1String("datePosition"), QString()).toString();

    bool dateShowYear = _settings->value(QLatin1String("dateShowYear"), false).toBool();
    bool dateShowDoW = _settings->value(QLatin1String("dateShowDoW"), false).toBool();
    bool datePadDay = _settings->value(QLatin1String("datePadDay"), false).toBool();
    bool dateLongNames = _settings->value(QLatin1String("dateLongNames"), false).toBool();

    // advanced
    QString customFormat = _settings->value(QLatin1String("customFormat"), tr("'<b>'HH:mm:ss'</b><br/><font size=\"-2\">'ddd, d MMM yyyy'<br/>'TT'</font>'")).toString();

    if (advancedManual)
        mFormat = customFormat;
    else
    {
        if (formatType == QLatin1String("short-timeonly"))
            mFormat = QLocale{}.timeFormat(QLocale::ShortFormat);
        else if (formatType == QLatin1String("long-timeonly"))
            mFormat = QLocale{}.timeFormat(QLocale::LongFormat);
        else // if (formatType == QLatin1String("custom-timeonly"))
            mFormat = QString(QLatin1String("%1:mm%2%3")).arg(timePadHour ? QLatin1String("hh") : QLatin1String("h")).arg(timeShowSeconds ? QLatin1String(":ss") : QLatin1String("")).arg(timeAMPM ? QLatin1String(" A") : QLatin1String(""));

        if (showTimezone)
        {
            QString timezonePortion;
            if (timezoneFormatType == QLatin1String("short"))
                timezonePortion = QLatin1String("TTTT");
            else if (timezoneFormatType == QLatin1String("long"))
                timezonePortion = QLatin1String("TTTTT");
            else if (timezoneFormatType == QLatin1String("offset"))
                timezonePortion = QLatin1String("T");
            else if (timezoneFormatType == QLatin1String("abbreviation"))
                timezonePortion = QLatin1String("TTT");
            else if (timezoneFormatType == QLatin1String("iana"))
                timezonePortion = QLatin1String("TT");
            else // if (timezoneFormatType == QLatin1String("custom"))
                timezonePortion = QLatin1String("TTTTTT");

            if (timezonePosition == QLatin1String("below"))
                mFormat = mFormat + QLatin1String("'<br/>'") + timezonePortion;
            else if (timezonePosition == QLatin1String("above"))
                mFormat = timezonePortion + QLatin1String("'<br/>'") + mFormat;
            else if (timezonePosition == QLatin1String("before"))
                mFormat = timezonePortion + QLatin1String(" ") + mFormat;
            else // if (timezonePosition == QLatin1String("after"))
                mFormat = mFormat + QLatin1String(" ") + timezonePortion;
        }

        if (showDate)
        {
            QString datePortion;
            if (dateFormatType == QLatin1String("short"))
                datePortion = QLocale{}.dateFormat(QLocale::ShortFormat);
            else if (dateFormatType == QLatin1String("long"))
                datePortion = QLocale{}.dateFormat(QLocale::LongFormat);
            else if (dateFormatType == QLatin1String("iso"))
                datePortion = QLatin1String("yyyy-MM-dd");
            else // if (dateFormatType == QLatin1String("custom"))
            {
                QString datePortionOrder;
                QString dateLocale = QLocale{}.dateFormat(QLocale::ShortFormat).toLower();
                int yearIndex = dateLocale.indexOf(QLatin1String("y"));
                int monthIndex = dateLocale.indexOf(QLatin1String("m"));
                int dayIndex = dateLocale.indexOf(QLatin1String("d"));
                if (yearIndex < dayIndex)
                // Big-endian (year, month, day) (yyyy MMMM dd, dddd) -> in some Asia countires like China or Japan
                    datePortionOrder = QLatin1String("%1%2%3 %4%5%6");
                else if (monthIndex < dayIndex)
                // Middle-endian (month, day, year) (dddd, MMMM dd yyyy) -> USA
                    datePortionOrder = QLatin1String("%6%5%3 %4%2%1");
                else
                // Little-endian (day, month, year) (dddd, dd MMMM yyyy) -> most of Europe
                    datePortionOrder = QLatin1String("%6%5%4 %3%2%1");
                datePortion = datePortionOrder.arg(dateShowYear ? QLatin1String("yyyy") : QLatin1String("")).arg(dateShowYear ? QLatin1String(" ") : QLatin1String("")).arg(dateLongNames ? QLatin1String("MMMM") : QLatin1String("MMM")).arg(datePadDay ? QLatin1String("dd") : QLatin1String("d")).arg(dateShowDoW ? QLatin1String(", ") : QLatin1String("")).arg(dateShowDoW ? (dateLongNames ? QLatin1String("dddd") : QLatin1String("ddd")) : QLatin1String(""));
            }

            if (datePosition == QLatin1String("below"))
                mFormat = mFormat + QLatin1String("'<br/>'") + datePortion;
            else if (datePosition == QLatin1String("above"))
                mFormat = datePortion + QLatin1String("'<br/>'") + mFormat;
            else if (datePosition == QLatin1String("before"))
                mFormat = datePortion + QLatin1String(" ") + mFormat;
            else // if (datePosition == QLatin1String("after"))
                mFormat = mFormat + QLatin1String(" ") + datePortion;
        }
    }


    if ((oldFormat != mFormat))
    {
        int update_interval;
        QString format = mFormat;
        format.replace(regexp, QString());
        //don't support updating on millisecond basis -> big performance hit
        if (format.contains(QLatin1String("s")))
            update_interval = 1000;
        else if (format.contains(QLatin1String("m")))
            update_interval = 60000;
        else
            update_interval = 3600000;

        if (update_interval != mUpdateInterval)
        {
            mUpdateInterval = update_interval;
            restartTimer();
        }
    }

    bool autoRotate = settings()->value(QLatin1String("autoRotate"), true).toBool();
    if (autoRotate != mAutoRotate)
    {
        mAutoRotate = autoRotate;
        realign();
    }

    bool showWeekNumber = settings()->value(QL1S("showWeekNumber"), true).toBool();
    if (showWeekNumber != mShowWeekNumber)
    {
        mShowWeekNumber = showWeekNumber;
    }

    if (mPopup)
    {
        updatePopupContent();
        mPopup->adjustSize();
        mPopup->setGeometry(calculatePopupWindowPos(mPopup->size()));
    }

    setTimeText();
}

QDialog *LXQtWorldClock::configureDialog()
{
    return new LXQtWorldClockConfiguration(settings());
}

void LXQtWorldClock::wheelScrolled(int delta)
{
    if (mTimeZoneWheel && mTimeZones.count() > 1)
    {
        mActiveTimeZone = mTimeZones[(mTimeZones.indexOf(mActiveTimeZone) + ((delta > 0) ? -1 : 1) + mTimeZones.size()) % mTimeZones.size()];
        setTimeText();
    }
}

void LXQtWorldClock::activated(ActivationReason reason)
{
    switch (reason)
    {
    case ILXQtPanelPlugin::Trigger:
    case ILXQtPanelPlugin::MiddleClick:
        break;

    default:
        return;
    }

    if (!mPopup)
    {
        mPopup = new LXQtWorldClockPopup(mContent);
        connect(mPopup, &QObject::destroyed, this, [this]() {
            mPopupContent = nullptr;
            mCalendarTimeLabel = nullptr;
            mAnalogClock = nullptr;
            mPopup = nullptr;
        });

        if (reason == ILXQtPanelPlugin::Trigger)
        {
            mPopup->setObjectName(QLatin1String("WorldClockCalendar"));

            LunarCalendarWidget *calendarWidget = new LunarCalendarWidget(mPopup);
            if (!mShowWeekNumber)
                calendarWidget->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
            mPopup->layout()->addWidget(calendarWidget);

            // Row below the calendar: analog clock on the left, text label on the right
            QWidget *bottomRow = new QWidget(mPopup);
            QHBoxLayout *bottomLayout = new QHBoxLayout(bottomRow);
            bottomLayout->setContentsMargins(16, 8, 16, 12);
            bottomLayout->setSpacing(0);

            mAnalogClock = new AnalogClock(bottomRow);
            mAnalogClock->setFixedSize(72, 72);
            bottomLayout->addWidget(mAnalogClock);

            mCalendarTimeLabel = new QLabel(bottomRow);
            mCalendarTimeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            bottomLayout->addWidget(mCalendarTimeLabel, 1);

            QPushButton *todayButton = new QPushButton(tr("Today"), bottomRow);
            bottomLayout->addWidget(todayButton);

            mPopup->layout()->addWidget(bottomRow);

            QString timeZoneName = mActiveTimeZone;
            if (timeZoneName == QLatin1String("local"))
                timeZoneName = QString::fromLatin1(QTimeZone::systemTimeZoneId());

            QTimeZone timeZone(timeZoneName.toLatin1());
            calendarWidget->setFirstDayOfWeek(Qt::Monday);
            calendarWidget->setSelectedDate(QDateTime::currentDateTime().toTimeZone(timeZone).date());

            connect(todayButton, &QPushButton::clicked, this, [this, calendarWidget, timeZone]() {
                calendarWidget->reload(timeZone);
                updateCalendarTime();
            });

            updateCalendarTime();
        }
        else
        {
            mPopup->setObjectName(QLatin1String("WorldClockPopup"));

            mPopupContent = new QLabel(mPopup);
            mPopup->layout()->addWidget(mPopupContent);
            mPopupContent->setAlignment(mContent->alignment());

            updatePopupContent();
        }

        mPopup->adjustSize();
        mPopup->setGeometry(calculatePopupWindowPos(mPopup->size()));

        willShowWindow(mPopup);
        mPopup->show();
    }
    else
    {
        delete mPopup;
    }
}

QString LXQtWorldClock::formatDateTime(const QDateTime &datetime, const QString &timeZoneName)
{
    QTimeZone timeZone(timeZoneName.toLatin1());
    QDateTime tzNow = datetime.toTimeZone(timeZone);
    return QLocale::system().toString(tzNow, preformat(mFormat, timeZone, tzNow));
}

void LXQtWorldClock::updateCalendarTime()
{
    if (mCalendarTimeLabel || mAnalogClock)
    {
        QDateTime now = QDateTime::currentDateTime();
        QString timeZoneName = mActiveTimeZone;
        if (timeZoneName == QLatin1String("local"))
            timeZoneName = QString::fromLatin1(QTimeZone::systemTimeZoneId());
        QTimeZone timeZone(timeZoneName.toLatin1());
        QDateTime tzNow = now.toTimeZone(timeZone);

        if (mCalendarTimeLabel)
            mCalendarTimeLabel->setText(QLocale::system().toString(tzNow, QStringLiteral("ddd, MMM d yyyy hh:mm:ss")));

        if (mAnalogClock)
            mAnalogClock->setTime(tzNow.time());
    }
}

void LXQtWorldClock::updatePopupContent()
{
    if (mPopupContent)
    {
        QDateTime now = QDateTime::currentDateTime();
        QStringList allTimeZones;
        bool hasTimeZone = formatHasTimeZone(mFormat);

        for (QString timeZoneName : std::as_const(mTimeZones))
        {
            if (timeZoneName == QLatin1String("local"))
                timeZoneName = QString::fromLatin1(QTimeZone::systemTimeZoneId());

            QString formatted = formatDateTime(now, timeZoneName);

            if (!hasTimeZone)
                formatted += QLatin1String("<br/>") + QString::fromLatin1(QTimeZone(timeZoneName.toLatin1()).id());

            allTimeZones.append(formatted);
        }

        mPopupContent->setText(allTimeZones.join(QLatin1String("<hr/>")));
    }
}

bool LXQtWorldClock::formatHasTimeZone(QString format)
{
    static const QRegularExpression regexp(QLatin1String("'[^']*'"));
    format.replace(regexp, QString());
    return format.contains(QLatin1Char('t'), Qt::CaseInsensitive);
}

QString LXQtWorldClock::preformat(const QString &format, const QTimeZone &timeZone, const QDateTime &dateTime)
{
    QString result = format;
    int from = 0;
    for (;;)
    {
        int apos = result.indexOf(QLatin1Char('\''), from);
        int tz = result.indexOf(QLatin1Char('T'), from);
        if ((apos != -1) && (tz != -1))
        {
            if (apos > tz)
                apos = -1;
            else
                tz = -1;
        }
        if (apos != -1)
        {
            from = apos + 1;
            apos = result.indexOf(QLatin1Char('\''), from);
            if (apos == -1) // misformat
                break;
            from = apos + 1;
        }
        else if (tz != -1)
        {
            int length = 1;
            for (; result[tz + length] == QLatin1Char('T'); ++length);
            if (length > 6)
                length = 6;
            QString replacement;
            switch (length)
            {
            case 1:
                replacement = timeZone.displayName(dateTime, QTimeZone::OffsetName);
                if (replacement.startsWith(QLatin1String("UTC")))
                    replacement = replacement.mid(3);
                break;

            case 2:
                replacement = QString::fromLatin1(timeZone.id());
                break;

            case 3:
                replacement = timeZone.abbreviation(dateTime);
                break;

            case 4:
                replacement = timeZone.displayName(dateTime, QTimeZone::ShortName);
                break;

            case 5:
                replacement = timeZone.displayName(dateTime, QTimeZone::LongName);
                break;

            case 6:
                replacement = mTimeZoneCustomNames[QString::fromLatin1(timeZone.id())];
            }

            if ((tz > 0) && (result[tz - 1] == QLatin1Char('\'')))
            {
                --tz;
                ++length;
            }
            else
                replacement.prepend(QLatin1Char('\''));

            if (result[tz + length] == QLatin1Char('\''))
                ++length;
            else
                replacement.append(QLatin1Char('\''));

            result.replace(tz, length, replacement);
            from = tz + replacement.length();
        }
        else
            break;
    }
    return result;
}

void LXQtWorldClock::realign()
{
    if (mAutoRotate)
        switch (panel()->position())
        {
        case ILXQtPanel::PositionTop:
        case ILXQtPanel::PositionBottom:
            mRotatedWidget->setOrigin(Qt::TopLeftCorner);
            break;

        case ILXQtPanel::PositionLeft:
            mRotatedWidget->setOrigin(Qt::BottomLeftCorner);
            break;

        case ILXQtPanel::PositionRight:
            mRotatedWidget->setOrigin(Qt::TopRightCorner);
            break;
        }
    else
        mRotatedWidget->setOrigin(Qt::TopLeftCorner);
    if (mContent->size() != mContent->sizeHint())
        mRotatedWidget->adjustContentSize();
}

ActiveLabel::ActiveLabel(QWidget *parent) :
    QLabel(parent)
{
}

void ActiveLabel::wheelEvent(QWheelEvent *event)
{
    QPoint angleDelta = event->angleDelta();
    Qt::Orientation orient = (qAbs(angleDelta.x()) > qAbs(angleDelta.y()) ? Qt::Horizontal : Qt::Vertical);
    int delta = (orient == Qt::Horizontal ? angleDelta.x() : angleDelta.y());

    emit wheelScrolled(delta);

    QLabel::wheelEvent(event);
}

void ActiveLabel::mouseReleaseEvent(QMouseEvent* event)
{
    switch (event->button())
    {
    case Qt::LeftButton:
        emit leftMouseButtonClicked();
        break;

    case Qt::MiddleButton:
        emit middleMouseButtonClicked();
        break;

    default:;
    }

    QLabel::mouseReleaseEvent(event);
}

LXQtWorldClockPopup::LXQtWorldClockPopup(QWidget *parent) :
    QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Popup | Qt::X11BypassWindowManagerHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setLayout(new QVBoxLayout(this));
    layout()->setContentsMargins(0, 0, 0, 0);
}

void LXQtWorldClockPopup::show()
{
    QDialog::show();
    activateWindow();
}

// ---------------------------------------------------------------------------
// AnalogClock
// ---------------------------------------------------------------------------

AnalogClock::AnalogClock(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void AnalogClock::setTime(const QTime &time)
{
    mTime = time;
    update(); // trigger a repaint
}

void AnalogClock::paintEvent(QPaintEvent * /*event*/)
{
    const int side = qMin(width(), height());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Centre and scale so the "unit circle" has radius 1
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(side / 2.0, side / 2.0);

    // --- face ---
    const QColor faceColor  = QColor(255, 255, 255);
    const QColor rimColor   = QColor(197, 197, 197);
    const QColor textColor  = QColor(0, 0, 0);
    const QColor hourColor  = QColor(0, 0, 0);
    const QColor minColor   = QColor(0, 0, 0);
    const QColor secColor   = QColor(220, 45, 45); // red sweep hand

    // Outer rim
    painter.setPen(Qt::NoPen);
    painter.setBrush(faceColor);
    painter.drawEllipse(QPointF(0, 0), 1, 1);

    // Hour tick marks
    painter.setPen(QPen(textColor, 0.025, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 12; ++i)
    {
        painter.save();
        painter.rotate(30.0 * i);
        painter.drawLine(QPointF(0, -0.90), QPointF(0, -0.96));
        painter.restore();
    }

    // Hour numbers 1-12:
    // Navigate to each position in the scaled coord system, extract the
    // device-pixel coordinate, then reset the transform before drawText so
    // the font pixel size is NOT multiplied by the painter's scale factor.
    {
        QFont numFont;
        numFont.setPixelSize(qMax(1, static_cast<int>(side * 0.15)));
        numFont.setWeight(QFont::DemiBold);
        painter.setPen(textColor);
        const double numRadius = 0.72;
        const double halfBox   = numFont.pixelSize() * 0.72; // in device px

        for (int i = 1; i <= 12; ++i)
        {
            painter.save();
            painter.rotate(30.0 * i);            // aim at hour i
            painter.translate(0.0, -numRadius);  // step to number ring
            // Map the current origin (= number centre) to device coordinates
            QPointF devPos = painter.worldTransform().map(QPointF(0.0, 0.0));
            // Reset transform: font px now equals one physical screen pixel
            painter.resetTransform();
            painter.setFont(numFont);
            QRectF box(devPos.x() - halfBox, devPos.y() - halfBox,
                       halfBox * 2.0, halfBox * 2.0);
            painter.drawText(box, Qt::AlignCenter, QString::number(i));
            painter.restore();
        }
    }

    // Minute tick marks (skip where hour ticks are)
    painter.setPen(QPen(rimColor, 0.025, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 60; ++i)
    {
        if (i % 5 != 0)
        {
            painter.save();
            painter.rotate(6.0 * i);
            painter.drawLine(QPointF(0, -0.90), QPointF(0, -0.96));
            painter.restore();
        }
    }

    if (!mTime.isNull())
    {
        const int h  = mTime.hour() % 12;
        const int m  = mTime.minute();
        const int s  = mTime.second();
        const int ms = mTime.msec();

        // Smooth angles
        const double hourAngle   = (h * 3600 + m * 60 + s) / 43200.0 * 360.0;
        const double minuteAngle = (m * 60 + s) / 3600.0 * 360.0;
        const double secondAngle = (s * 1000 + ms) / 60000.0 * 360.0;

        // --- hour hand ---
        painter.save();
        painter.rotate(hourAngle);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hourColor);
        // Draw as a filled rounded polygon
        const QPointF hourPoly[] = {
            {-0.045,  0.12},
            { 0.045,  0.12},
            { 0.03, -0.52},
            {-0.03, -0.52}
        };
        painter.drawPolygon(hourPoly, 4);
        painter.restore();

        // --- minute hand ---
        painter.save();
        painter.rotate(minuteAngle);
        painter.setPen(Qt::NoPen);
        painter.setBrush(minColor);
        const QPointF minPoly[] = {
            {-0.03,  0.16},
            { 0.03,  0.16},
            { 0.02, -0.94},
            {-0.02, -0.94}
        };
        painter.drawPolygon(minPoly, 4);
        painter.restore();

        // --- second hand ---
        painter.save();
        painter.rotate(secondAngle);
        painter.setPen(QPen(secColor, 0.025, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, 0.24), QPointF(0, -0.96));
        painter.restore();
    }

    // Centre cap
    painter.setPen(QPen(secColor, 0.02));
    painter.setBrush(faceColor);
    painter.drawEllipse(QPointF(0, 0), 0.03, 0.03);
}

// ---------------------------------------------------------------------------
// LunarCalendarWidget & Vietnamese Lunar Calendar Algorithm
// Based on Ho Ngoc Duc's algorithm (amlich.py)
// ---------------------------------------------------------------------------

namespace {

#ifndef M_PI
constexpr double M_PI = 3.14159265358979323846;
#endif

// Julian day number from Gregorian date dd/mm/yyyy
int jdFromDate(int dd, int mm, int yy)
{
    int a = (14 - mm) / 12;
    int y = yy + 4800 - a;
    int m = mm + 12 * a - 3;
    int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161)
        jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    return jd;
}

// Time of k-th new moon after the new moon of 1/1/1900 13:52 UTC
double newMoon(double k)
{
    const double T = k / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double dr = M_PI / 180.0;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    const double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    const double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    const double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2.0 * dr * M);
    C1 = C1 - 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(dr * 2.0 * Mpr);
    C1 = C1 - 0.0004 * std::sin(dr * 3.0 * Mpr);
    C1 = C1 + 0.0104 * std::sin(dr * 2.0 * F) - 0.0051 * std::sin(dr * (M + Mpr));
    C1 = C1 - 0.0074 * std::sin(dr * (M - Mpr)) + 0.0004 * std::sin(dr * (2.0 * F + M));
    C1 = C1 - 0.0004 * std::sin(dr * (2.0 * F - M)) - 0.0006 * std::sin(dr * (2.0 * F + Mpr));
    C1 = C1 + 0.0010 * std::sin(dr * (2.0 * F - Mpr)) + 0.0005 * std::sin(dr * (2.0 * Mpr + M));
    double deltat;
    if (T < -11.0)
        deltat = 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3;
    else
        deltat = -0.000278 + 0.000265 * T + 0.000262 * T2;

    return Jd1 + C1 - deltat;
}

// Longitude of the sun
double sunLongitude(double jdn)
{
    const double T = (jdn - 2451545.0) / 36525.0;
    const double T2 = T * T;
    const double dr = M_PI / 180.0;
    const double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    const double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M);
    DL += (0.019993 - 0.000101 * T) * std::sin(dr * 2.0 * M) + 0.000290 * std::sin(dr * 3.0 * M);
    double L = L0 + DL;
    L *= dr;
    L -= M_PI * 2.0 * std::floor(L / (M_PI * 2.0));
    return L;
}

int getSunLongitude(int dayNumber, double timeZone)
{
    return static_cast<int>(std::floor(sunLongitude(dayNumber - 0.5 - timeZone / 24.0) / M_PI * 6.0));
}

int getNewMoonDay(double k, double timeZone)
{
    return static_cast<int>(std::floor(newMoon(k) + 0.5 + timeZone / 24.0));
}

int getLunarMonth11(int yy, double timeZone)
{
    double off = jdFromDate(31, 12, yy) - 2415021.0;
    double k = std::floor(off / 29.530588853);
    int nm = getNewMoonDay(k, timeZone);
    int sunLong = getSunLongitude(nm, timeZone);
    if (sunLong >= 9)
        nm = getNewMoonDay(k - 1, timeZone);
    return nm;
}

int getLeapMonthOffset(int a11, double timeZone)
{
    double k = std::floor((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int arc = getSunLongitude(getNewMoonDay(k + 1, timeZone), timeZone);
    for (int i = 2; i < 15; ++i)
    {
        int last = arc;
        arc = getSunLongitude(getNewMoonDay(k + i, timeZone), timeZone);
        if (arc == last)
            return i - 1;
    }
    return 13;
}

} // namespace

LunarCalendarWidget::LunarCalendarWidget(QWidget *parent)
    : QCalendarWidget(parent),
      mTimeZone(7.0)
{
}

void LunarCalendarWidget::reload(const QTimeZone &timeZone)
{
    QTimeZone tz = timeZone.isValid() ? timeZone : QTimeZone(QTimeZone::systemTimeZoneId());
    QDate today = QDateTime::currentDateTime().toTimeZone(tz).date();
    setSelectedDate(today);
    showSelectedDate();
    updateCells();
}

LunarDate LunarCalendarWidget::solarToLunar(int dd, int mm, int yy, double timeZone)
{
    int dayNumber = jdFromDate(dd, mm, yy);
    double k = std::floor((dayNumber - 2415021.076998695) / 29.530588853);
    int monthStart = getNewMoonDay(k + 1, timeZone);
    if (monthStart > dayNumber)
        monthStart = getNewMoonDay(k, timeZone);

    int a11 = getLunarMonth11(yy, timeZone);
    int b11 = a11;
    int lunarYear;
    if (a11 >= monthStart)
    {
        lunarYear = yy;
        a11 = getLunarMonth11(yy - 1, timeZone);
    }
    else
    {
        lunarYear = yy + 1;
        b11 = getLunarMonth11(yy + 1, timeZone);
    }

    int lunarDay = dayNumber - monthStart + 1;
    int diff = static_cast<int>(std::floor((monthStart - a11) / 29.0));
    int lunarLeap = 0;
    int lunarMonth = diff + 11;
    if (b11 - a11 > 365)
    {
        int leapMonthDiff = getLeapMonthOffset(a11, timeZone);
        if (diff >= leapMonthDiff)
        {
            lunarMonth = diff + 10;
            if (diff == leapMonthDiff)
                lunarLeap = 1;
        }
    }

    if (lunarMonth > 12)
        lunarMonth -= 12;

    if (lunarMonth >= 11 && diff < 4)
        lunarYear -= 1;

    return {lunarDay, lunarMonth, lunarYear, lunarLeap};
}

void LunarCalendarWidget::paintCell(QPainter *painter, const QRect &rect, QDate date) const
{
    QCalendarWidget::paintCell(painter, rect, date);

    if (!date.isValid())
        return;

    LunarDate lunar = solarToLunar(date, mTimeZone);

    QString text;
    if (lunar.day == 1)
        text = QStringLiteral("1/%1%2").arg(lunar.month).arg(lunar.leap ? QStringLiteral("*") : QString());
    else
        text = QString::number(lunar.day);

    painter->save();

    QFont font = painter->font();
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(6.0, font.pointSizeF() * 0.72));
    else
        font.setPixelSize(qMax(8, font.pixelSize() * 7 / 10));

    if (lunar.day == 1 || lunar.day == 15)
        font.setBold(true);

    painter->setFont(font);

    if (date == selectedDate()) {
        painter->setPen(QColor(255, 255, 255));
    } else if (date.month() != monthShown()) {
        painter->setPen(QColor(128, 128, 128));
    } else if (date.dayOfWeek() > 5 || lunar.day == 1 || lunar.day == 15) {
        painter->setPen(QColor(220, 0, 0));
    } else {
        painter->setPen(QColor(220, 220, 220));
    }

    QRect lunarRect = rect.adjusted(1, 1, -2, -1);
    painter->drawText(lunarRect, Qt::AlignRight | Qt::AlignBottom, text);

    painter->restore();
}

bool LXQtWorldClock::eventFilter(QObject * watched, QEvent * event)
{
    if (mShowTooltip && watched == mMainWidget && event->type() == QEvent::ToolTip)
    {
        QHelpEvent *helpEvent = static_cast<QHelpEvent*>(event);
        QDateTime now = QDateTime::currentDateTime();
        QString timeZoneName = mActiveTimeZone;
        if (timeZoneName == QLatin1String("local"))
            timeZoneName = QString::fromLatin1(QTimeZone::systemTimeZoneId());
        QTimeZone timeZone(timeZoneName.toLatin1());
        QDateTime tzNow = now.toTimeZone(timeZone);
        QToolTip::showText(helpEvent->globalPos(), QLocale::system().toString(tzNow, QStringLiteral("ddd, MMM d yyyy hh:mm:ss")));
        return false;
    }
    return QObject::eventFilter(watched, event);
}
