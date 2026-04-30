#include "acpi_power.hpp"
#include "shell_init.hpp"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/reboot.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <thread>

// 常用颜色
#define C_RESET "\033[0m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_WHITE "\033[1m"

static void init_console() {
    int fd = open("/dev/console", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // 让 Enter 检测可以走非阻塞 read。
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
}

static volatile sig_atomic_t cad_hit = 0;
static bool verbose = false;

static void on_sigint(int) {
    cad_hit = 1;
}

// logger
static void log_verbose(const char* fmt, ...) {
    if (!verbose) {
        return;
    }
    printf("[v] ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(C_RESET "\n");
}
static void log_info(const char* fmt, ...) {
    printf(C_WHITE C_GREEN "[i] " C_RESET);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(C_RESET "\n");
}
static void log_warn(const char* fmt, ...) {
    printf(C_WHITE C_YELLOW "[!] " C_RESET);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(C_RESET "\n");
}
static void log_error(const char* fmt, ...) {
    printf(C_WHITE C_RED "[X] " C_RESET);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(C_RESET "\n");
}

static void perform_reboot() {
    log_info("Ctrl+Alt+Delete triggered! Rebooting...");
    sync();
    reboot(LINUX_REBOOT_CMD_RESTART);
    for (;;) pause();
}

static void perform_poweroff() {
    log_info("[i] Power button triggered! Powering off...");
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
    for (;;) pause();
}

static bool consume_enter_press() {
    struct pollfd stdin_pollfd{STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&stdin_pollfd, 1, 0);
    if (ready <= 0 || (stdin_pollfd.revents & POLLIN) == 0) {
        return false;
    }

    bool hit = false;
    char buffer[128];
    for (;;) {
        const ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                if (buffer[i] == '\n' || buffer[i] == '\r') {
                    hit = true;
                }
            }
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }
    return hit;
}

static bool handle_enter_restart_request() {
    if (!consume_enter_press()) {
        return false;
    }

    log_info("Enter detected. Entering emergency shell...");
    const int status = system(siEmergencyShell);
    if (status == -1) {
        log_error("Can't open shell: %s", strerror(errno));
    } else if (WIFEXITED(status)) {
        log_info("Shell exited with exit=%d", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        log_warn("Shell killed by signal %d", WTERMSIG(status));
    } else {
        log_warn("Shell exited with status=0x%x", status);
    }

    log_info("Restarting init flow...");
    return true;
}

static bool process_pending_events(const AcpiPowerKeyWatcher& acpi, int acpi_wait_ms) {
    if (cad_hit != 0) {
        perform_reboot();
    }
    if (handle_enter_restart_request()) {
        return true;
    }

    if (acpi_wait_ms > 0) {
        if (acpi.IsAvailable()) {
            if (acpi.WaitForPowerKey(acpi_wait_ms)) {
                perform_poweroff();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(acpi_wait_ms));
        }
    } else if (acpi.WaitForPowerKey(0)) {
        perform_poweroff();
    }
    return handle_enter_restart_request();
}

static void print_banner() {
    printf(C_WHITE "=== " C_GREEN "Power4Init" C_RESET C_WHITE " is initializing your system! ===\n" C_RESET);
    log_info("Power4Init (p4init) 1.0.0");
    log_info("Made by littlekan233 & GPT-5.3-Codex with love <3");
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (getpid() != 1) {
        fprintf(stderr, C_RED "This program must run as PID 1 (current PID=%d)\n" C_RESET, getpid());
        return 1;
    }

    init_console();

    if (reboot(LINUX_REBOOT_CMD_CAD_OFF) < 0) {
        perror("reboot(CAD_OFF)");
    }

    struct sigaction sigact{};
    sigact.sa_handler = on_sigint;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sigact, nullptr);

    int delay_sec = 0;

    static option long_opts[] = {
        {"delay", required_argument, nullptr, 'd'},
        {"verbose", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:v", long_opts, nullptr)) != -1) {
        switch (c) {
            case 'd':
                delay_sec = atoi(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [--delay N] [--verbose]\n", argv[0]);
                return 2;
        }
    }

    AcpiPowerKeyWatcher acpi;
    const bool acpi_ready = acpi.Init();

    if (verbose) {
        log_warn("You started p4init with --verbose. It should be used for developing!");
    }
    log_verbose("ACPI power key watcher: %s", acpi_ready ? "enabled" : "not available");

    for (;;) {
        print_banner();

        if (delay_sec > 0) {
            log_verbose("Waiting %d second(s) before exec systemd...", delay_sec);
            const int loops = delay_sec * 10;
            bool restart_during_delay = false;
            for (int i = 0; i < loops; ++i) {
                if (process_pending_events(acpi, 0)) {
                    restart_during_delay = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (restart_during_delay) {
                continue;
            }
        }

        if (process_pending_events(acpi, 0)) {
            continue;
        }

        sync();

        const char* candidates[] = {
            "/usr/lib/systemd/systemd",
            "/lib/systemd/systemd",
            "/sbin/init"
        };

        log_info("Running systemd...");
        char* const newargv[] = {
            const_cast<char*>("systemd"),
            nullptr
        };

        bool restart_requested = false;
        for (const char* p : candidates) {
            if (process_pending_events(acpi, 0)) {
                restart_requested = true;
                break;
            }
            log_verbose("Trying exec: %s", p);
            execv(p, newargv);
            log_verbose("execv failed: %s (%s)", p, strerror(errno));
        }
        if (restart_requested) {
            continue;
        }

        log_error("Cannot start systemd! <ERRINFO> %d (%s)", errno, strerror(errno));
        log_error("Press " C_WHITE "Ctrl+Alt+Delete" C_RESET " to reboot.");
	    log_error("Press " C_WHITE "Power Button" C_RESET " to power off.");
        log_error("Press " C_WHITE "Enter" C_RESET " to enter emergency shell.");
        for (;;) {
            if (process_pending_events(acpi, 250)) {
                break;
            }
        }
    }
}
