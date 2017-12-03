#pragma once

#include <functional>

#include <boost/type_traits/is_same.hpp>

namespace hlp
{

namespace details
{
    template <class T, std::size_t N, class... Args>
    struct get_number_of_element_from_tuple_by_type_impl
    {
        const static std::size_t value = N;
    };

    template <class T, std::size_t N, class... Args>
    struct get_number_of_element_from_tuple_by_type_impl<T, N, T, Args...>
    {
        const static std::size_t value = N;
    };

    template <class T, std::size_t N, class... Args>
    struct get_number_of_element_from_tuple_by_type_impl<T, N, std::reference_wrapper<T>, Args...>
    {
        const static std::size_t value = N;
    };

    template <class T, std::size_t N, class U, class... Args>
    struct get_number_of_element_from_tuple_by_type_impl<T, N, U, Args...>
    {
        const static std::size_t value = get_number_of_element_from_tuple_by_type_impl<T, N + 1, Args...>::value;
    };


    template <class T, class... Args>
    const T& get_element_by_type(const std::tuple<Args...>& t)
    {
        return std::get<get_number_of_element_from_tuple_by_type_impl<T, 0, Args...>::value>(t);
    }

} // namespace details

template<class T>
struct Param
{

    template<typename ...Args>
    static T& Unpack(const std::tuple<Args...>& tuple)
    {
        return const_cast<T&>(details::get_element_by_type<T>(tuple));
    }

    template<typename ...Args>
    static T& Unpack(const T& arg, const Args&...)
    {
        return const_cast<T&>(arg);
    }

    template<typename ...Args>
    static T& Unpack(const std::reference_wrapper<T>& arg, const Args&...)
    {
        return const_cast<T&>(arg.get());
    }

    template<typename Ignored, typename ...Args>
    static T& Unpack(const Ignored&, const Args&... args)
    {
        return Unpack(args...);
    }

};

} // namespace hlp
