// SPDX-FileCopyrightText: 2026 KHeadless contributors
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "AudioStream.h"

#include <atomic>
#include <chrono>

#include <freerdp/codec/audio.h>
#include <freerdp/server/rdpsnd.h>

#include "PeerContext_p.h"
#include "RdpConnection.h"
#include "krdp_logging.h"

namespace KRdp
{

class KRDP_NO_EXPORT AudioStream::Private
{
public:
    using Context = std::unique_ptr<RdpsndServerContext, decltype(&rdpsnd_server_context_free)>;

    static void activated(RdpsndServerContext *context)
    {
        auto *stream = static_cast<AudioStream *>(context->data);
        if (!stream) {
            return;
        }
        for (UINT16 index = 0; index < context->num_client_formats; ++index) {
            const auto &format = context->client_formats[index];
            if (format.wFormatTag == WAVE_FORMAT_PCM && format.nChannels == 2
                && format.nSamplesPerSec == 48'000 && format.wBitsPerSample == 16) {
                if (context->SelectFormat(context, index) == CHANNEL_RC_OK) {
                    stream->d->active.store(true);
                    Q_EMIT stream->activeChanged();
                }
                return;
            }
        }
        qCWarning(KRDP) << "RDP client offered no compatible 48 kHz stereo PCM audio format";
    }

    RdpConnection *connection = nullptr;
    Context context = Context(nullptr, rdpsnd_server_context_free);
    AUDIO_FORMAT *serverFormat = nullptr;
    AUDIO_FORMAT *sourceFormat = nullptr;
    std::atomic_bool active = false;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

AudioStream::AudioStream(RdpConnection *connection)
    : d(std::make_unique<Private>())
{
    d->connection = connection;
}

AudioStream::~AudioStream()
{
    close();
    audio_format_free(d->sourceFormat);
}

bool AudioStream::initialize()
{
    if (d->context) {
        return true;
    }

    auto *peerContext = reinterpret_cast<PeerContext *>(d->connection->rdpPeerContext());
    d->context.reset(rdpsnd_server_context_new(peerContext->virtualChannelManager));
    d->serverFormat = audio_formats_new(1);
    d->sourceFormat = audio_format_new();
    if (!d->context || !d->serverFormat || !d->sourceFormat) {
        qCWarning(KRDP) << "Unable to allocate the RDP playback audio channel";
        if (d->context) {
            d->context->server_formats = d->serverFormat;
            d->context->num_server_formats = d->serverFormat ? 1 : 0;
        } else {
            audio_formats_free(d->serverFormat, d->serverFormat ? 1 : 0);
        }
        d->serverFormat = nullptr;
        close();
        return false;
    }

    AUDIO_FORMAT pcm{
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = 2,
        .nSamplesPerSec = 48'000,
        .nAvgBytesPerSec = 48'000 * 2 * 2,
        .nBlockAlign = 4,
        .wBitsPerSample = 16,
        .cbSize = 0,
        .data = nullptr,
    };
    *d->serverFormat = pcm;
    *d->sourceFormat = pcm;
    d->context->server_formats = d->serverFormat;
    d->context->num_server_formats = 1;
    d->context->src_format = d->sourceFormat;
    d->context->latency = 50;
    d->context->rdpcontext = d->connection->rdpPeerContext();
    d->context->data = this;
    d->context->Activated = &Private::activated;

    if (d->context->Initialize(d->context.get(), TRUE) != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Unable to initialize the RDP playback audio channel";
        close();
        return false;
    }
    d->started = std::chrono::steady_clock::now();
    return true;
}

void AudioStream::close()
{
    const bool wasActive = d->active.exchange(false);
    if (d->context) {
        d->context->Stop(d->context.get());
        d->context.reset();
        // FreeRDP owns and frees server_formats with the channel context.
        d->serverFormat = nullptr;
    }
    if (wasActive) {
        Q_EMIT activeChanged();
    }
}

bool AudioStream::active() const
{
    return d->active.load();
}

void AudioStream::sendSamples(const QByteArray &pcm)
{
    if (!d->active.load() || !d->context || pcm.isEmpty()) {
        return;
    }
    const auto byteCount = pcm.size() - (pcm.size() % 4);
    if (byteCount == 0) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - d->started);
    const auto timestamp = UINT16(elapsed.count() & 0xffff);
    const auto frames = size_t(byteCount / 4);
    const auto result = d->context->SendSamples(d->context.get(), pcm.constData(), frames, timestamp);
    if (result != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Unable to send RDP playback audio samples:" << result;
    }
}

}

#include "moc_AudioStream.cpp"
