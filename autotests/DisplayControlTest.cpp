// SPDX-FileCopyrightText: 2026 KHeadless contributors
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <QTest>

#include <vector>

#include "DisplayControl.h"

using namespace KRdp;

namespace
{

DISPLAY_CONTROL_MONITOR_LAYOUT monitor(int x, int y, uint32_t width, uint32_t height, bool primary = false)
{
    return {
        .Flags = primary ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0U,
        .Left = x,
        .Top = y,
        .Width = width,
        .Height = height,
        .PhysicalWidth = 0,
        .PhysicalHeight = 0,
        .Orientation = 0,
        .DesktopScaleFactor = 100,
        .DeviceScaleFactor = 100,
    };
}

DISPLAY_CONTROL_MONITOR_LAYOUT_PDU pdu(std::vector<DISPLAY_CONTROL_MONITOR_LAYOUT> &monitors)
{
    return {
        .MonitorLayoutSize = DISPLAY_CONTROL_MONITOR_LAYOUT_SIZE,
        .NumMonitors = uint32_t(monitors.size()),
        .Monitors = monitors.data(),
    };
}

}

class DisplayControlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acceptsMultipleMonitors()
    {
        std::vector monitors{
            monitor(0, 0, 2560, 1440, true),
            monitor(-1920, 0, 1920, 1080),
            monitor(2560, 0, 1280, 720),
        };
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QString error;
        QVERIFY2(DisplayControl::decodeMonitorLayout(layoutPdu, &decoded, &error), qPrintable(error));
        QCOMPARE(decoded.size(), 3);
        QCOMPARE(decoded.at(0).position, QPoint(0, 0));
        QCOMPARE(decoded.at(1).position, QPoint(-1920, 0));
        QCOMPARE(decoded.at(2).size, QSize(1280, 720));
        QVERIFY(decoded.at(0).primary);
    }

    void rejectsOverlappingMonitors()
    {
        std::vector monitors{
            monitor(0, 0, 1920, 1080, true),
            monitor(1000, 0, 1920, 1080),
        };
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QVERIFY(!DisplayControl::decodeMonitorLayout(layoutPdu, &decoded));
    }

    void rejectsMissingPrimary()
    {
        std::vector monitors{monitor(0, 0, 1920, 1080)};
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QVERIFY(!DisplayControl::decodeMonitorLayout(layoutPdu, &decoded));
    }

    void rejectsDisconnectedMonitor()
    {
        std::vector monitors{
            monitor(0, 0, 1920, 1080, true),
            monitor(3000, 0, 1920, 1080),
        };
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QVERIFY(!DisplayControl::decodeMonitorLayout(layoutPdu, &decoded));
    }

    void ignoresInvalidOptionalAttributes()
    {
        auto invalid = monitor(0, 0, 1920, 1080, true);
        invalid.Orientation = 45;
        invalid.DesktopScaleFactor = 700;
        invalid.DeviceScaleFactor = 125;
        invalid.PhysicalWidth = 1;
        invalid.PhysicalHeight = 1;
        std::vector monitors{invalid};
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QVERIFY(DisplayControl::decodeMonitorLayout(layoutPdu, &decoded));
        QCOMPARE(decoded.constFirst().orientation, 0U);
        QCOMPARE(decoded.constFirst().desktopScaleFactor, 100U);
        QCOMPARE(decoded.constFirst().deviceScaleFactor, 100U);
        QVERIFY(decoded.constFirst().physicalSize.isEmpty());
    }

    void rejectsExcessiveDesktopExtent()
    {
        std::vector monitors{
            monitor(0, 0, 8192, 8192, true),
            monitor(32760, 0, 200, 200),
        };
        const auto layoutPdu = pdu(monitors);

        DisplayMonitorList decoded;
        QVERIFY(!DisplayControl::decodeMonitorLayout(layoutPdu, &decoded));
    }
};

QTEST_GUILESS_MAIN(DisplayControlTest)

#include "DisplayControlTest.moc"
