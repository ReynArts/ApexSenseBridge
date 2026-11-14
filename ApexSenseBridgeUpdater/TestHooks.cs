#if DEBUG
using System;
using System.IO;
using System.Text;
using ApexSenseBridge.Security;
using Microsoft.Win32;

namespace ApexSenseBridgeUpdater
{
    internal static class TestHooks
    {
        public static void Install(UpdaterOptions options)
        {
            UpdatePackage.SignatureTrustCheck = delegate(string path) { return !Contains(path, "ASB-TEST-UNSIGNED"); };
            UpdatePackage.InstalledTrustCheck = delegate(string path) { return true; };
            UpdatePackage.SignerSubjectReader = delegate(string path)
            {
                return Contains(path, "ASB-TEST-OTHER-SIGNER") ? "CN=ASB Test Other" : "CN=ASB Test Publisher";
            };

            var root = options.TestRegistryRoot;
            if (root == null) return;
            UpdatePackage.InstalledRegistryReader = delegate(string valueName)
            {
                using (var key = Registry.CurrentUser.OpenSubKey(root + "\\" + InstallRegistry.ProductKeyPath))
                {
                    return key == null ? null : key.GetValue(valueName) as string;
                }
            };
        }

        private static bool Contains(string path, string marker)
        {
            var text = Encoding.ASCII.GetString(File.ReadAllBytes(path));
            return text.IndexOf(marker, StringComparison.Ordinal) >= 0;
        }
    }
}
#endif
