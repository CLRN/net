#pragma once

#include <windows.h>

namespace net
{
namespace pipes
{
namespace details
{

class PipeSecurity
{
public:
    inline PipeSecurity();
    inline ~PipeSecurity();
    inline void BuildAttributes(SECURITY_ATTRIBUTES& sa);
private:
    inline BOOL GetUserSid(PSID*  ppSidUser);
private:

    BOOL                    m_IsAllocated;
    PSECURITY_DESCRIPTOR    m_SecurityDescriptor;
    PACL                    m_ACL;
    PTOKEN_USER             m_TokenUser;
};

} // namespace details
} // namespace pipes
} // namespace net

#include "pipe_security_impl.hpp"
