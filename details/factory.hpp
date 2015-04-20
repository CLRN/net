#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace net
{
namespace details
{

/*!Interface for factory with variadic arguments in factory function.
 * This factory needed because in some cases we may want to split
 * arguments which we know when we instantiating factory(StaticArg) and arguments
 * which we know in runtime at time when we need to create object using factory(RuntimeArg).
 * This interface knows about runtime arguments, so we can hold pointer to factory somewhere
 * and use factory function to instantiate objects. But to pass some additional arguments through
 * factory function we need concrete factory with another variadic pack referenced from factory ctor.
 *
\code{.cpp}
    class Connection;           // what we need to create
    class Socket;               // connection argument, known when connected to server
    class Description;          // connection argument, known when connected to server
    class IoService;            // connection argument, known when client instantiated

    class Client
    {
    public:
        Client(const IoService& s) // this ctor may be variadic
        {
            m_Factory = MakeFactory<Socket, Socket, Description>(s, 12345, "some another argument");
        }

        boost::shared_ptr<Connection> Connect()
        {
            const Socket socket = ConnectToServer();
            const Description desc = GetServerDescription();

            const auto connection = m_Factory->Create(socket, desc); // make connection using arguments from Client
                                                                     // ctor and from stack
            return connection;
        }

    private:
        typename IFactory<Socket, Socket, Description>::Ptr m_Factory;
    };

\endcode

 * */

template<typename Result, typename ... RuntimeArg>
class IFactory
{
public:
    typedef boost::shared_ptr<IFactory> Ptr;

    virtual ~IFactory() = default;
    virtual boost::shared_ptr<Result> Create(const RuntimeArg& ... argument) = 0;
};

template<typename Result, typename StaticArg, typename ... RuntimeArg>
class ConcreteFactory : public IFactory<Result, RuntimeArg...>
{
public:
    ConcreteFactory(const StaticArg& arg) : m_Static(arg) {}
    virtual boost::shared_ptr<Result> Create(const RuntimeArg& ... runtime) override
    {
        return boost::make_shared<Result>(runtime..., m_Static);
    }
private:
    const StaticArg& m_Static;
};

//! Factory method for concrete factory...(because of ConcreteFactory complex template).
//! Through this function we can split two sets of variadic arguments correctly.
template<typename Result, typename ... Runtime, typename ... Static>
typename IFactory<Result, Runtime...>::Ptr MakeFactory(const Static& ... args)
{
    return boost::make_shared<ConcreteFactory<Result, Static..., Runtime...>>(args...);
}

} // namespace details
} // namespace net