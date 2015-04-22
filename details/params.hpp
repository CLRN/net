#pragma once

namespace hlp
{

template<class T>
struct Param
{
    template<typename ...Args>
    static T& Unpack(const T& arg, const Args&...)
    {
        return const_cast<T&>(arg);
    }

    template<typename Ignored, typename ...Args>
    static T& Unpack(const Ignored&, const Args&... args)
    {
        return Unpack(args...);
    }

};

} // namespace hlp
