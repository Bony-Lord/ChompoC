#include "secure_channel.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace {
constexpr std::size_t KeySize = 32;
constexpr std::size_t SaltSize = 16;
constexpr std::size_t NoncePrefixSize = 4;
constexpr std::size_t NonceSize = 12;
constexpr std::size_t TagSize = 16;
constexpr std::uint32_t Pbkdf2Iterations = 210000;
constexpr std::string_view HelloMarker = "CHOMPO-SECURE-2";
constexpr std::string_view KeyMarker = "CHOMPO-KEY-2";
constexpr std::string_view FrameMarker = "SEC2";
constexpr std::string_view ClientToServer = "C2S";
constexpr std::string_view ServerToClient = "S2C";

using Key = std::array<std::uint8_t, KeySize>;
using Salt = std::array<std::uint8_t, SaltSize>;
using NoncePrefix = std::array<std::uint8_t, NoncePrefixSize>;
using Nonce = std::array<std::uint8_t, NonceSize>;
using Tag = std::array<std::uint8_t, TagSize>;

struct SealedData {
    std::vector<std::uint8_t> ciphertext;
    Tag tag{};
};

std::string hex_encode(std::span<const std::uint8_t> bytes) {
    static constexpr char Digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = Digits[bytes[index] >> 4U];
        result[index * 2 + 1] = Digits[bytes[index] & 0x0fU];
    }
    return result;
}

int hex_digit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> hex_decode(std::string_view text, std::string_view description) {
    if (text == "-")
        return {};
    if (text.size() % 2 != 0)
        throw std::runtime_error(std::string(description) + " has an odd hexadecimal length");

    std::vector<std::uint8_t> result(text.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = hex_digit(text[index * 2]);
        const int low = hex_digit(text[index * 2 + 1]);
        if (high < 0 || low < 0)
            throw std::runtime_error(std::string(description) + " contains a non-hexadecimal character");
        result[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return result;
}

template <std::size_t Size>
std::array<std::uint8_t, Size> hex_decode_array(std::string_view text, std::string_view description) {
    const std::vector<std::uint8_t> decoded = hex_decode(text, description);
    if (decoded.size() != Size) {
        throw std::runtime_error(std::string(description) + " must contain exactly " +
                                 std::to_string(Size) + " bytes");
    }

    std::array<std::uint8_t, Size> result{};
    std::copy(decoded.begin(), decoded.end(), result.begin());
    return result;
}

std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && line[offset] == ' ')
            ++offset;
        if (offset == line.size())
            break;
        const std::size_t end = line.find(' ', offset);
        if (end == std::string_view::npos) {
            fields.push_back(line.substr(offset));
            break;
        }
        fields.push_back(line.substr(offset, end - offset));
        offset = end + 1;
    }
    return fields;
}

std::string counter_hex(std::uint64_t counter) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << counter;
    return stream.str();
}

std::uint64_t parse_counter(std::string_view text) {
    if (text.size() != 16)
        throw std::runtime_error("secure frame counter must contain 16 hexadecimal characters");
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("secure frame counter is invalid");
    return value;
}

std::uint32_t parse_iterations(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc{} || end != text.data() + text.size() || value < 100000 || value > 2000000)
        throw std::runtime_error("secure handshake PBKDF2 iteration count is invalid");
    return value;
}

Nonce make_nonce(const NoncePrefix &prefix, std::uint64_t counter) {
    Nonce nonce{};
    std::copy(prefix.begin(), prefix.end(), nonce.begin());
    for (std::size_t index = 0; index < 8; ++index) {
        nonce[NoncePrefixSize + index] =
            static_cast<std::uint8_t>(counter >> static_cast<unsigned int>((7 - index) * 8));
    }
    return nonce;
}

std::string make_aad(std::string_view direction, std::uint64_t counter) {
    return "ChompoChat/2|" + std::string(direction) + "|" + counter_hex(counter);
}

#ifdef _WIN32
[[noreturn]] void throw_bcrypt(std::string_view operation, NTSTATUS status) {
    std::ostringstream stream;
    stream << operation << " failed with NTSTATUS 0x" << std::hex
           << static_cast<std::uint32_t>(status);
    throw std::runtime_error(stream.str());
}

void require_bcrypt(NTSTATUS status, std::string_view operation) {
    if (status < 0)
        throw_bcrypt(operation, status);
}

class AlgorithmHandle {
public:
    AlgorithmHandle(LPCWSTR algorithm, ULONG flags = 0) {
        require_bcrypt(BCryptOpenAlgorithmProvider(&handle_, algorithm, nullptr, flags),
                       "BCryptOpenAlgorithmProvider");
    }

    ~AlgorithmHandle() {
        if (handle_)
            BCryptCloseAlgorithmProvider(handle_, 0);
    }

    AlgorithmHandle(const AlgorithmHandle &) = delete;
    AlgorithmHandle &operator=(const AlgorithmHandle &) = delete;

    BCRYPT_ALG_HANDLE get() const { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class KeyHandle {
public:
    explicit KeyHandle(BCRYPT_KEY_HANDLE handle) : handle_(handle) {}
    ~KeyHandle() {
        if (handle_)
            BCryptDestroyKey(handle_);
    }

    KeyHandle(const KeyHandle &) = delete;
    KeyHandle &operator=(const KeyHandle &) = delete;

    BCRYPT_KEY_HANDLE get() const { return handle_; }

private:
    BCRYPT_KEY_HANDLE handle_ = nullptr;
};

void random_bytes(std::span<std::uint8_t> destination) {
    if (destination.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("random byte request is too large");
    require_bcrypt(BCryptGenRandom(nullptr, destination.data(), static_cast<ULONG>(destination.size()),
                                   BCRYPT_USE_SYSTEM_PREFERRED_RNG),
                   "BCryptGenRandom");
}

Key derive_key(std::string_view password, const Salt &salt, std::uint32_t iterations) {
    if (password.empty())
        throw std::runtime_error("secure channel password cannot be empty");
    if (password.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("secure channel password is too large");

    AlgorithmHandle algorithm(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    Key result{};
    require_bcrypt(
        BCryptDeriveKeyPBKDF2(algorithm.get(),
                              reinterpret_cast<PUCHAR>(const_cast<char *>(password.data())),
                              static_cast<ULONG>(password.size()),
                              const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations,
                              result.data(), static_cast<ULONG>(result.size()), 0),
        "BCryptDeriveKeyPBKDF2");
    return result;
}

KeyHandle create_aes_key(AlgorithmHandle &algorithm, const Key &key, std::vector<std::uint8_t> &key_object) {
    require_bcrypt(BCryptSetProperty(algorithm.get(), BCRYPT_CHAINING_MODE,
                                     reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                     sizeof(BCRYPT_CHAIN_MODE_GCM), 0),
                   "BCryptSetProperty(BCRYPT_CHAIN_MODE_GCM)");

    DWORD object_size = 0;
    DWORD copied = 0;
    require_bcrypt(BCryptGetProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH,
                                     reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &copied, 0),
                   "BCryptGetProperty(BCRYPT_OBJECT_LENGTH)");
    key_object.resize(object_size);

    BCRYPT_KEY_HANDLE key_handle = nullptr;
    require_bcrypt(BCryptGenerateSymmetricKey(algorithm.get(), &key_handle, key_object.data(), object_size,
                                              const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0),
                   "BCryptGenerateSymmetricKey");
    return KeyHandle(key_handle);
}

SealedData encrypt_aes_gcm(const Key &key, const Nonce &nonce, std::string_view aad,
                           std::string_view plaintext) {
    if (plaintext.size() > std::numeric_limits<ULONG>::max() || aad.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("secure frame is too large");

    AlgorithmHandle algorithm(BCRYPT_AES_ALGORITHM);
    std::vector<std::uint8_t> key_object;
    KeyHandle key_handle = create_aes_key(algorithm, key, key_object);

    SealedData result;
    result.ciphertext.resize(plaintext.size());

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
    auth_info.pbNonce = const_cast<PUCHAR>(nonce.data());
    auth_info.cbNonce = static_cast<ULONG>(nonce.size());
    auth_info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.data()));
    auth_info.cbAuthData = static_cast<ULONG>(aad.size());
    auth_info.pbTag = result.tag.data();
    auth_info.cbTag = static_cast<ULONG>(result.tag.size());

    ULONG written = 0;
    require_bcrypt(BCryptEncrypt(key_handle.get(),
                                 reinterpret_cast<PUCHAR>(const_cast<char *>(plaintext.data())),
                                 static_cast<ULONG>(plaintext.size()), &auth_info, nullptr, 0,
                                 result.ciphertext.empty() ? nullptr : result.ciphertext.data(),
                                 static_cast<ULONG>(result.ciphertext.size()), &written, 0),
                   "BCryptEncrypt(AES-GCM)");
    result.ciphertext.resize(written);
    return result;
}

std::string decrypt_aes_gcm(const Key &key, const Nonce &nonce, std::string_view aad,
                            std::span<const std::uint8_t> ciphertext, const Tag &tag) {
    if (ciphertext.size() > std::numeric_limits<ULONG>::max() || aad.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("secure frame is too large");

    AlgorithmHandle algorithm(BCRYPT_AES_ALGORITHM);
    std::vector<std::uint8_t> key_object;
    KeyHandle key_handle = create_aes_key(algorithm, key, key_object);

    std::string plaintext(ciphertext.size(), '\0');
    Tag mutable_tag = tag;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
    auth_info.pbNonce = const_cast<PUCHAR>(nonce.data());
    auth_info.cbNonce = static_cast<ULONG>(nonce.size());
    auth_info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.data()));
    auth_info.cbAuthData = static_cast<ULONG>(aad.size());
    auth_info.pbTag = mutable_tag.data();
    auth_info.cbTag = static_cast<ULONG>(mutable_tag.size());

    ULONG written = 0;
    const NTSTATUS status = BCryptDecrypt(
        key_handle.get(), const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()), &auth_info,
        nullptr, 0, plaintext.empty() ? nullptr : reinterpret_cast<PUCHAR>(plaintext.data()),
        static_cast<ULONG>(plaintext.size()), &written, 0);
    if (status < 0)
        throw std::runtime_error("secure frame authentication failed");
    plaintext.resize(written);
    return plaintext;
}
#else
void random_bytes(std::span<std::uint8_t> destination) {
    if (destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("random byte request is too large");
    if (RAND_bytes(destination.data(), static_cast<int>(destination.size())) != 1)
        throw std::runtime_error("OpenSSL RAND_bytes failed");
}

Key derive_key(std::string_view password, const Salt &salt, std::uint32_t iterations) {
    if (password.empty())
        throw std::runtime_error("secure channel password cannot be empty");
    if (password.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("secure channel password is too large");

    Key result{};
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), static_cast<int>(iterations), EVP_sha256(),
                          static_cast<int>(result.size()), result.data()) != 1) {
        throw std::runtime_error("OpenSSL PBKDF2-HMAC-SHA256 failed");
    }
    return result;
}

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

SealedData encrypt_aes_gcm(const Key &key, const Nonce &nonce, std::string_view aad,
                           std::string_view plaintext) {
    if (plaintext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        aad.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("secure frame is too large");
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context)
        throw std::runtime_error("OpenSSL failed to allocate an AES-GCM context");

    if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("OpenSSL failed to initialize AES-256-GCM encryption");
    }

    int written = 0;
    if (!aad.empty() && EVP_EncryptUpdate(context.get(), nullptr, &written,
                                           reinterpret_cast<const unsigned char *>(aad.data()),
                                           static_cast<int>(aad.size())) != 1) {
        throw std::runtime_error("OpenSSL failed to authenticate secure frame metadata");
    }

    SealedData result;
    result.ciphertext.resize(plaintext.size() + 16);
    int total = 0;
    if (!plaintext.empty() &&
        EVP_EncryptUpdate(context.get(), result.ciphertext.data(), &written,
                          reinterpret_cast<const unsigned char *>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-256-GCM encryption failed");
    }
    total += written;

    if (EVP_EncryptFinal_ex(context.get(), result.ciphertext.data() + total, &written) != 1)
        throw std::runtime_error("OpenSSL AES-256-GCM finalization failed");
    total += written;
    result.ciphertext.resize(static_cast<std::size_t>(total));

    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(result.tag.size()),
                            result.tag.data()) != 1) {
        throw std::runtime_error("OpenSSL failed to obtain the AES-GCM authentication tag");
    }
    return result;
}

std::string decrypt_aes_gcm(const Key &key, const Nonce &nonce, std::string_view aad,
                            std::span<const std::uint8_t> ciphertext, const Tag &tag) {
    if (ciphertext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        aad.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("secure frame is too large");
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context)
        throw std::runtime_error("OpenSSL failed to allocate an AES-GCM context");

    if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("OpenSSL failed to initialize AES-256-GCM decryption");
    }

    int written = 0;
    if (!aad.empty() && EVP_DecryptUpdate(context.get(), nullptr, &written,
                                           reinterpret_cast<const unsigned char *>(aad.data()),
                                           static_cast<int>(aad.size())) != 1) {
        throw std::runtime_error("OpenSSL failed to authenticate secure frame metadata");
    }

    std::string plaintext(ciphertext.size(), '\0');
    int total = 0;
    if (!ciphertext.empty() &&
        EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()), &written,
                          ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-256-GCM decryption failed");
    }
    total += written;

    Tag mutable_tag = tag;
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(mutable_tag.size()),
                            mutable_tag.data()) != 1) {
        throw std::runtime_error("OpenSSL failed to set the AES-GCM authentication tag");
    }

    if (EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()) + total,
                            &written) != 1) {
        throw std::runtime_error("secure frame authentication failed");
    }
    total += written;
    plaintext.resize(static_cast<std::size_t>(total));
    return plaintext;
}
#endif

void send_all(NetworkManager &network, NetworkManager::Handle socket, std::string_view data, int timeout_ms) {
    if (timeout_ms < -1)
        throw std::runtime_error("secure send timeout must be -1 or non-negative");

    const auto started = std::chrono::steady_clock::now();
    std::size_t total = 0;
    while (total < data.size()) {
        total += network.send(socket, data.substr(total));
        if (total == data.size())
            return;

        if (timeout_ms >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            if (elapsed.count() >= timeout_ms)
                throw std::runtime_error("secure send timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}

struct SecureChannelManager::Impl {
    enum class HandshakeRole {
        Client,
        Server
    };

    enum class HandshakePhase {
        // Client waits for CHOMPO-SECURE-2 hello, then sends KEY, then waits for READY.
        WaitServerHello,
        WaitServerReady,
        // Server has sent hello and waits for CHOMPO-KEY-2.
        WaitClientKey
    };

    struct Session {
        Key key{};
        NoncePrefix send_prefix{};
        NoncePrefix receive_prefix{};
        std::string send_direction;
        std::string receive_direction;
        std::uint64_t send_counter = 0;
        std::uint64_t receive_counter = 0;
    };

    struct PendingHandshake {
        HandshakeRole role = HandshakeRole::Client;
        HandshakePhase phase = HandshakePhase::WaitServerHello;
        std::string password;
        Salt salt{};
        NoncePrefix server_prefix{};
        Session session{};
    };

    explicit Impl(NetworkManager &network_manager) : network(network_manager) {}

    NetworkManager &network;
    std::unordered_map<NetworkManager::Handle, Session> sessions;
    std::unordered_map<NetworkManager::Handle, PendingHandshake> pending;

    void clear_socket(NetworkManager::Handle socket) noexcept {
        sessions.erase(socket);
        pending.erase(socket);
    }

    // Seal with an explicit counter; does not mutate session counters.
    static std::string seal_at(const Session &session, std::string_view plaintext, std::uint64_t counter) {
        if (counter == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("secure channel send counter exhausted");

        const Nonce nonce = make_nonce(session.send_prefix, counter);
        const std::string aad = make_aad(session.send_direction, counter);
        const SealedData sealed = encrypt_aes_gcm(session.key, nonce, aad, plaintext);
        const std::string ciphertext = sealed.ciphertext.empty() ? "-" : hex_encode(sealed.ciphertext);
        return std::string(FrameMarker) + " " + counter_hex(counter) + " " + ciphertext + " " +
               hex_encode(sealed.tag);
    }

    static std::string open(Session &session, std::string_view frame) {
        const std::vector<std::string_view> fields = split_fields(frame);
        if (fields.size() != 4 || fields[0] != FrameMarker)
            throw std::runtime_error("received an invalid secure frame");

        const std::uint64_t counter = parse_counter(fields[1]);
        if (counter != session.receive_counter)
            throw std::runtime_error("secure frame is replayed or out of order");
        if (session.receive_counter == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("secure channel receive counter exhausted");

        const std::vector<std::uint8_t> ciphertext = hex_decode(fields[2], "secure frame ciphertext");
        const Tag tag = hex_decode_array<TagSize>(fields[3], "secure frame authentication tag");
        const Nonce nonce = make_nonce(session.receive_prefix, counter);
        const std::string aad = make_aad(session.receive_direction, counter);
        std::string plaintext = decrypt_aes_gcm(session.key, nonce, aad, ciphertext, tag);
        ++session.receive_counter;
        return plaintext;
    }

    void promote(NetworkManager::Handle socket, Session session) {
        pending.erase(socket);
        sessions.insert_or_assign(socket, std::move(session));
    }
};

SecureChannelManager::SecureChannelManager(NetworkManager &network_manager)
    : impl_(std::make_unique<Impl>(network_manager)) {}

SecureChannelManager::~SecureChannelManager() = default;

void SecureChannelManager::begin_client_handshake(NetworkManager::Handle socket, std::string_view password) {
    if (password.empty())
        throw std::runtime_error("secure channel password cannot be empty");

    impl_->clear_socket(socket);

    Impl::PendingHandshake pending;
    pending.role = Impl::HandshakeRole::Client;
    pending.phase = Impl::HandshakePhase::WaitServerHello;
    pending.password = std::string(password);
    impl_->pending.insert_or_assign(socket, std::move(pending));
}

void SecureChannelManager::begin_server_handshake(NetworkManager::Handle socket, std::string_view password) {
    if (password.empty())
        throw std::runtime_error("secure channel password cannot be empty");

    impl_->clear_socket(socket);

    Salt salt{};
    NoncePrefix server_prefix{};
    random_bytes(salt);
    random_bytes(server_prefix);

    const std::string hello = std::string(HelloMarker) + " " + hex_encode(salt) + " " +
                              hex_encode(server_prefix) + " " + std::to_string(Pbkdf2Iterations) + "\n";
    // Short timeout: HELLO is tiny; if the send buffer is full something is very wrong.
    send_all(impl_->network, socket, hello, 2000);

    Impl::PendingHandshake pending;
    pending.role = Impl::HandshakeRole::Server;
    pending.phase = Impl::HandshakePhase::WaitClientKey;
    pending.password = std::string(password);
    pending.salt = salt;
    pending.server_prefix = server_prefix;
    impl_->pending.insert_or_assign(socket, std::move(pending));
}

SecureChannelManager::StepResult SecureChannelManager::step_client_handshake(NetworkManager::Handle socket) {
    const auto pending_it = impl_->pending.find(socket);
    if (pending_it == impl_->pending.end()) {
        if (impl_->sessions.contains(socket))
            return {StepStatus::Complete, {}};
        return {StepStatus::Failed, "no pending client secure handshake"};
    }

    Impl::PendingHandshake &pending = pending_it->second;
    if (pending.role != Impl::HandshakeRole::Client)
        return {StepStatus::Failed, "socket is not a client handshake"};

    try {
        if (pending.phase == Impl::HandshakePhase::WaitServerHello) {
            NetworkManager::ReceiveResult result = impl_->network.receive_line(socket);
            if (result.status == NetworkManager::ReceiveStatus::Wait)
                return {StepStatus::Pending, {}};
            if (result.status == NetworkManager::ReceiveStatus::Closed) {
                impl_->clear_socket(socket);
                return {StepStatus::Failed, "connection closed during secure client handshake"};
            }

            const std::vector<std::string_view> hello_fields = split_fields(result.data);
            if (hello_fields.size() != 4 || hello_fields[0] != HelloMarker) {
                impl_->clear_socket(socket);
                return {StepStatus::Failed, "server does not support the Chompo secure chat protocol"};
            }

            const Salt salt = hex_decode_array<SaltSize>(hello_fields[1], "secure handshake salt");
            const NoncePrefix server_prefix =
                hex_decode_array<NoncePrefixSize>(hello_fields[2], "secure server nonce prefix");
            const std::uint32_t iterations = parse_iterations(hello_fields[3]);

            pending.session.key = derive_key(pending.password, salt, iterations);
            random_bytes(pending.session.send_prefix);
            pending.session.receive_prefix = server_prefix;
            pending.session.send_direction = std::string(ClientToServer);
            pending.session.receive_direction = std::string(ServerToClient);
            pending.session.send_counter = 0;
            pending.session.receive_counter = 0;

            // AUTH is sealed at counter 0; commit counter after the KEY frame is fully sent.
            const std::string authentication = Impl::seal_at(pending.session, "AUTH", 0);
            const std::vector<std::string_view> auth_fields = split_fields(authentication);
            const std::string key_message = std::string(KeyMarker) + " " +
                                            hex_encode(pending.session.send_prefix) + " " +
                                            std::string(auth_fields[1]) + " " +
                                            std::string(auth_fields[2]) + " " +
                                            std::string(auth_fields[3]) + "\n";
            send_all(impl_->network, socket, key_message, 2000);
            pending.session.send_counter = 1;
            pending.phase = Impl::HandshakePhase::WaitServerReady;
            return {StepStatus::Pending, {}};
        }

        if (pending.phase == Impl::HandshakePhase::WaitServerReady) {
            NetworkManager::ReceiveResult result = impl_->network.receive_line(socket);
            if (result.status == NetworkManager::ReceiveStatus::Wait)
                return {StepStatus::Pending, {}};
            if (result.status == NetworkManager::ReceiveStatus::Closed) {
                impl_->clear_socket(socket);
                return {StepStatus::Failed, "connection closed while waiting for secure server proof"};
            }

            if (Impl::open(pending.session, result.data) != "READY") {
                impl_->clear_socket(socket);
                return {StepStatus::Failed, "server returned an invalid secure handshake proof"};
            }

            Impl::Session session = std::move(pending.session);
            impl_->promote(socket, std::move(session));
            return {StepStatus::Complete, {}};
        }

        impl_->clear_socket(socket);
        return {StepStatus::Failed, "invalid client handshake phase"};
    } catch (const std::exception &exception) {
        impl_->clear_socket(socket);
        return {StepStatus::Failed, exception.what()};
    }
}

SecureChannelManager::StepResult SecureChannelManager::step_server_handshake(NetworkManager::Handle socket) {
    const auto pending_it = impl_->pending.find(socket);
    if (pending_it == impl_->pending.end()) {
        if (impl_->sessions.contains(socket))
            return {StepStatus::Complete, {}};
        return {StepStatus::Failed, "no pending server secure handshake"};
    }

    Impl::PendingHandshake &pending = pending_it->second;
    if (pending.role != Impl::HandshakeRole::Server)
        return {StepStatus::Failed, "socket is not a server handshake"};

    try {
        if (pending.phase != Impl::HandshakePhase::WaitClientKey) {
            impl_->clear_socket(socket);
            return {StepStatus::Failed, "invalid server handshake phase"};
        }

        NetworkManager::ReceiveResult result = impl_->network.receive_line(socket);
        if (result.status == NetworkManager::ReceiveStatus::Wait)
            return {StepStatus::Pending, {}};
        if (result.status == NetworkManager::ReceiveStatus::Closed) {
            impl_->clear_socket(socket);
            return {StepStatus::Failed, "connection closed during secure server handshake"};
        }

        const std::vector<std::string_view> key_fields = split_fields(result.data);
        if (key_fields.size() != 5 || key_fields[0] != KeyMarker) {
            impl_->clear_socket(socket);
            return {StepStatus::Failed, "client sent an invalid secure handshake response"};
        }

        Impl::Session session;
        session.key = derive_key(pending.password, pending.salt, Pbkdf2Iterations);
        session.send_prefix = pending.server_prefix;
        session.receive_prefix =
            hex_decode_array<NoncePrefixSize>(key_fields[1], "secure client nonce prefix");
        session.send_direction = std::string(ServerToClient);
        session.receive_direction = std::string(ClientToServer);
        session.send_counter = 0;
        session.receive_counter = 0;

        const std::string authentication = std::string(FrameMarker) + " " + std::string(key_fields[2]) + " " +
                                           std::string(key_fields[3]) + " " + std::string(key_fields[4]);
        if (Impl::open(session, authentication) != "AUTH") {
            impl_->clear_socket(socket);
            return {StepStatus::Failed, "client returned an invalid secure handshake proof"};
        }

        const std::string ready = Impl::seal_at(session, "READY", session.send_counter) + "\n";
        send_all(impl_->network, socket, ready, 2000);
        ++session.send_counter;
        impl_->promote(socket, std::move(session));
        return {StepStatus::Complete, {}};
    } catch (const std::exception &exception) {
        impl_->clear_socket(socket);
        return {StepStatus::Failed, exception.what()};
    }
}

void SecureChannelManager::client_handshake(NetworkManager::Handle socket, std::string_view password,
                                            int timeout_ms) {
    begin_client_handshake(socket, password);

    if (timeout_ms < -1)
        throw std::runtime_error("secure handshake timeout must be -1 or non-negative");

    const auto started = std::chrono::steady_clock::now();
    while (true) {
        StepResult result = step_client_handshake(socket);
        if (result.status == StepStatus::Complete)
            return;
        if (result.status == StepStatus::Failed)
            throw std::runtime_error(result.detail.empty() ? "secure handshake failed" : result.detail);

        int wait_ms = 50;
        if (timeout_ms >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            const long long remaining = static_cast<long long>(timeout_ms) - elapsed.count();
            if (remaining <= 0) {
                forget(socket);
                throw std::runtime_error("secure handshake timed out");
            }
            wait_ms = static_cast<int>(std::min<long long>(remaining, wait_ms));
        }
        impl_->network.poll({socket}, wait_ms);
    }
}

void SecureChannelManager::server_handshake(NetworkManager::Handle socket, std::string_view password,
                                            int timeout_ms) {
    begin_server_handshake(socket, password);

    if (timeout_ms < -1)
        throw std::runtime_error("secure handshake timeout must be -1 or non-negative");

    const auto started = std::chrono::steady_clock::now();
    while (true) {
        StepResult result = step_server_handshake(socket);
        if (result.status == StepStatus::Complete)
            return;
        if (result.status == StepStatus::Failed)
            throw std::runtime_error(result.detail.empty() ? "secure handshake failed" : result.detail);

        int wait_ms = 50;
        if (timeout_ms >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            const long long remaining = static_cast<long long>(timeout_ms) - elapsed.count();
            if (remaining <= 0) {
                forget(socket);
                throw std::runtime_error("secure handshake timed out");
            }
            wait_ms = static_cast<int>(std::min<long long>(remaining, wait_ms));
        }
        impl_->network.poll({socket}, wait_ms);
    }
}

std::size_t SecureChannelManager::send_line(NetworkManager::Handle socket, std::string_view plaintext,
                                            int timeout_ms) {
    const auto iterator = impl_->sessions.find(socket);
    if (iterator == impl_->sessions.end())
        throw std::runtime_error("network handle has no active secure channel");

    Impl::Session &session = iterator->second;
    if (session.send_counter == std::numeric_limits<std::uint64_t>::max()) {
        impl_->clear_socket(socket);
        throw std::runtime_error("secure channel send counter exhausted");
    }

    const std::uint64_t counter = session.send_counter;
    const std::string frame = Impl::seal_at(session, plaintext, counter) + "\n";
    try {
        send_all(impl_->network, socket, frame, timeout_ms);
    } catch (...) {
        // Partial or failed send leaves the peer in an unknown state — kill the session.
        impl_->clear_socket(socket);
        throw;
    }
    ++session.send_counter;
    return plaintext.size();
}

NetworkManager::ReceiveResult SecureChannelManager::receive_line(NetworkManager::Handle socket) {
    const auto iterator = impl_->sessions.find(socket);
    if (iterator == impl_->sessions.end())
        throw std::runtime_error("network handle has no active secure channel");

    NetworkManager::ReceiveResult result = impl_->network.receive_line(socket);
    if (result.status == NetworkManager::ReceiveStatus::Data) {
        try {
            result.data = Impl::open(iterator->second, result.data);
        } catch (...) {
            impl_->clear_socket(socket);
            throw;
        }
    } else if (result.status == NetworkManager::ReceiveStatus::Closed) {
        impl_->clear_socket(socket);
    }
    return result;
}

bool SecureChannelManager::active(NetworkManager::Handle socket) const {
    return impl_->sessions.contains(socket);
}

bool SecureChannelManager::handshake_pending(NetworkManager::Handle socket) const {
    return impl_->pending.contains(socket);
}

void SecureChannelManager::forget(NetworkManager::Handle socket) noexcept {
    impl_->clear_socket(socket);
}
