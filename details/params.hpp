#pragma once

#include <functional>

namespace hlp
{

template<class T>
struct Param
{
    template<typename ...Args>
    static T& Unpack(const std::reference_wrapper<T>& arg, const Args&... args)
    {
        return arg;
    }

    template<typename ...Args>
    static T& Unpack(const T& arg, const Args&... args)
    {
        return arg;
    }

    template<typename Ignored, typename ...Args>
    static T& Unpack(const Ignored&, const Args&... args)
    {
        return Unpack(args...);
    }

};

} // namespace hlp
