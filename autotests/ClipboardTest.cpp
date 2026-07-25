// SPDX-FileCopyrightText: 2026 KHeadless contributors
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <QTest>

#include "ClipboardTextCodec_p.h"

using namespace KRdp;

class ClipboardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unicodeRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("ascii") << QStringLiteral("KHeadless clipboard");
        QTest::newRow("unicode") << QStringLiteral("多螢幕 KDE Plasma 🖥️");
        QTest::newRow("newlines") << QStringLiteral("first\r\nsecond\nthird");
    }

    void unicodeRoundTrip()
    {
        QFETCH(QString, text);
        const auto encoded = encodeClipboardText(text, CF_UNICODETEXT);
        QVERIFY(encoded.endsWith(QByteArray(2, '\0')));
        const auto decoded = decodeClipboardText(reinterpret_cast<const BYTE *>(encoded.constData()), encoded.size(), CF_UNICODETEXT);
        QVERIFY(decoded.has_value());
        QCOMPARE(*decoded, text);
    }

    void localTextRoundTrip()
    {
        const auto source = QStringLiteral("plain text");
        const auto encoded = encodeClipboardText(source, CF_TEXT);
        const auto decoded = decodeClipboardText(reinterpret_cast<const BYTE *>(encoded.constData()), encoded.size(), CF_TEXT);
        QVERIFY(decoded.has_value());
        QCOMPARE(*decoded, source);
    }

    void rejectsMalformedPayloads()
    {
        const QByteArray oddUtf16("x", 1);
        QVERIFY(!decodeClipboardText(reinterpret_cast<const BYTE *>(oddUtf16.constData()), oddUtf16.size(), CF_UNICODETEXT));
        QVERIFY(!decodeClipboardText(nullptr, 0, CF_UNICODETEXT));
        QVERIFY(encodeClipboardText(QStringLiteral("text"), 0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ClipboardTest)

#include "ClipboardTest.moc"
