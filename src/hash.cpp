#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "file_probe/hash.hpp"

namespace file_probe {

    namespace {
        class Sha256 {
        public:
            Sha256() {
                reset();
            }

            void update(const std::uint8_t* data, std::size_t length) {
                while (length > 0) {
                    std::size_t space = block_size - buffer_size_;
                    std::size_t to_copy = std::min(space, length);
                    std::memcpy(buffer_.data() + buffer_size_, data, to_copy);
                    buffer_size_ += to_copy;
                    data += to_copy;
                    length -= to_copy;
                    bit_count_ += static_cast<std::uint64_t>(to_copy) * 8;

                    if (buffer_size_ == block_size) {
                        process_block(buffer_.data());
                        buffer_size_ = 0;
                    }
                }
            }

            std::array<std::uint8_t, 32> finalize() {
                buffer_[buffer_size_++] = 0x80;
                if (buffer_size_ > 56) {
                    while (buffer_size_ < block_size) {
                        buffer_[buffer_size_++] = 0;
                    }
                    process_block(buffer_.data());
                    buffer_size_ = 0;
                }

                while (buffer_size_ < 56) {
                    buffer_[buffer_size_++] = 0;
                }

                for (int shift = 56; shift >= 0; shift -= 8) {
                    buffer_[buffer_size_++] = static_cast<std::uint8_t>((bit_count_ >> shift) & 0xFF);
                }

                process_block(buffer_.data());

                std::array<std::uint8_t, 32> digest {};
                for (std::size_t i = 0; i < 8; ++i) {
                    digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
                    digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
                    digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
                    digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
                }
                return digest;
            }

        private:
            static constexpr std::size_t block_size = 64;

            void reset() {
                state_ = {
                    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
                bit_count_ = 0;
                buffer_size_ = 0;
            }

            static std::uint32_t rotr(std::uint32_t value, std::uint32_t count) {
                return (value >> count) | (value << (32 - count));
            }

            void process_block(const std::uint8_t* block) {
                static const std::uint32_t k[64] = {
                    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
                    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
                    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
                    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
                    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
                    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
                    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

                std::uint32_t w[64];
                for (std::size_t i = 0; i < 16; ++i) {
                    w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                        (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                        (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                        (static_cast<std::uint32_t>(block[i * 4 + 3]));
                }
                for (std::size_t i = 16; i < 64; ++i) {
                    std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                std::uint32_t a = state_[0];
                std::uint32_t b = state_[1];
                std::uint32_t c = state_[2];
                std::uint32_t d = state_[3];
                std::uint32_t e = state_[4];
                std::uint32_t f = state_[5];
                std::uint32_t g = state_[6];
                std::uint32_t h = state_[7];

                for (std::size_t i = 0; i < 64; ++i) {
                    std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                    std::uint32_t ch = (e & f) ^ ((~e) & g);
                    std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
                    std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                    std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    std::uint32_t temp2 = s0 + maj;

                    h = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }

                state_[0] += a;
                state_[1] += b;
                state_[2] += c;
                state_[3] += d;
                state_[4] += e;
                state_[5] += f;
                state_[6] += g;
                state_[7] += h;
            }

            std::array<std::uint32_t, 8> state_ {};
            std::array<std::uint8_t, block_size> buffer_ {};
            std::uint64_t bit_count_ = 0;
            std::size_t buffer_size_ = 0;
        };
    }

    std::optional<std::string> compute_sha256(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }

        Sha256 hasher;
        std::array<std::uint8_t, 1 << 15> buffer {};

        while (file) {
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            std::streamsize bytes_read = file.gcount();
            if (bytes_read > 0) {
                hasher.update(buffer.data(), static_cast<std::size_t>(bytes_read));
            }
        }

        if (!file.eof()) {
            return std::nullopt;
        }

        auto digest = hasher.finalize();
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (std::uint8_t byte : digest) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }
}
