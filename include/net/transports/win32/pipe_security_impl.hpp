#include "pipe_security.hpp"
#include "exception/CheckHelpers.h"

#include <Sddl.h>

#include <boost/scope_exit.hpp>

namespace net
{
namespace pipes
{
namespace details
{

PipeSecurity::PipeSecurity()
    : m_IsAllocated()
    , m_SecurityDescriptor()
    , m_ACL()
    , m_TokenUser()
{
}

void PipeSecurity::BuildAttributes(SECURITY_ATTRIBUTES& sa)
{
    PSID pSidUser;
    CHECK_LE(GetUserSid(&pSidUser), cmn::Exception("Failed to get user sid"));

    char* pszSidUser;
    CHECK_LE(ConvertSidToStringSidA(pSidUser, &pszSidUser), cmn::Exception("Failed to get user sid"));

    char szBuff[1024] = {};
    // Start of DACL
    strcat_s(szBuff, "D:");
    // GENERIC_ALL access to current user
    strcat_s(szBuff, "(A;;GA;;;");
    strcat_s(szBuff, pszSidUser);
    strcat_s(szBuff, ")");
    // GENERIC_READ/WRITE to anonymous users
    strcat_s(szBuff, "(A;;GWGR;;;AN)");
    // GENERIC_READ/WRITE to everyone
    strcat_s(szBuff, "(A;;GWGR;;;WD)");

    // Free user sid again
    LocalFree(pszSidUser);

    // We need this only if we have Windows Vista, Windows 7 or Windows 2008 Server
    OSVERSIONINFO osvi;
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    CHECK_LE(GetVersionExA(&osvi), cmn::Exception("Failed to get win version"));

    // If Vista, Server 2008, or Windows7!
    if (osvi.dwMajorVersion >= 6)
    {
        // Now the trick with the SACL:
        // We set SECURITY_MANDATORY_UNTRUSTED_RID to SYSTEM_MANDATORY_POLICY_NO_WRITE_UP
        // Anonymous access is untrusted, and this process runs equal or above medium
        // integrity level. Setting "S:(ML;;NW;;;LW)" is not sufficient.
        strcat_s(szBuff, "S:(ML;;NW;;;S-1-16-0)");
    }

    // Get the SD
    CHECK_LE(ConvertStringSecurityDescriptorToSecurityDescriptorA(szBuff, SDDL_REVISION_1, &m_SecurityDescriptor, NULL), cmn::Exception("Failed to convert psid"));

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = m_SecurityDescriptor;
}


BOOL PipeSecurity::GetUserSid(PSID* ppSidUser)
{
    HANDLE hToken;
    DWORD dwLength;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hToken))
    {
        if (GetLastError() == ERROR_NO_TOKEN)
        {
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
            {
                return FALSE;
            }
        }
        else
        {
            return FALSE;
        }
    }


    if (!GetTokenInformation(hToken,       // handle of the access token
        TokenUser,    // type of information to retrieve
        m_TokenUser,   // address of retrieved information 
        0,            // size of the information buffer
        &dwLength     // address of required buffer size
        ))
    {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            m_TokenUser = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength);
            if (m_TokenUser == NULL)
            {
                return FALSE;
            }
        }
        else
        {
            return FALSE;
        }
    }

    if (!GetTokenInformation(hToken,     // handle of the access token
        TokenUser,  // type of information to retrieve
        m_TokenUser, // address of retrieved information 
        dwLength,   // size of the information buffer
        &dwLength   // address of required buffer size
        ))
    {
        HeapFree(GetProcessHeap(), 0, m_TokenUser);
        m_TokenUser = NULL;

        return FALSE;
    }

    *ppSidUser = m_TokenUser->User.Sid;
    return TRUE;
}

PipeSecurity::~PipeSecurity()
{
    if (m_IsAllocated)
    {
        if (m_SecurityDescriptor)
            HeapFree(GetProcessHeap(), 0, m_SecurityDescriptor);
        if (m_ACL)
            HeapFree(GetProcessHeap(), 0, m_ACL);
        if (m_TokenUser)
            HeapFree(GetProcessHeap(), 0, m_TokenUser);
        m_IsAllocated = FALSE;
    }
}

} // namespace details
} // namespace pipes
} // namespace net
