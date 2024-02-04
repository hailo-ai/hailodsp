/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "logger_macros.hpp"
#include "spdlog/sinks/stdout_color_sinks.h"

#define CONSOLE_LOGGER_PATTERN \
    ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v") // Console logger will print: [timestamp] [PID] [TID]
                                                                 // [hailodsp] [log level] [source file:line number]
                                                                 // [function name] msg
#define CONSOLE_LEVEL_ENV_NAME ("HAILODSP_CONSOLE_LOG_LEVEL")

class Logger {
   public:
    Logger() : m_console_sink(std::make_shared<spdlog::sinks::stderr_color_sink_mt>())
    {
        m_console_sink->set_pattern(CONSOLE_LOGGER_PATTERN);

        m_logger = std::make_shared<spdlog::logger>("hailodsp", m_console_sink);
#if NDEBUG
        auto default_level = spdlog::level::level_enum::warn;
#else
        auto default_level = spdlog::level::level_enum::debug;
#endif
        m_logger->set_level(get_level(std::getenv(CONSOLE_LEVEL_ENV_NAME), default_level));

        LOGGER__TRACE("libhailodsp is loaded");
    }

    spdlog::level::level_enum get_level(const char *log_level_c_str, spdlog::level::level_enum default_level)
    {
        std::string log_level = log_level_c_str == nullptr ? "" : std::string(log_level_c_str);

        if (log_level == "trace")
            return spdlog::level::level_enum::trace;
        else if (log_level == "debug")
            return spdlog::level::level_enum::debug;
        else if (log_level == "info")
            return spdlog::level::level_enum::info;
        else if (log_level == "warn")
            return spdlog::level::level_enum::warn;
        else if (log_level == "error")
            return spdlog::level::level_enum::err;
        else if (log_level == "critical")
            return spdlog::level::level_enum::critical;
        else if (log_level == "off")
            return spdlog::level::level_enum::off;
        else
            return default_level;
    }

    std::shared_ptr<spdlog::logger> get_logger() { return m_logger; }

   private:
    std::shared_ptr<spdlog::sinks::sink> m_console_sink;
    std::shared_ptr<spdlog::logger> m_logger;
};

// This will cause the logger to initialize on load
static Logger logger;

std::shared_ptr<spdlog::logger> get_hailodsp_logger()
{
    return logger.get_logger();
}