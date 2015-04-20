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

    template<typename Ignored, typename ...Args>
    static T& Unpack(const Ignored&, const Args&... args)
    {
        return Unpack(args...);
    }

    static T& Unpack()
    {
        static const T def = {};
        return def;
    }

};

} // namespace hlp
