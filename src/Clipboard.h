// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QObject>

#include <freerdp/freerdp.h>
#include <freerdp/server/cliprdr.h>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT Clipboard : public QObject
{
    Q_OBJECT

public:
    explicit Clipboard(RdpConnection *session);
    ~Clipboard() override;

    bool initialize();
    void close();

    void setEnabled(bool enabled);
    bool enabled() const;

    /**
     * Advertise host clipboard text to the RDP client.
     */
    void setServerText(const QString &text);

Q_SIGNALS:
    /**
     * Emitted when the RDP client publishes new clipboard text.
     */
    void clientTextChanged(const QString &text);

private:
    void sendServerData();

    class Private;
    const std::unique_ptr<Private> d;
};
}
