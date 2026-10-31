#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/single-instance.h"
#include "core/text-utils.h"

namespace Flare {
    namespace {
        std::uint32_t ReadBigEndian(const unsigned char* bytes) {
            return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
                | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
        }

        bool ReadExact(int socket, void* data, std::size_t size) {
            auto* bytes = static_cast<unsigned char*>(data);
            std::size_t offset = 0;
            while (offset < size) {
                const ssize_t received = recv(socket, bytes + offset, size - offset, 0);
                if (received == 0) {
                    return false;
                }
                if (received < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return false;
                }
                offset += static_cast<std::size_t>(received);
            }
            return true;
        }

        bool WriteExact(int socket, const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            std::size_t offset = 0;
            while (offset < size) {
                const ssize_t sent = send(socket, bytes + offset, size - offset, 0);
                if (sent < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return false;
                }
                offset += static_cast<std::size_t>(sent);
            }
            return true;
        }

        std::string EncodeArguments(const std::vector<std::string>& arguments) {
            std::string payload;
            for (const std::string& argument : arguments) {
                payload.append(argument);
                payload.push_back('\0');
            }
            return payload;
        }

        std::vector<std::string> DecodeArguments(std::string_view payload) {
            std::vector<std::string> arguments;
            std::size_t start = 0;
            while (start < payload.size()) {
                const std::size_t end = payload.find('\0', start);
                if (end == std::string_view::npos) {
                    break;
                }
                arguments.emplace_back(payload.substr(start, end - start));
                start = end + 1;
            }
            return arguments;
        }

        bool SetCloseOnExec(int descriptor) {
            const int flags = fcntl(descriptor, F_GETFD);
            return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
        }
    }

    struct SingleInstanceGuard::Impl {
        int lockFd = -1;
        int listenFd = -1;
        int stopReadFd = -1;
        int stopWriteFd = -1;
        std::string socketPath;
        std::string lockPath;
        std::atomic<bool> stopping{false};
        std::thread listener;
        SingleInstanceGuard::Callback callback;

        void Listen() {
            while (!stopping.load()) {
                pollfd descriptors[2]{};
                descriptors[0].fd = listenFd;
                descriptors[0].events = POLLIN;
                descriptors[1].fd = stopReadFd;
                descriptors[1].events = POLLIN;
                const int ready = poll(descriptors, 2, -1);
                if (ready < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                if ((descriptors[1].revents & POLLIN) != 0) {
                    break;
                }
                if ((descriptors[0].revents & POLLIN) == 0) {
                    continue;
                }
                const int client = accept(listenFd, nullptr, nullptr);
                if (client < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                timeval timeout{};
                timeout.tv_sec = 3;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                unsigned char lengthBytes[4]{};
                if (ReadExact(client, lengthBytes, sizeof(lengthBytes))) {
                    const std::uint32_t length = ReadBigEndian(lengthBytes);
                    std::string payload(length, '\0');
                    if (ReadExact(client, payload.data(), payload.size())) {
                        std::vector<std::string> arguments = DecodeArguments(payload);
                        if (callback) {
                            callback(std::move(arguments));
                        }
                    }
                }
                close(client);
            }
        }
    };

    Result<std::unique_ptr<SingleInstanceGuard>> SingleInstanceGuard::Create(
        std::string identifier, const std::vector<std::string>& arguments, Callback callback) {
        std::error_code error;
        const std::filesystem::path temporary = std::filesystem::temp_directory_path(error);
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to resolve the temporary directory", error.message());
        }
        const std::string key = SanitizeFileName(identifier);
        const std::filesystem::path base = temporary / ("flare-instance-" + key);
        const std::string lockPath = base.string() + ".lock";
        const std::string socketPath = base.string() + ".sock";
        if (socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Single-instance socket path is too long");
        }

        const int lockFd = open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lockFd < 0) {
            return Error(ErrorCode::IO_FAILURE, "Unable to open the single-instance lock file", std::strerror(errno));
        }
        if (flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
            close(lockFd);
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                return Error(ErrorCode::IO_FAILURE, "Unable to acquire the single-instance lock", std::strerror(errno));
            }
            const std::string payload = EncodeArguments(arguments);
            for (int attempt = 0; attempt < 20; ++attempt) {
                const int client = socket(AF_UNIX, SOCK_STREAM, 0);
                if (client < 0) {
                    break;
                }
                sockaddr_un address{};
                address.sun_family = AF_UNIX;
                std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size());
                if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
                    const unsigned char lengthBytes[4] = {static_cast<unsigned char>((length >> 24) & 0xFFu),
                        static_cast<unsigned char>((length >> 16) & 0xFFu),
                        static_cast<unsigned char>((length >> 8) & 0xFFu), static_cast<unsigned char>(length & 0xFFu)};
                    WriteExact(client, lengthBytes, sizeof(lengthBytes));
                    WriteExact(client, payload.data(), payload.size());
                    close(client);
                    return Error(
                        ErrorCode::SINGLE_INSTANCE, "Another instance is already running; arguments were forwarded");
                }
                close(client);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return Error(ErrorCode::SINGLE_INSTANCE, "Another instance is already running and could not be reached");
        }

        auto impl = std::make_unique<Impl>();
        impl->lockFd = lockFd;
        impl->lockPath = lockPath;
        impl->socketPath = socketPath;
        impl->callback = std::move(callback);

        unlink(socketPath.c_str());
        impl->listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (impl->listenFd < 0) {
            return Error(
                ErrorCode::PLATFORM_FAILURE, "Unable to create the single-instance socket", std::strerror(errno));
        }
        SetCloseOnExec(impl->listenFd);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size());
        if (bind(impl->listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(impl->listenFd);
            return Error(
                ErrorCode::PLATFORM_FAILURE, "Unable to bind the single-instance socket", std::strerror(errno));
        }
        chmod(socketPath.c_str(), 0600);
        if (listen(impl->listenFd, 8) != 0) {
            close(impl->listenFd);
            unlink(socketPath.c_str());
            return Error(
                ErrorCode::PLATFORM_FAILURE, "Unable to listen on the single-instance socket", std::strerror(errno));
        }

        int stopPipe[2] = {-1, -1};
        if (pipe(stopPipe) != 0) {
            close(impl->listenFd);
            unlink(socketPath.c_str());
            return Error(
                ErrorCode::PLATFORM_FAILURE, "Unable to create the single-instance control pipe", std::strerror(errno));
        }
        SetCloseOnExec(stopPipe[0]);
        SetCloseOnExec(stopPipe[1]);
        impl->stopReadFd = stopPipe[0];
        impl->stopWriteFd = stopPipe[1];

        Impl* raw = impl.get();
        impl->listener = std::thread([raw]() { raw->Listen(); });

        auto guard = std::unique_ptr<SingleInstanceGuard>(new SingleInstanceGuard(std::move(impl)));
        return guard;
    }

    SingleInstanceGuard::SingleInstanceGuard(std::unique_ptr<Impl> guardImpl) : impl(std::move(guardImpl)) {}

    SingleInstanceGuard::~SingleInstanceGuard() {
        if (!impl) {
            return;
        }
        impl->stopping.store(true);
        if (impl->stopWriteFd >= 0) {
            const char byte = 1;
            const ssize_t ignored = write(impl->stopWriteFd, &byte, 1);
            (void)ignored;
        }
        if (impl->listener.joinable()) {
            impl->listener.join();
        }
        if (impl->listenFd >= 0) {
            close(impl->listenFd);
        }
        if (impl->stopReadFd >= 0) {
            close(impl->stopReadFd);
        }
        if (impl->stopWriteFd >= 0) {
            close(impl->stopWriteFd);
        }
        if (!impl->socketPath.empty()) {
            unlink(impl->socketPath.c_str());
        }
        if (impl->lockFd >= 0) {
            flock(impl->lockFd, LOCK_UN);
            close(impl->lockFd);
        }
    }
}
