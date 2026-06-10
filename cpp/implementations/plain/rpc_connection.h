#ifndef LOGOS_PLAIN_RPC_CONNECTION_H
#define LOGOS_PLAIN_RPC_CONNECTION_H

#include "incoming_call_handler.h"
#include "rpc_framing.h"
#include "rpc_message.h"
#include "wire_codec.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace logos::plain {

// -----------------------------------------------------------------------------
// RpcConnectionBase — type-erased public surface of RpcConnection<Stream>.
//
// Callers (plain_logos_object, plain_transport_host) hold a
// shared_ptr<RpcConnectionBase> so they don't have to know whether the
// underlying socket is plain TCP or TLS-wrapped TCP. All the async machinery
// lives in the templated subclass.
// -----------------------------------------------------------------------------
class RpcConnectionBase {
public:
    using ErrorHandler = std::function<void(const std::string& reason)>;

    virtual ~RpcConnectionBase() = default;

    virtual void start() = 0;
    virtual void stop(const std::string& reason = "stopped") = 0;
    virtual bool isOpen() const = 0;

    virtual std::future<ResultMessage>        sendCall(CallMessage msg) = 0;
    virtual std::future<MethodsResultMessage> sendMethods(MethodsMessage msg) = 0;

    virtual void sendSubscribe(SubscribeMessage msg,
                               std::function<void(EventMessage)> callback) = 0;
    virtual void sendUnsubscribe(UnsubscribeMessage msg) = 0;
    virtual void sendEvent(EventMessage msg) = 0;
    virtual void sendToken(TokenMessage msg) = 0;

    virtual void setErrorHandler(ErrorHandler handler) = 0;
    virtual uint64_t nextId() = 0;
};

// -----------------------------------------------------------------------------
// RpcConnection<Stream> — one full-duplex RPC conversation over a Boost.Asio
// stream-like socket (plain TCP or SSL-wrapped TCP, sharing this template).
//
// Roles: the same connection supports both directions. Either peer can
// initiate Call / Methods / Subscribe / Token / Event messages. Provider-side
// dispatch of inbound Call/Methods/Subscribe/Token goes through an
// IncomingCallHandler supplied at construction (may be null for pure-consumer
// connections).
//
// Lifecycle: heap-allocated via std::make_shared; call start() once the
// socket is ready; call stop() (or destroy) to tear down.
// -----------------------------------------------------------------------------
template <typename Stream>
class RpcConnection
    : public RpcConnectionBase
    , public std::enable_shared_from_this<RpcConnection<Stream>>
{
public:
    RpcConnection(Stream stream,
                  std::shared_ptr<IWireCodec> codec,
                  IncomingCallHandler* handler = nullptr);

    void start() override;
    void stop(const std::string& reason = "stopped") override;
    bool isOpen() const override { return !m_stopped.load(); }

    std::future<ResultMessage>        sendCall(CallMessage msg) override;
    std::future<MethodsResultMessage> sendMethods(MethodsMessage msg) override;

    void sendSubscribe(SubscribeMessage msg,
                       std::function<void(EventMessage)> callback) override;
    void sendUnsubscribe(UnsubscribeMessage msg) override;
    void sendEvent(EventMessage msg) override;
    void sendToken(TokenMessage msg) override;

    void setErrorHandler(ErrorHandler handler) override {
        std::lock_guard<std::mutex> g(m_mu);
        m_error = std::move(handler);
    }

    uint64_t nextId() override {
        return m_nextId.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void doRead();
    void handleFrame(MessageType tag, std::vector<uint8_t> payload);
    void dispatchIncoming(AnyMessage msg);
    void writeFrame(std::vector<uint8_t> frame);
    void doWrite();
    void fail(const std::string& reason);

    Stream                                       m_stream;
    std::shared_ptr<IWireCodec>                  m_codec;
    IncomingCallHandler*                         m_handler;
    boost::asio::strand<boost::asio::any_io_executor> m_strand;

    // Read side
    FrameReader                                  m_reader;
    std::vector<uint8_t>                         m_readBuf;

    // Write side
    std::deque<std::vector<uint8_t>>             m_writeQueue;
    bool                                         m_writing = false;

    // Outgoing-pending maps
    std::mutex                                   m_mu;
    std::map<uint64_t, std::shared_ptr<std::promise<ResultMessage>>>        m_pendingCalls;
    std::map<uint64_t, std::shared_ptr<std::promise<MethodsResultMessage>>> m_pendingMethods;

    using EventKey = std::pair<std::string, std::string>; // object, event
    std::map<EventKey, std::function<void(EventMessage)>> m_eventCallbacks;

    ErrorHandler                                 m_error;
    std::atomic<uint64_t>                        m_nextId{1};
    std::atomic<bool>                            m_stopped{false};
    std::atomic<bool>                            m_started{false};
};

// ── Template implementation (must be visible at instantiation sites) ─────

template <typename Stream>
RpcConnection<Stream>::RpcConnection(Stream stream,
                                     std::shared_ptr<IWireCodec> codec,
                                     IncomingCallHandler* handler)
    : m_stream(std::move(stream))
    , m_codec(std::move(codec))
    , m_handler(handler)
    , m_strand(boost::asio::make_strand(m_stream.get_executor()))
{
    m_readBuf.resize(4096);
}

template <typename Stream>
void RpcConnection<Stream>::start()
{
    bool expected = false;
    if (!m_started.compare_exchange_strong(expected, true)) return;
    auto self = this->shared_from_this();
    boost::asio::post(m_strand, [self] { self->doRead(); });
}

template <typename Stream>
void RpcConnection<Stream>::stop(const std::string& reason)
{
    fail(reason);
}

template <typename Stream>
void RpcConnection<Stream>::doRead()
{
    auto self = this->shared_from_this();
    m_stream.async_read_some(boost::asio::buffer(m_readBuf),
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t n) {
                if (ec) { self->fail(ec.message()); return; }
                try {
                    self->m_reader.append(self->m_readBuf.data(), n);
                    MessageType tag;
                    std::vector<uint8_t> payload;
                    while (self->m_reader.next(tag, payload)) {
                        self->handleFrame(tag, std::move(payload));
                    }
                } catch (const std::exception& e) {
                    self->fail(std::string("frame error: ") + e.what());
                    return;
                }
                self->doRead();
            }));
}

template <typename Stream>
void RpcConnection<Stream>::handleFrame(MessageType tag, std::vector<uint8_t> payload)
{
    AnyMessage msg;
    try {
        msg = m_codec->decode(tag, payload.data(), payload.size());
    } catch (const std::exception& e) {
        fail(std::string("decode error: ") + e.what());
        return;
    }
    dispatchIncoming(std::move(msg));
}

template <typename Stream>
void RpcConnection<Stream>::dispatchIncoming(AnyMessage msg)
{
    std::visit([this](auto&& m) {
        using T = std::decay_t<decltype(m)>;

        if constexpr (std::is_same_v<T, ResultMessage>) {
            std::shared_ptr<std::promise<ResultMessage>> p;
            {
                std::lock_guard<std::mutex> g(m_mu);
                auto it = m_pendingCalls.find(m.id);
                if (it != m_pendingCalls.end()) {
                    p = std::move(it->second);
                    m_pendingCalls.erase(it);
                }
            }
            if (p) p->set_value(std::forward<decltype(m)>(m));

        } else if constexpr (std::is_same_v<T, MethodsResultMessage>) {
            std::shared_ptr<std::promise<MethodsResultMessage>> p;
            {
                std::lock_guard<std::mutex> g(m_mu);
                auto it = m_pendingMethods.find(m.id);
                if (it != m_pendingMethods.end()) {
                    p = std::move(it->second);
                    m_pendingMethods.erase(it);
                }
            }
            if (p) p->set_value(std::forward<decltype(m)>(m));

        } else if constexpr (std::is_same_v<T, EventMessage>) {
            std::function<void(EventMessage)> cb;
            std::function<void(EventMessage)> wildcardCb;
            {
                std::lock_guard<std::mutex> g(m_mu);
                auto it = m_eventCallbacks.find({m.object, m.eventName});
                if (it != m_eventCallbacks.end()) cb = it->second;
                auto wit = m_eventCallbacks.find({m.object, std::string{}});
                if (wit != m_eventCallbacks.end()) wildcardCb = wit->second;
            }
            if (cb)         cb(m);
            if (wildcardCb) wildcardCb(m);

        } else if constexpr (std::is_same_v<T, CallMessage>) {
            if (!m_handler) return;
            auto self = this->shared_from_this();
            m_handler->onCall(m, [self](ResultMessage res) {
                self->writeFrame(encodeFrame(*self->m_codec, AnyMessage{std::move(res)}));
            });

        } else if constexpr (std::is_same_v<T, MethodsMessage>) {
            if (!m_handler) return;
            auto self = this->shared_from_this();
            m_handler->onMethods(m, [self](MethodsResultMessage res) {
                self->writeFrame(encodeFrame(*self->m_codec, AnyMessage{std::move(res)}));
            });

        } else if constexpr (std::is_same_v<T, SubscribeMessage>) {
            if (!m_handler) return;
            // weak_ptr capture so the host's stored sink doesn't keep the
            // connection alive past its natural lifetime — without this,
            // `[self]` would leak every subscribed connection until
            // unsubscribe (which a crashing client never sends).
            std::weak_ptr<RpcConnection<Stream>> weak = this->shared_from_this();
            const void* connId = static_cast<const void*>(this);
            m_handler->onSubscribe(m, [weak](EventMessage evt) {
                if (auto self = weak.lock()) self->sendEvent(std::move(evt));
            }, connId);

        } else if constexpr (std::is_same_v<T, UnsubscribeMessage>) {
            if (m_handler)
                m_handler->onUnsubscribe(m, static_cast<const void*>(this));

        } else if constexpr (std::is_same_v<T, TokenMessage>) {
            if (m_handler) m_handler->onToken(m);
        }
    }, std::move(msg));
}

template <typename Stream>
std::future<ResultMessage>
RpcConnection<Stream>::sendCall(CallMessage msg)
{
    auto p = std::make_shared<std::promise<ResultMessage>>();
    auto f = p->get_future();
    if (m_stopped.load()) {
        ResultMessage r;
        r.id = msg.id; r.ok = false;
        r.err = "connection stopped"; r.errCode = "TRANSPORT_CLOSED";
        p->set_value(std::move(r));
        return f;
    }
    {
        std::lock_guard<std::mutex> g(m_mu);
        m_pendingCalls[msg.id] = p;
    }
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
    return f;
}

template <typename Stream>
std::future<MethodsResultMessage>
RpcConnection<Stream>::sendMethods(MethodsMessage msg)
{
    auto p = std::make_shared<std::promise<MethodsResultMessage>>();
    auto f = p->get_future();
    if (m_stopped.load()) {
        MethodsResultMessage r;
        r.id = msg.id; r.ok = false; r.err = "connection stopped";
        p->set_value(std::move(r));
        return f;
    }
    {
        std::lock_guard<std::mutex> g(m_mu);
        m_pendingMethods[msg.id] = p;
    }
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
    return f;
}

template <typename Stream>
void RpcConnection<Stream>::sendSubscribe(SubscribeMessage msg,
                                          std::function<void(EventMessage)> cb)
{
    {
        std::lock_guard<std::mutex> g(m_mu);
        m_eventCallbacks[{msg.object, msg.eventName}] = std::move(cb);
    }
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
}

template <typename Stream>
void RpcConnection<Stream>::sendUnsubscribe(UnsubscribeMessage msg)
{
    {
        std::lock_guard<std::mutex> g(m_mu);
        m_eventCallbacks.erase({msg.object, msg.eventName});
    }
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
}

template <typename Stream>
void RpcConnection<Stream>::sendEvent(EventMessage msg)
{
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
}

template <typename Stream>
void RpcConnection<Stream>::sendToken(TokenMessage msg)
{
    writeFrame(encodeFrame(*m_codec, AnyMessage{std::move(msg)}));
}

template <typename Stream>
void RpcConnection<Stream>::writeFrame(std::vector<uint8_t> frame)
{
    if (m_stopped.load()) return;
    auto self = this->shared_from_this();
    boost::asio::post(m_strand, [self, frame = std::move(frame)]() mutable {
        self->m_writeQueue.push_back(std::move(frame));
        if (!self->m_writing) {
            self->m_writing = true;
            self->doWrite();
        }
    });
}

template <typename Stream>
void RpcConnection<Stream>::doWrite()
{
    auto self = this->shared_from_this();
    boost::asio::async_write(m_stream,
        boost::asio::buffer(m_writeQueue.front()),
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t /*n*/) {
                if (ec) { self->fail(ec.message()); return; }
                self->m_writeQueue.pop_front();
                if (self->m_writeQueue.empty()) {
                    self->m_writing = false;
                } else {
                    self->doWrite();
                }
            }));
}

template <typename Stream>
void RpcConnection<Stream>::fail(const std::string& reason)
{
    bool expected = false;
    if (!m_stopped.compare_exchange_strong(expected, true)) return;

    // Fail every pending promise with a transport-level error.
    std::map<uint64_t, std::shared_ptr<std::promise<ResultMessage>>>        calls;
    std::map<uint64_t, std::shared_ptr<std::promise<MethodsResultMessage>>> methods;
    ErrorHandler errCb;
    {
        std::lock_guard<std::mutex> g(m_mu);
        calls.swap(m_pendingCalls);
        methods.swap(m_pendingMethods);
        errCb.swap(m_error);
        m_eventCallbacks.clear();
    }
    for (auto& [id, p] : calls) {
        ResultMessage r; r.id = id; r.ok = false;
        r.err = reason; r.errCode = "TRANSPORT_ERROR";
        try { p->set_value(std::move(r)); } catch (...) {}
    }
    for (auto& [id, p] : methods) {
        MethodsResultMessage r; r.id = id; r.ok = false; r.err = reason;
        try { p->set_value(std::move(r)); } catch (...) {}
    }

    boost::system::error_code ignore;
    try {
        // lowest_layer() works for plain asio::ip::tcp::socket (returns
        // itself) and for asio::ssl::stream (returns the underlying TCP
        // socket). Closing the lowest layer tears the stack down cleanly
        // without needing protocol-specific shutdown sequences.
        m_stream.lowest_layer().close(ignore);
    } catch (...) {}

    // Notify the dispatch handler so it can drop any subscriptions still
    // keyed to this connection. Without this, a connection that drops
    // without sending Unsubscribe leaks sinks in the host's per-event map.
    if (m_handler) {
        try { m_handler->onConnectionClosed(static_cast<const void*>(this)); }
        catch (...) {}
    }

    if (errCb) errCb(reason);
}

} // namespace logos::plain

#endif // LOGOS_PLAIN_RPC_CONNECTION_H
