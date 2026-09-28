using System.Runtime.InteropServices;

namespace Wsddm;

/// <summary>
/// Validates passwords against the local OS account via LogonUser.
/// Works from a normal (non-admin) user-mode process; the OS does the checking,
/// so a wrong password can never succeed and nothing is stored.
/// </summary>
public static class Unlock
{
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool LogonUser(
        string lpszUsername, string lpszDomain, string lpszPassword,
        int dwLogonType, int dwLogonProvider, out IntPtr phToken);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const int LOGON32_LOGON_NETWORK = 3;
    private const int LOGON32_PROVIDER_DEFAULT = 0;

    public static bool Validate(string password)
    {
        if (!LogonUser(Environment.UserName, ".", password,
                LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, out var token))
            return false;
        CloseHandle(token);
        return true;
    }
}