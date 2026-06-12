#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include "token_manager.h"

class TokenManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        TokenManager::instance().clearAllTokens();
    }
};

TEST_F(TokenManagerTest, SaveAndGetToken)
{
    TokenManager::instance().saveToken("mod_a", "token_a");
    EXPECT_EQ(TokenManager::instance().getToken("mod_a"), "token_a");
}

TEST_F(TokenManagerTest, GetMissingTokenReturnsEmpty)
{
    EXPECT_TRUE(TokenManager::instance().getToken("nonexistent").isEmpty());
}

TEST_F(TokenManagerTest, HasToken)
{
    EXPECT_FALSE(TokenManager::instance().hasToken("key"));
    TokenManager::instance().saveToken("key", "val");
    EXPECT_TRUE(TokenManager::instance().hasToken("key"));
}

TEST_F(TokenManagerTest, RemoveToken)
{
    TokenManager::instance().saveToken("key", "val");
    EXPECT_TRUE(TokenManager::instance().removeToken("key"));
    EXPECT_FALSE(TokenManager::instance().hasToken("key"));
}

TEST_F(TokenManagerTest, RemoveNonexistentTokenReturnsFalse)
{
    EXPECT_FALSE(TokenManager::instance().removeToken("missing"));
}

TEST_F(TokenManagerTest, ClearAllTokens)
{
    TokenManager::instance().saveToken("a", "1");
    TokenManager::instance().saveToken("b", "2");
    TokenManager::instance().clearAllTokens();
    EXPECT_EQ(TokenManager::instance().tokenCount(), 0);
}

TEST_F(TokenManagerTest, TokenCount)
{
    EXPECT_EQ(TokenManager::instance().tokenCount(), 0);
    TokenManager::instance().saveToken("x", "1");
    EXPECT_EQ(TokenManager::instance().tokenCount(), 1);
    TokenManager::instance().saveToken("y", "2");
    EXPECT_EQ(TokenManager::instance().tokenCount(), 2);
}

TEST_F(TokenManagerTest, GetTokenKeys)
{
    TokenManager::instance().saveToken("alpha", "1");
    TokenManager::instance().saveToken("beta", "2");
    QList<QString> keys = TokenManager::instance().getTokenKeys();
    EXPECT_EQ(keys.size(), 2);
    EXPECT_TRUE(keys.contains("alpha"));
    EXPECT_TRUE(keys.contains("beta"));
}

TEST_F(TokenManagerTest, OverwriteToken)
{
    TokenManager::instance().saveToken("key", "old");
    TokenManager::instance().saveToken("key", "new");
    EXPECT_EQ(TokenManager::instance().getToken("key"), "new");
    EXPECT_EQ(TokenManager::instance().tokenCount(), 1);
}

TEST_F(TokenManagerTest, SignalTokenSaved)
{
    QSignalSpy spy(&TokenManager::instance(), &TokenManager::tokenSaved);
    TokenManager::instance().saveToken("k", "v");
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "k");
}

TEST_F(TokenManagerTest, SignalTokenRemoved)
{
    TokenManager::instance().saveToken("k", "v");
    QSignalSpy spy(&TokenManager::instance(), &TokenManager::tokenRemoved);
    TokenManager::instance().removeToken("k");
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "k");
}

TEST_F(TokenManagerTest, SignalAllTokensCleared)
{
    TokenManager::instance().saveToken("k", "v");
    QSignalSpy spy(&TokenManager::instance(), &TokenManager::allTokensCleared);
    TokenManager::instance().clearAllTokens();
    EXPECT_EQ(spy.count(), 1);
}

// --- redactToken: log-safe token fingerprinting (F-007) ---
//
// Tokens gate all cross-module RPC and are accepted by value, so a raw token
// recovered from a log line is directly replayable. redactToken() is the
// guard that keeps the cleartext secret out of logs; these tests pin the
// security-relevant properties: the raw value never appears, the output is
// stable for correlation, and distinct tokens map to distinct fingerprints.

TEST(RedactTokenTest, NeverContainsRawTokenValue)
{
    const QString secret = "3f2a9c00-dead-beef-cafe-0123456789ab";
    const QString redacted = redactToken(secret);
    EXPECT_FALSE(redacted.contains(secret));
    // A replayer must not be able to recover the secret from any substring:
    // the redaction is a one-way hash, so the cleartext is absent entirely.
    EXPECT_EQ(redacted.indexOf(secret), -1);
}

TEST(RedactTokenTest, EmptyTokenRendersAsNone)
{
    EXPECT_EQ(redactToken(QString()), QStringLiteral("<none>"));
    EXPECT_EQ(redactToken(""), QStringLiteral("<none>"));
}

TEST(RedactTokenTest, IsDeterministicForCorrelation)
{
    // Same token must always fingerprint identically so operators can still
    // correlate log lines referring to the same credential.
    const QString token = "some-capability-token";
    EXPECT_EQ(redactToken(token), redactToken(token));
}

TEST(RedactTokenTest, DistinctTokensProduceDistinctFingerprints)
{
    EXPECT_NE(redactToken("token-one"), redactToken("token-two"));
}

TEST(RedactTokenTest, HasStableFingerprintShape)
{
    // Fixed prefix + 8 hex chars of SHA-256 + ellipsis, e.g. "redacted:1a2b3c4d…".
    const QString redacted = redactToken("abc");
    EXPECT_TRUE(redacted.startsWith(QStringLiteral("redacted:")));
    EXPECT_TRUE(redacted.endsWith(QStringLiteral("…")));
    // SHA-256("abc") = ba7816bf8f01cfea... → first 8 hex chars are "ba7816bf".
    EXPECT_EQ(redacted, QStringLiteral("redacted:ba7816bf…"));
}
