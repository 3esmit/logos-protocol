#include "logos_types.h"

// All three fields. The `error` field used to be dropped on the wire —
// senders set it, receivers got a default-constructed (null) QVariant —
// so any failed LogosResult looked like a success path with no explanation.
// Daemon and modules always build from the same SDK (they ship together in
// the same process group) so extending the wire format is safe.
QDataStream& operator<<(QDataStream& out, const LogosResult& result) {
    out << result.success << result.value << result.error;
    return out;
}

QDataStream& operator>>(QDataStream& in, LogosResult& result) {
    in >> result.success >> result.value >> result.error;
    return in;
}