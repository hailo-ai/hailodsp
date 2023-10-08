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

#ifdef NDEBUG
#define CONSOLE_LOGGER_PATTERN ("[%n] [%^%l%$] %v") // Console logger will print: [hailodsp] [log level] msg
#else
#define CONSOLE_LOGGER_PATTERN \
    ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v") // Console logger will print: [timestamp] [PID] [TID]
                                                                 // [hailodsp] [log level] [source file:line number]
                                                                 // [function name] msg
#endif

class Logger {
   public:
    Logger() : m_console_sink(std::make_shared<spdlog::sinks::stderr_color_sink_mt>())
    {
        m_console_sink->set_pattern(CONSOLE_LOGGER_PATTERN);

        spdlog::sinks_init_list sink_list = {m_console_sink};

        m_logger = std::make_shared<spdlog::logger>("hailodsp", sink_list.begin(), sink_list.end());
        // set log level to minimum. The actual level is controled at compile time
        m_logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(m_logger);

        LOGGER__TRACE("libhailodsp is loaded");
    }

   private:
    std::shared_ptr<spdlog::sinks::sink> m_console_sink;
    std::shared_ptr<spdlog::logger> m_logger;
};

// This will cause the logger to initialize on load
static Logger logger;
