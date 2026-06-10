#include "rpc_message.h"

#include <type_traits>

namespace logos::plain {

MessageType messageTypeOf(const AnyMessage& m)
{
    return std::visit([](const auto& v) -> MessageType {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, CallMessage>)          return MessageType::Call;
        else if constexpr (std::is_same_v<T, ResultMessage>)   return MessageType::Result;
        else if constexpr (std::is_same_v<T, SubscribeMessage>)return MessageType::Subscribe;
        else if constexpr (std::is_same_v<T, UnsubscribeMessage>) return MessageType::Unsubscribe;
        else if constexpr (std::is_same_v<T, EventMessage>)    return MessageType::Event;
        else if constexpr (std::is_same_v<T, TokenMessage>)    return MessageType::Token;
        else if constexpr (std::is_same_v<T, MethodsMessage>)  return MessageType::Methods;
        else if constexpr (std::is_same_v<T, MethodsResultMessage>) return MessageType::MethodsResult;
    }, m);
}

} // namespace logos::plain
