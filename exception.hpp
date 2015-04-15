#pragma once

#include "log/src/formatter.h"

#include <boost/exception/exception.hpp>

namespace net
{

struct Exception : public virtual boost::exception, std::runtime_error
{
    template <typename ... T>
    Exception(const std::string& text, const T&... args)
            : std::runtime_error(logging::MessageFormatter(text, args...).GetText().c_str())
    {}
};

struct Disconnected : public Exception
{
    template <typename ... T>
    Disconnected(const T&... args) : Exception(args...) {}
};

struct UnableToConnect : public Exception
{
    template <typename ... T>
    UnableToConnect(const T&... args) : Exception(args...) {}
};

typedef boost::error_info<struct endpoint_, std::string> EndpointInfo;
typedef boost::error_info<struct sys_error_, boost::system::error_code> SysErrorInfo;

inline std::string to_string(SysErrorInfo const & e)
{
    std::ostringstream tmp;
    tmp << "(" << e.value() << "): \"" << e.value().message() << "\"";
    return tmp.str();
}

} // namespace net
