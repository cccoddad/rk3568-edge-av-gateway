// 文件作用：程序入口，负责命令行、配置、日志、系统信号和 Application 生命周期。
// 主要知识点：from_chars、结构化错误、POSIX/Windows 信号、jthread 和退出码约定。
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "rkav/app/application.h"
#include "rkav/common/error.h"
#include "rkav/common/logger.h"
#include "rkav/config/config.h"

namespace {

volatile std::sig_atomic_t g_signal_requested = 0;  // 0=未收到信号，1=请求安全停止。

/// 功能：异步信号安全地记录停止意图；参数中的具体信号值当前无需区分。
// 信号处理函数中只能修改 sig_atomic_t，日志、加锁和停止线程都交给控制线程执行。
void HandleSignal(int) { g_signal_requested = 1; }

struct CommandLine {
    std::string config_path{"config/mock.json"};  // JSON 配置文件路径。
    std::optional<int> duration_seconds;  // CLI 运行秒数覆盖值；未设置则使用 JSON。
    bool validate_only{false};            // 只校验配置，不启动任何 worker。
    bool show_help{false};                // 输出帮助后退出。
    bool show_version{false};             // 输出版本后退出。
};

/// 功能：把可用参数和含义打印到标准输出。
void PrintHelp() {
    std::cout << "RK3568 audio-video edge gateway\n\n"
              << "Usage: rkav-gateway [options]\n"
              << "  --config <path>      Load the given JSON configuration\n"
              << "  --duration <seconds> Override run duration; 0 runs until a signal\n"
              << "  --validate-config   Validate configuration without starting workers\n"
              << "  --version           Print the program version\n"
              << "  --help              Print this help\n";
}

/// 功能：严格解析 argv，任何未知参数或缺失值都返回 kInvalidConfig。
/// 返回：成功时为 CommandLine；失败时包含 cli.parse 上下文。
rkav::Result<CommandLine> ParseArguments(int argc, char* argv[]) {
    CommandLine command;  // 从默认命令行设置开始逐项覆盖。
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];  // 当前正在解析的参数文本。
        if (argument == "--help" || argument == "-h") {
            command.show_help = true;
        } else if (argument == "--version") {
            command.show_version = true;
        } else if (argument == "--validate-config") {
            command.validate_only = true;
        } else if (argument == "--config") {
            if (index + 1 >= argc) {
                return rkav::Result<CommandLine>::Failure(
                    rkav::Error{rkav::ErrorCategory::kInvalidConfig, 0, "cli", "parse",
                                "--config requires a path", false});
            }
            command.config_path = argv[++index];
        } else if (argument == "--duration") {
            if (index + 1 >= argc) {
                return rkav::Result<CommandLine>::Failure(
                    rkav::Error{rkav::ErrorCategory::kInvalidConfig, 0, "cli", "parse",
                                "--duration requires a non-negative integer", false});
            }
            const std::string text = argv[++index];  // --duration 后的原始文本。
            int duration = -1;  // 解析后的秒数；-1 用来检测未正确赋值。
            // from_chars 不受本地化环境影响，并能严格拒绝夹杂其他字符的数值。
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), duration);
            if (error != std::errc{} || end != text.data() + text.size() || duration < 0) {
                return rkav::Result<CommandLine>::Failure(
                    rkav::Error{rkav::ErrorCategory::kInvalidConfig, 0, "cli", "parse",
                                "--duration requires a non-negative integer", false});
            }
            command.duration_seconds = duration;
        } else {
            return rkav::Result<CommandLine>::Failure(
                rkav::Error{rkav::ErrorCategory::kInvalidConfig, 0, "cli", "parse",
                            "unknown argument: " + argument, false});
        }
    }
    return rkav::Result<CommandLine>::Success(std::move(command));
}

}  // namespace

/// 功能：协调程序完整生命周期，并用退出码区分成功、运行错误和配置错误。
/// 退出码：0=成功，1=运行阶段失败，2=命令行或配置错误。
int main(int argc, char* argv[]) {
    try {
        auto parsed = ParseArguments(argc, argv);  // 命令行解析结果。
        if (!parsed) {
            std::cerr << rkav::DescribeError(parsed.error()) << '\n';
            return 2;
        }
        const CommandLine command = std::move(parsed).value();  // 后续只读的命令行配置。
        if (command.show_help) {
            PrintHelp();
            return 0;
        }
        if (command.show_version) {
            std::cout << "rkav-gateway " << RKAV_VERSION << '\n';
            return 0;
        }

        auto loaded = rkav::ConfigLoader::LoadFromFile(command.config_path);  // 文件加载结果。
        if (!loaded) {
            std::cerr << rkav::DescribeError(loaded.error()) << '\n';
            return 2;
        }
        rkav::AppConfig config = std::move(loaded).value();  // 本次运行的强类型配置。
        if (command.duration_seconds.has_value()) {
            config.runtime.run_duration_seconds = *command.duration_seconds;
        }
        // CLI 覆盖发生后重新校验，保证命令行和 JSON 遵守同一套语义规则。
        auto validation = rkav::ConfigLoader::Validate(config);
        if (!validation) {
            std::cerr << rkav::DescribeError(validation.error()) << '\n';
            return 2;
        }
        if (command.validate_only) {
            std::cout << "Configuration is valid: " << rkav::ConfigSummary(config) << '\n';
            return 0;
        }

        rkav::LogLevel log_level = rkav::LogLevel::kInfo;  // 配置文本对应的枚举值。
        if (!rkav::ParseLogLevel(config.runtime.log_level, log_level)) {
            std::cerr << "config.runtime.log_level: unsupported level\n";
            return 2;
        }
        rkav::Logger::Instance().SetMinimumLevel(log_level);
        rkav::Logger::Instance().Log(rkav::LogLevel::kInfo, "main", "configuration_loaded",
                                     "configuration validated",
                                     {{"config", rkav::ConfigSummary(config)}});

        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);

        rkav::Application application(config);  // 管道及全部 worker 的唯一所有者。
        auto started = application.Start();     // 启动结果。
        if (!started) {
            std::cerr << rkav::DescribeError(started.error()) << '\n';
            return 1;
        }

        // 控制线程把异步信号和运行时长转换成普通的线程安全停止请求。
        std::jthread controller(
            [&application, duration = config.runtime.run_duration_seconds](std::stop_token stop) {
                const auto started_at = std::chrono::steady_clock::now();  // 自动停止计时起点。
                while (!stop.stop_requested() && !application.stop_requested()) {
                    if (g_signal_requested != 0) {
                        application.RequestStop("signal");
                        break;
                    }
                    if (duration > 0 && std::chrono::steady_clock::now() - started_at >=
                                            std::chrono::seconds(duration)) {
                        application.RequestStop("run_duration_elapsed");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });

        auto completed = application.Wait();  // 最终运行/停止结果。
        controller.request_stop();
        controller.join();
        if (!completed) {
            std::cerr << rkav::DescribeError(completed.error()) << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "fatal unhandled exception: " << exception.what() << '\n';
        return 1;
    }
}
