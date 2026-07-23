// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Clipboard.h"

#include <QMetaObject>
#include <QStringConverter>
#include <QThread>

#include "PeerContext_p.h"
#include "RdpConnection.h"
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/server/cliprdr.h>
#include <winpr/user.h>

#include "krdp_logging.h"

namespace KRdp
{
class KRDP_NO_EXPORT Clipboard::Private
{
public:
    using CliprdrServerContextPtr = std::unique_ptr<CliprdrServerContext, decltype(&cliprdr_server_context_free)>;

    explicit Private(Clipboard *q)
        : q(q)
    {
    }

    uint32_t onClientFormatList(const CLIPRDR_FORMAT_LIST *formatList);
    uint32_t onClientFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE *formatListResponse);
    uint32_t onClientFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST *formatDataRequest);
    uint32_t onClientFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE *formatDataResponse);

    template<typename>
    struct FunctionArgument;

    template<typename Argument>
    struct FunctionArgument<uint32_t (Private::*)(Argument)> {
        using Type = Argument;
    };

    template<auto function>
    static UINT processInMainThread(Clipboard *clipboard, typename FunctionArgument<decltype(function)>::Type packet)
    {
        if (clipboard->thread() == QThread::currentThread()) {
            return ((*clipboard->d).*function)(packet);
        }
        uint32_t result = CHANNEL_RC_OK;
        QMetaObject::invokeMethod(
            clipboard,
            [clipboard, packet, &result]() {
                result = ((*clipboard->d).*function)(packet);
            },
            Qt::BlockingQueuedConnection);
        return result;
    }

    static UINT clientFormatList(CliprdrServerContext *context, const CLIPRDR_FORMAT_LIST *formatList)
    {
        return processInMainThread<&Private::onClientFormatList>(static_cast<Clipboard *>(context->custom), formatList);
    }

    static UINT clientFormatListResponse(CliprdrServerContext *context, const CLIPRDR_FORMAT_LIST_RESPONSE *response)
    {
        return processInMainThread<&Private::onClientFormatListResponse>(static_cast<Clipboard *>(context->custom), response);
    }

    static UINT clientFormatDataRequest(CliprdrServerContext *context, const CLIPRDR_FORMAT_DATA_REQUEST *request)
    {
        return processInMainThread<&Private::onClientFormatDataRequest>(static_cast<Clipboard *>(context->custom), request);
    }

    static UINT clientFormatDataResponse(CliprdrServerContext *context, const CLIPRDR_FORMAT_DATA_RESPONSE *response)
    {
        return processInMainThread<&Private::onClientFormatDataResponse>(static_cast<Clipboard *>(context->custom), response);
    }

    Clipboard *q = nullptr;
    RdpConnection *session;
    CliprdrServerContextPtr clipContext = CliprdrServerContextPtr(nullptr, cliprdr_server_context_free);
    QString serverText;
    uint32_t requestedClientFormat = 0;
    bool hasServerText = false;
    bool initialized = false;
    bool enabled = false;
};

Clipboard::Clipboard(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>(this))
{
    d->session = session;
}

Clipboard::~Clipboard()
{
    close();
}

bool Clipboard::initialize()
{
    if (d->clipContext) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(d->session->rdpPeer()->context);

    d->clipContext = Private::CliprdrServerContextPtr{cliprdr_server_context_new(peerContext->virtualChannelManager), cliprdr_server_context_free};
    if (!d->clipContext) {
        qCWarning(KRDP) << "Failed creating Clipboard context";
        return false;
    }

    d->clipContext->useLongFormatNames = TRUE;
    d->clipContext->streamFileClipEnabled = FALSE;
    d->clipContext->fileClipNoFilePaths = FALSE;
    d->clipContext->canLockClipData = FALSE;
    d->clipContext->hasHugeFileSupport = FALSE;

    d->clipContext->custom = this;
    d->clipContext->rdpcontext = d->session->rdpPeer()->context;
    d->clipContext->ClientFormatList = Private::clientFormatList;
    d->clipContext->ClientFormatListResponse = Private::clientFormatListResponse;
    d->clipContext->ClientFormatDataRequest = Private::clientFormatDataRequest;
    d->clipContext->ClientFormatDataResponse = Private::clientFormatDataResponse;

    // returns 0 on success
    // https://pub.freerdp.com/api/server_2cliprdr__main_8c.html#ab4e8a28c6b4371c2a5f34e8716ab1e9e
    if (d->clipContext->Start(d->clipContext.get())) {
        qCWarning(KRDP) << "Could not start Clipboard context";
        return false;
    };

    d->initialized = true;
    if (d->enabled && d->hasServerText) {
        QMetaObject::invokeMethod(this, &Clipboard::sendServerData, Qt::QueuedConnection);
    }

    return true;
}

void Clipboard::setEnabled(bool enabled)
{
    if (d->enabled == enabled) {
        return;
    }
    d->enabled = enabled;
    d->requestedClientFormat = 0;
    if (enabled && d->initialized && d->hasServerText) {
        QMetaObject::invokeMethod(this, &Clipboard::sendServerData, Qt::QueuedConnection);
    }
}

bool Clipboard::enabled() const
{
    return d->initialized && d->enabled;
}

void Clipboard::setServerText(const QString &text)
{
    d->serverText = text;
    d->hasServerText = true;
    if (enabled()) {
        QMetaObject::invokeMethod(this, &Clipboard::sendServerData, Qt::QueuedConnection);
    }
}

void Clipboard::close()
{
    if (!d->clipContext) {
        return;
    }

    if (d->clipContext->Stop(d->clipContext.get())) {
        qCWarning(KRDP) << "Could not stop Clipboard context";
    }
    d->clipContext.reset();
    d->initialized = false;
    d->enabled = false;
    d->requestedClientFormat = 0;
}

void Clipboard::sendServerData()
{
    if (!enabled() || !d->hasServerText || !d->clipContext) {
        return;
    }

    CLIPRDR_FORMAT format{
        .formatId = CF_UNICODETEXT,
        .formatName = nullptr,
    };
    CLIPRDR_FORMAT_LIST formatList{
        .common =
            {
                .msgType = CB_FORMAT_LIST,
                .msgFlags = 0,
                .dataLen = 0,
            },
        .numFormats = 1,
        .formats = &format,
    };
    d->clipContext->ServerFormatList(d->clipContext.get(), &formatList);
}

uint32_t Clipboard::Private::onClientFormatList(const CLIPRDR_FORMAT_LIST *formatList)
{
    requestedClientFormat = 0;
    if (enabled) {
        for (uint32_t index = 0; index < formatList->numFormats; ++index) {
            if (formatList->formats[index].formatId == CF_UNICODETEXT) {
                requestedClientFormat = CF_UNICODETEXT;
                break;
            }
            if (requestedClientFormat == 0 && (formatList->formats[index].formatId == CF_TEXT || formatList->formats[index].formatId == CF_OEMTEXT)) {
                requestedClientFormat = formatList->formats[index].formatId;
            }
        }
    }

    CLIPRDR_FORMAT_LIST_RESPONSE response{
        .common =
            {
                .msgType = CB_FORMAT_LIST_RESPONSE,
                .msgFlags = CB_RESPONSE_OK,
                .dataLen = 0,
            },
    };
    clipContext->ServerFormatListResponse(clipContext.get(), &response);

    if (requestedClientFormat != 0) {
        CLIPRDR_FORMAT_DATA_REQUEST request{
            .common =
                {
                    .msgType = CB_FORMAT_DATA_REQUEST,
                    .msgFlags = 0,
                    .dataLen = 4,
                },
            .requestedFormatId = requestedClientFormat,
        };
        clipContext->ServerFormatDataRequest(clipContext.get(), &request);
    }
    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE *)
{
    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST *request)
{
    CLIPRDR_FORMAT_DATA_RESPONSE response{
        .common =
            {
                .msgType = CB_FORMAT_DATA_RESPONSE,
                .msgFlags = CB_RESPONSE_FAIL,
                .dataLen = 0,
            },
        .requestedFormatData = nullptr,
    };
    QByteArray encoded;
    if (enabled && hasServerText && request->requestedFormatId == CF_UNICODETEXT) {
        QStringEncoder encoder(QStringConverter::Utf16LE);
        encoded = encoder(serverText);
        encoded.append(2, '\0');
    } else if (enabled && hasServerText && (request->requestedFormatId == CF_TEXT || request->requestedFormatId == CF_OEMTEXT)) {
        encoded = serverText.toLocal8Bit();
        encoded.append('\0');
    }
    if (!encoded.isEmpty()) {
        response.common.msgFlags = CB_RESPONSE_OK;
        response.common.dataLen = encoded.size();
        response.requestedFormatData = reinterpret_cast<const BYTE *>(encoded.constData());
    }
    clipContext->ServerFormatDataResponse(clipContext.get(), &response);
    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE *response)
{
    constexpr uint32_t maximumTextBytes = 16 * 1024 * 1024;
    if (!enabled || !(response->common.msgFlags & CB_RESPONSE_OK) || !response->requestedFormatData || response->common.dataLen > maximumTextBytes) {
        return CHANNEL_RC_OK;
    }

    QString text;
    if (requestedClientFormat == CF_UNICODETEXT) {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        text = decoder(QByteArrayView(reinterpret_cast<const char *>(response->requestedFormatData), response->common.dataLen));
    } else {
        text = QString::fromLocal8Bit(reinterpret_cast<const char *>(response->requestedFormatData), response->common.dataLen);
    }
    while (text.endsWith(QChar::Null)) {
        text.chop(1);
    }
    requestedClientFormat = 0;
    Q_EMIT q->clientTextChanged(text);
    return CHANNEL_RC_OK;
}
}
