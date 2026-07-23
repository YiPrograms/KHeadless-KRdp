// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
// SPDX-FileCopyrightText: 2026 KHeadless contributors
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "DisplayControl.h"

#include <algorithm>
#include <limits>

#include <QRect>

#include <freerdp/peer.h>
#include <freerdp/settings.h>

#include "PeerContext_p.h"
#include "RdpConnection.h"
#include "VideoStream.h"
#include "krdp_logging.h"

namespace KRdp
{

namespace
{

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool adjacent(const DisplayMonitor &first, const DisplayMonitor &second)
{
    const qint64 firstLeft = first.position.x();
    const qint64 firstTop = first.position.y();
    const qint64 firstRight = firstLeft + first.size.width();
    const qint64 firstBottom = firstTop + first.size.height();
    const qint64 secondLeft = second.position.x();
    const qint64 secondTop = second.position.y();
    const qint64 secondRight = secondLeft + second.size.width();
    const qint64 secondBottom = secondTop + second.size.height();

    const bool verticalRangesTouch = std::max(firstTop, secondTop) <= std::min(firstBottom, secondBottom);
    const bool horizontalRangesTouch = std::max(firstLeft, secondLeft) <= std::min(firstRight, secondRight);
    return ((firstRight == secondLeft || secondRight == firstLeft) && verticalRangesTouch)
        || ((firstBottom == secondTop || secondBottom == firstTop) && horizontalRangesTouch);
}

}

UINT DisplayControl::receiveMonitorLayout(DispServerContext *context, const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu)
{
    auto control = static_cast<DisplayControl *>(context->custom);
    if (!control || !pdu) {
        return CHANNEL_RC_NULL_DATA;
    }

    DisplayMonitorList monitors;
    QString error;
    if (!decodeMonitorLayout(*pdu, &monitors, &error)) {
        qCWarning(KRDP) << "Rejected RDP display layout:" << error;
        return CHANNEL_RC_BAD_CHANNEL;
    }

    control->requestMonitorLayout(monitors);
    return CHANNEL_RC_OK;
}

DisplayControl::DisplayControl(RdpConnection *connection)
    : m_connection(connection)
{
}

DisplayControl::~DisplayControl()
{
    close();
}

bool DisplayControl::initialize()
{
    if (m_context) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(m_connection->rdpPeer()->context);
    m_context = disp_server_context_new(peerContext->virtualChannelManager);
    if (!m_context) {
        qCWarning(KRDP) << "Failed creating Display Control context";
        return false;
    }

    m_context->rdpcontext = m_connection->rdpPeer()->context;
    m_context->custom = this;
    m_context->DispMonitorLayout = &DisplayControl::receiveMonitorLayout;
    m_context->MaxNumMonitors = MaximumMonitorCount;
    m_context->MaxMonitorAreaFactorA = 8192;
    m_context->MaxMonitorAreaFactorB = 8192;

    if (m_context->Open(m_context) != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Could not open Display Control context";
        close();
        return false;
    }
    if (m_context->DisplayControlCaps(m_context) != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Could not advertise Display Control capabilities";
        close();
        return false;
    }

    requestInitialMonitorLayout();
    return true;
}

void DisplayControl::close()
{
    if (!m_context) {
        return;
    }

    disp_server_context_free(m_context);
    m_context = nullptr;
}

bool DisplayControl::acceptRequestedMonitorLayout(const DisplayMonitorList &monitors, QString *error)
{
    {
        std::lock_guard lock(m_requestedMonitorLayoutMutex);
        if (!m_requestedMonitorLayout) {
            return fail(error, QStringLiteral("There is no pending client monitor layout"));
        }
        if (*m_requestedMonitorLayout != monitors) {
            return fail(error, QStringLiteral("The accepted layout does not match the pending client request"));
        }
        m_requestedMonitorLayout.reset();
    }

    m_connection->videoStream()->setMonitorLayout(monitors);
    return true;
}

void DisplayControl::rejectRequestedMonitorLayout()
{
    std::lock_guard lock(m_requestedMonitorLayoutMutex);
    m_requestedMonitorLayout.reset();
}

std::optional<DisplayMonitorList> DisplayControl::requestedMonitorLayout() const
{
    std::lock_guard lock(m_requestedMonitorLayoutMutex);
    return m_requestedMonitorLayout;
}

void DisplayControl::requestMonitorLayout(const DisplayMonitorList &monitors)
{
    {
        std::lock_guard lock(m_requestedMonitorLayoutMutex);
        m_requestedMonitorLayout = monitors;
    }
    Q_EMIT requestedMonitorLayoutChanged(monitors);
}

void DisplayControl::requestInitialMonitorLayout()
{
    const auto settings = m_connection->rdpPeer()->context->settings;
    const auto monitorCount = freerdp_settings_get_uint32(settings, FreeRDP_MonitorCount);
    const auto monitorArray = static_cast<const rdpMonitor *>(freerdp_settings_get_pointer(settings, FreeRDP_MonitorDefArray));

    std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> wireMonitors;
    if (monitorCount > 0 && monitorArray) {
        wireMonitors.reserve(monitorCount);
        for (uint32_t index = 0; index < monitorCount; ++index) {
            const auto &monitor = monitorArray[index];
            wireMonitors.push_back({
                .Flags = monitor.is_primary ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0U,
                .Left = monitor.x,
                .Top = monitor.y,
                .Width = uint32_t(monitor.width),
                .Height = uint32_t(monitor.height),
                .PhysicalWidth = monitor.attributes.physicalWidth,
                .PhysicalHeight = monitor.attributes.physicalHeight,
                .Orientation = monitor.attributes.orientation,
                .DesktopScaleFactor = monitor.attributes.desktopScaleFactor,
                .DeviceScaleFactor = monitor.attributes.deviceScaleFactor,
            });
        }
    } else {
        wireMonitors.push_back({
            .Flags = DISPLAY_CONTROL_MONITOR_PRIMARY,
            .Left = 0,
            .Top = 0,
            .Width = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
            .Height = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight),
            .PhysicalWidth = 0,
            .PhysicalHeight = 0,
            .Orientation = 0,
            .DesktopScaleFactor = 100,
            .DeviceScaleFactor = 100,
        });
    }

    DISPLAY_CONTROL_MONITOR_LAYOUT_PDU pdu{
        .MonitorLayoutSize = DISPLAY_CONTROL_MONITOR_LAYOUT_SIZE,
        .NumMonitors = uint32_t(wireMonitors.size()),
        .Monitors = wireMonitors.data(),
    };
    DisplayMonitorList monitors;
    QString error;
    if (decodeMonitorLayout(pdu, &monitors, &error)) {
        requestMonitorLayout(monitors);
    } else {
        qCWarning(KRDP) << "Ignoring invalid initial client monitor layout:" << error;
    }
}

bool DisplayControl::decodeMonitorLayout(const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU &pdu,
                                         DisplayMonitorList *monitors,
                                         QString *error)
{
    if (!monitors) {
        return fail(error, QStringLiteral("No destination was provided"));
    }
    monitors->clear();

    if (pdu.MonitorLayoutSize != DISPLAY_CONTROL_MONITOR_LAYOUT_SIZE) {
        return fail(error, QStringLiteral("Unsupported monitor record size"));
    }
    if (pdu.NumMonitors == 0 || pdu.NumMonitors > MaximumMonitorCount || !pdu.Monitors) {
        return fail(error, QStringLiteral("Monitor count must be between 1 and %1").arg(MaximumMonitorCount));
    }

    DisplayMonitorList decoded;
    decoded.reserve(pdu.NumMonitors);
    int primaryCount = 0;
    qint64 minimumX = std::numeric_limits<qint64>::max();
    qint64 minimumY = std::numeric_limits<qint64>::max();
    qint64 maximumX = std::numeric_limits<qint64>::min();
    qint64 maximumY = std::numeric_limits<qint64>::min();

    for (uint32_t index = 0; index < pdu.NumMonitors; ++index) {
        const auto &wireMonitor = pdu.Monitors[index];
        if (wireMonitor.Width < DISPLAY_CONTROL_MIN_MONITOR_WIDTH || wireMonitor.Width > DISPLAY_CONTROL_MAX_MONITOR_WIDTH
            || (wireMonitor.Width % 2) != 0
            || wireMonitor.Height < DISPLAY_CONTROL_MIN_MONITOR_HEIGHT || wireMonitor.Height > DISPLAY_CONTROL_MAX_MONITOR_HEIGHT) {
            return fail(error, QStringLiteral("Monitor %1 has an unsupported pixel size").arg(index));
        }

        const bool primary = (wireMonitor.Flags & DISPLAY_CONTROL_MONITOR_PRIMARY) != 0;
        primaryCount += primary ? 1 : 0;
        if (primary && (wireMonitor.Left != 0 || wireMonitor.Top != 0)) {
            return fail(error, QStringLiteral("The primary monitor must be at 0,0"));
        }

        const qint64 right = qint64(wireMonitor.Left) + wireMonitor.Width;
        const qint64 bottom = qint64(wireMonitor.Top) + wireMonitor.Height;
        minimumX = std::min(minimumX, qint64(wireMonitor.Left));
        minimumY = std::min(minimumY, qint64(wireMonitor.Top));
        maximumX = std::max(maximumX, right);
        maximumY = std::max(maximumY, bottom);

        const QRect geometry(wireMonitor.Left, wireMonitor.Top, wireMonitor.Width, wireMonitor.Height);
        for (const auto &other : std::as_const(decoded)) {
            if (geometry.intersects(QRect(other.position, other.size))) {
                return fail(error, QStringLiteral("Monitor %1 overlaps another monitor").arg(index));
            }
        }

        const bool validPhysicalSize = wireMonitor.PhysicalWidth >= DISPLAY_CONTROL_MIN_PHYSICAL_MONITOR_WIDTH
            && wireMonitor.PhysicalWidth <= DISPLAY_CONTROL_MAX_PHYSICAL_MONITOR_WIDTH
            && wireMonitor.PhysicalHeight >= DISPLAY_CONTROL_MIN_PHYSICAL_MONITOR_HEIGHT
            && wireMonitor.PhysicalHeight <= DISPLAY_CONTROL_MAX_PHYSICAL_MONITOR_HEIGHT;
        const bool validOrientation = wireMonitor.Orientation == 0 || wireMonitor.Orientation == 90 || wireMonitor.Orientation == 180
            || wireMonitor.Orientation == 270;
        const bool validScale = wireMonitor.DesktopScaleFactor >= 100 && wireMonitor.DesktopScaleFactor <= 500
            && (wireMonitor.DeviceScaleFactor == 100 || wireMonitor.DeviceScaleFactor == 140 || wireMonitor.DeviceScaleFactor == 180);

        decoded.append({
            .position = QPoint(wireMonitor.Left, wireMonitor.Top),
            .size = QSize(wireMonitor.Width, wireMonitor.Height),
            .physicalSize = validPhysicalSize ? QSize(wireMonitor.PhysicalWidth, wireMonitor.PhysicalHeight) : QSize(),
            .orientation = validOrientation ? wireMonitor.Orientation : 0U,
            .desktopScaleFactor = validScale ? wireMonitor.DesktopScaleFactor : 100U,
            .deviceScaleFactor = validScale ? wireMonitor.DeviceScaleFactor : 100U,
            .primary = primary,
        });
    }

    if (primaryCount != 1) {
        return fail(error, QStringLiteral("Exactly one monitor must be primary"));
    }
    if (maximumX - minimumX > MaximumDesktopExtent || maximumY - minimumY > MaximumDesktopExtent) {
        return fail(error, QStringLiteral("The combined desktop is larger than %1 pixels").arg(MaximumDesktopExtent));
    }
    if (decoded.size() > 1) {
        for (qsizetype index = 0; index < decoded.size(); ++index) {
            bool hasAdjacentMonitor = false;
            for (qsizetype other = 0; other < decoded.size(); ++other) {
                if (index != other && adjacent(decoded.at(index), decoded.at(other))) {
                    hasAdjacentMonitor = true;
                    break;
                }
            }
            if (!hasAdjacentMonitor) {
                return fail(error, QStringLiteral("Monitor %1 is not adjacent to another monitor").arg(index));
            }
        }
    }

    *monitors = decoded;
    return true;
}

}
