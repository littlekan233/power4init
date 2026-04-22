#include "acpi_power.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

namespace {
bool TestBit(int bit, const unsigned long* bits) {
    const int bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
    return (bits[bit / bits_per_word] >> (bit % bits_per_word)) & 1UL;
}

bool DeviceHasPowerKey(int fd) {
    unsigned long ev_bits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
        return false;
    }
    if (!TestBit(EV_KEY, ev_bits)) {
        return false;
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }
    return TestBit(KEY_POWER, key_bits);
}
}  // namespace

AcpiPowerKeyWatcher::~AcpiPowerKeyWatcher() {
    for (int fd : fds_) {
        close(fd);
    }
}

bool AcpiPowerKeyWatcher::Init() {
    for (int fd : fds_) {
        close(fd);
    }
    fds_.clear();

    DIR* dir = opendir("/dev/input");
    if (dir == nullptr) {
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        const std::string path = std::string("/dev/input/") + entry->d_name;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        if (DeviceHasPowerKey(fd)) {
            fds_.push_back(fd);
        } else {
            close(fd);
        }
    }

    closedir(dir);
    return !fds_.empty();
}

bool AcpiPowerKeyWatcher::WaitForPowerKey(int timeout_ms) const {
    if (fds_.empty()) {
        return false;
    }

    std::vector<struct pollfd> poll_fds;
    poll_fds.reserve(fds_.size());
    for (int fd : fds_) {
        poll_fds.push_back({fd, POLLIN, 0});
    }

    const int ready = poll(poll_fds.data(), poll_fds.size(), timeout_ms);
    if (ready <= 0) {
        return false;
    }

    for (const auto& pfd : poll_fds) {
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        struct input_event event;
        while (read(pfd.fd, &event, sizeof(event)) == sizeof(event)) {
            if (event.type == EV_KEY && event.code == KEY_POWER && event.value == 1) {
                return true;
            }
        }
    }

    return false;
}

bool AcpiPowerKeyWatcher::IsAvailable() const {
    return !fds_.empty();
}
