// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
// SPDX-FileCopyrightText: 2026 KHeadless contributors
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>

#include <freerdp/server/disp.h>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * A monitor requested by an RDP client through MS-RDPEDISP.
 *
 * Positions are relative to the primary monitor, whose origin is always 0,0.
 * The list order is stable for the lifetime of a layout and is used as the
 * graphics output index.
 */
struct KRDP_EXPORT DisplayMonitor {
    QPoint position;
    QSize size;
    QSize physicalSize;
    uint32_t orientation = 0;
    uint32_t desktopScaleFactor = 100;
    uint32_t deviceScaleFactor = 100;
    bool primary = false;

    bool operator==(const DisplayMonitor &) const = default;
};

using DisplayMonitorList = QList<DisplayMonitor>;

/**
 * Server side implementation of the RDP Display Control virtual channel.
 */
class KRDP_EXPORT DisplayControl : public QObject
{
    Q_OBJECT

public:
    static constexpr uint32_t MaximumMonitorCount = 16;
    static constexpr uint32_t MaximumDesktopExtent = 32766;

    explicit DisplayControl(RdpConnection *connection);
    ~DisplayControl() override;

    bool initialize();
    void close();

    /**
     * Convert and validate a wire monitor layout.
     *
     * This method is public so embedders and tests can apply exactly the same
     * policy to initial and dynamic layouts.
     */
    static bool decodeMonitorLayout(const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU &pdu,
                                    DisplayMonitorList *monitors,
                                    QString *error = nullptr);

Q_SIGNALS:
    void requestedMonitorLayoutChanged(const KRdp::DisplayMonitorList &monitors);

private:
    RdpConnection *m_connection = nullptr;
    DispServerContext *m_context = nullptr;
};

}

Q_DECLARE_METATYPE(KRdp::DisplayMonitor)
Q_DECLARE_METATYPE(KRdp::DisplayMonitorList)
