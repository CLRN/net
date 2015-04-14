#pragma once

namespace hlp
{

template<class T>
struct Param
{
    template<typename ...Args>
    static T& Unpack(T& arg, Args... args)
    {
        return arg;
    }

    template<typename Ignored, typename ...Args>
    static T& Unpack(Ignored&, Args&... args)
    {
        return Unpack(args...);
    }

    template<typename ...Args>
    static T& Unpack(const std::reference_wrapper<T>& arg, const Args&... args)
    {
        return arg;
    }

    static const T& Unpack()
    {
        static const T def = {};
        return def;
    }

    template<typename ...Args>
    static const T& Unpack(const T& arg, const Args&... args)
    {
        return arg;
    }

    template<typename Ignored, typename ...Args>
    static const T& Unpack(const Ignored&, const Args&... args)
    {
        return Unpack(args...);
    }

};

} // namespace hlp
