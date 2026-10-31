#include "test-framework.h"

#include <string>
#include <unordered_set>
#include <vector>

#include "bridge/crypto.h"

void RunCryptoTests(Test::Context& context) {
    context.Begin("crypto");
    context.Check(Flare::Crypto::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc vector");
    context.Check(Flare::Crypto::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty vector");
    context.Check(Flare::Crypto::Sha256Hex("The quick brown fox jumps over the lazy dog")
            == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
        "SHA-256 fox vector");

    const auto hmac = Flare::Crypto::HmacSha256("key", "The quick brown fox jumps over the lazy dog");
    context.Check(Flare::Crypto::ToHex(hmac) == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
        "HMAC-SHA256 vector");
    const auto longKeyHmac = Flare::Crypto::HmacSha256(std::string(100, 'k'), "message");
    context.Check(longKeyHmac.size() == 32, "HMAC handles long keys");

    const std::vector<std::uint8_t> bytes{0, 1, 2, 250, 251, 252, 253, 254, 255};
    const std::string encoded = Flare::Crypto::Base64UrlEncode(bytes);
    context.Check(
        encoded.find('+') == std::string::npos && encoded.find('/') == std::string::npos, "base64url alphabet");
    const auto decoded = Flare::Crypto::Base64UrlDecode(encoded);
    context.Check(decoded.has_value() && *decoded == bytes, "base64url round trip");
    context.Check(!Flare::Crypto::Base64UrlDecode("!!!!").has_value(), "invalid base64url rejected");

    context.Check(Flare::Crypto::ConstantTimeEquals("abcdef", "abcdef"), "constant time equal");
    context.Check(!Flare::Crypto::ConstantTimeEquals("abcdef", "abcdeg"), "constant time unequal");
    context.Check(!Flare::Crypto::ConstantTimeEquals("abc", "abcd"), "constant time length mismatch");

    std::unordered_set<std::string> tokens;
    for (int index = 0; index < 64; ++index) {
        tokens.insert(Flare::Crypto::RandomToken(32));
    }
    context.Check(tokens.size() == 64, "random tokens are unique");
    context.Check(Flare::Crypto::RandomToken(32).size() == 43, "random token length");

    context.Check(Flare::Crypto::IsHex("deadBEEF"), "hex detection");
    context.Check(!Flare::Crypto::IsHex("xyz"), "hex rejection");
}
