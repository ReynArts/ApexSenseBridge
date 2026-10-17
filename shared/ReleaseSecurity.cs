using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

namespace ApexSenseBridge.Security
{
    public static class ReleaseSecurity
    {
        private const string ExpectedAssetName = "ApexSenseBridge-Setup.exe";
        private const string ExpectedDownloadPrefix =
            "/ReynArts/ApexSenseBridge/releases/download/";

        private static readonly Guid WinTrustActionGenericVerifyV2 =
            new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustFileInfo
        {
            public uint StructSize;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string FilePath;
            public IntPtr FileHandle;
            public IntPtr KnownSubject;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustData
        {
            public uint StructSize;
            public IntPtr PolicyCallbackData;
            public IntPtr SipClientData;
            public uint UiChoice;
            public uint RevocationChecks;
            public uint UnionChoice;
            public IntPtr FileInfo;
            public uint StateAction;
            public IntPtr StateData;
            public IntPtr UrlReference;
            public uint ProviderFlags;
            public uint UiContext;
        }

        [DllImport("wintrust.dll", ExactSpelling = true, SetLastError = true,
            CharSet = CharSet.Unicode)]
        private static extern uint WinVerifyTrust(
            IntPtr windowHandle,
            [In] ref Guid actionId,
            [In] ref WinTrustData trustData);

        public static bool IsExpectedSetupAsset(string name, string downloadUrl)
        {
            if (!string.Equals(name, ExpectedAssetName,
                StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            Uri uri;
            return Uri.TryCreate(downloadUrl, UriKind.Absolute, out uri) &&
                   string.Equals(uri.Scheme, Uri.UriSchemeHttps,
                       StringComparison.OrdinalIgnoreCase) &&
                   string.Equals(uri.Host, "github.com",
                       StringComparison.OrdinalIgnoreCase) &&
                   uri.AbsolutePath.StartsWith(ExpectedDownloadPrefix,
                       StringComparison.OrdinalIgnoreCase);
        }

        public static bool VerifyInstaller(
            string installerPath,
            string expectedVersion,
            string currentBinaryPath,
            out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(installerPath) ||
                !File.Exists(installerPath))
            {
                error = "The downloaded setup file is missing.";
                return false;
            }

            // Inno Setup pads version resource strings with trailing spaces.
            var versionInfo = FileVersionInfo.GetVersionInfo(installerPath);
            string productName = (versionInfo.ProductName ?? string.Empty).Trim();
            string productVersion = (versionInfo.ProductVersion ?? string.Empty).Trim();
            if (!string.Equals(productName, "ApexSenseBridge",
                StringComparison.OrdinalIgnoreCase))
            {
                error = "The downloaded executable is not an ApexSenseBridge setup.";
                return false;
            }

            if (!string.IsNullOrWhiteSpace(expectedVersion))
            {
                Version expected;
                Version actual;
                if (!Version.TryParse(expectedVersion.TrimStart('v', 'V'), out expected) ||
                    !Version.TryParse(productVersion, out actual) ||
                    expected.Major != actual.Major ||
                    expected.Minor != actual.Minor ||
                    expected.Build != actual.Build)
                {
                    error = "The setup version does not match the selected GitHub release.";
                    return false;
                }
            }

            if (!HasTrustedAuthenticodeSignature(installerPath))
            {
                error = "The setup Authenticode signature is missing or untrusted.";
                return false;
            }

            try
            {
                if (string.IsNullOrWhiteSpace(currentBinaryPath) ||
                    !File.Exists(currentBinaryPath))
                {
                    error = "The installed updater binary could not be identified.";
                    return false;
                }

                using (var installerCertificate = new X509Certificate2(
                    X509Certificate.CreateFromSignedFile(installerPath)))
                using (var currentCertificate = new X509Certificate2(
                    X509Certificate.CreateFromSignedFile(currentBinaryPath)))
                {
                    if (!string.Equals(installerCertificate.Subject,
                        currentCertificate.Subject,
                        StringComparison.OrdinalIgnoreCase))
                    {
                        error = "The setup signer differs from the installed ApexSenseBridge publisher.";
                        return false;
                    }
                }
            }
            catch (Exception ex)
            {
                error = "The setup publisher could not be verified: " + ex.Message;
                return false;
            }

            return true;
        }

        private static bool HasTrustedAuthenticodeSignature(string path)
        {
            var fileInfo = new WinTrustFileInfo
            {
                StructSize = (uint)Marshal.SizeOf(typeof(WinTrustFileInfo)),
                FilePath = path,
                FileHandle = IntPtr.Zero,
                KnownSubject = IntPtr.Zero
            };

            IntPtr fileInfoPointer = IntPtr.Zero;
            try
            {
                fileInfoPointer = Marshal.AllocCoTaskMem(
                    Marshal.SizeOf(typeof(WinTrustFileInfo)));
                Marshal.StructureToPtr(fileInfo, fileInfoPointer, false);

                var trustData = new WinTrustData
                {
                    StructSize = (uint)Marshal.SizeOf(typeof(WinTrustData)),
                    UiChoice = 2, // WTD_UI_NONE
                    RevocationChecks = 0, // WTD_REVOKE_NONE
                    UnionChoice = 1, // WTD_CHOICE_FILE
                    FileInfo = fileInfoPointer,
                    StateAction = 0,
                    ProviderFlags = 0x00001000 // WTD_CACHE_ONLY_URL_RETRIEVAL
                };
                var action = WinTrustActionGenericVerifyV2;
                return WinVerifyTrust(new IntPtr(-1), ref action, ref trustData) == 0;
            }
            finally
            {
                if (fileInfoPointer != IntPtr.Zero)
                {
                    Marshal.DestroyStructure(fileInfoPointer,
                        typeof(WinTrustFileInfo));
                    Marshal.FreeCoTaskMem(fileInfoPointer);
                }
            }
        }
    }
}
