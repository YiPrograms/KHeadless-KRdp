// SPDX-FileCopyrightText: 2026 KHeadless contributors
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QByteArray>
#include <QObject>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * Server-side RDP playback audio channel.
 *
 * The embedder supplies interleaved 48 kHz, stereo, signed 16-bit PCM. KRdp
 * negotiates the matching client format and applies RDPSND flow control.
 */
class KRDP_EXPORT AudioStream : public QObject
{
    Q_OBJECT

public:
    explicit AudioStream(RdpConnection *connection);
    ~AudioStream() override;

    bool initialize();
    void close();
    bool active() const;
    void sendSamples(const QByteArray &pcm);

    Q_SIGNAL void activeChanged();

private:
    class Private;
    const std::unique_ptr<Private> d;
};

}
