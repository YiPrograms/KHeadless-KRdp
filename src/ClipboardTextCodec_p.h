// SPDX-FileCopyrightText: 2026 KHeadless contributors
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringConverter>

#include <winpr/user.h>

namespace KRdp
{

constexpr qsizetype MaximumClipboardTextBytes = 16 * 1024 * 1024;

inline QByteArray encodeClipboardText(const QString &text, uint32_t format)
{
    if (format == CF_UNICODETEXT) {
        QStringEncoder encoder(QStringConverter::Utf16LE);
        QByteArray encoded = encoder(text);
        encoded.append(2, '\0');
        return encoded;
    }
    if (format == CF_TEXT || format == CF_OEMTEXT) {
        auto encoded = text.toLocal8Bit();
        encoded.append('\0');
        return encoded;
    }
    return {};
}

inline std::optional<QString> decodeClipboardText(const BYTE *data, qsizetype size, uint32_t format)
{
    if (!data || size < 0 || size > MaximumClipboardTextBytes) {
        return std::nullopt;
    }

    QString text;
    if (format == CF_UNICODETEXT) {
        if ((size % 2) != 0) {
            return std::nullopt;
        }
        QStringDecoder decoder(QStringConverter::Utf16LE);
        text = decoder(QByteArrayView(reinterpret_cast<const char *>(data), size));
        if (decoder.hasError()) {
            return std::nullopt;
        }
    } else if (format == CF_TEXT || format == CF_OEMTEXT) {
        text = QString::fromLocal8Bit(reinterpret_cast<const char *>(data), size);
    } else {
        return std::nullopt;
    }

    while (text.endsWith(QChar::Null)) {
        text.chop(1);
    }
    return text;
}

}
