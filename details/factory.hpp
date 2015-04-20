#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace net
{
namespace details
{

template<typename Result, typename Arg>
class IFactory
{
public:
    typedef boost::shared_ptr<IFactory> Ptr;

    virtual ~IFactory() = default;
    virtual boost::shared_ptr<Result> Create(const Arg& argument) = 0;
};

template<typename Result, typename Arg, typename Args>
class ConcreteFactory : public IFactory<Result, Arg>
{
public:
    ConcreteFactory(const Args& args) : m_Args(args) {}
    virtual boost::shared_ptr<Result> Create(const Arg& argument) override
    {
        return boost::make_shared<Result>(argument, m_Args);
    }
private:
    const Args& m_Args;
};

} // namespace details
} // namespace net