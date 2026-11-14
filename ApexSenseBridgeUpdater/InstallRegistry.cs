using System;
using System.Collections.Generic;
using Microsoft.Win32;

namespace ApexSenseBridgeUpdater
{
    internal sealed class InstallRegistry
    {
        public const string ProductKeyPath = @"SOFTWARE\ApexSenseBridge";
        public const string UninstallKeyPath =
            @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{5F8B1901-93E1-41E2-96B4-F1B278A5A630}_is1";
        private const string DisplayNamePrefix = "ApexSenseBridge ";

        private readonly string testRoot;
        private readonly List<Action> undo = new List<Action>();

        public InstallRegistry(string testRoot)
        {
            this.testRoot = testRoot;
        }

        public string ReadInstallPath()
        {
            using (var baseKey = OpenBase())
            using (var key = baseKey.OpenSubKey(ProductKeyPath, false))
            {
                return key == null ? null : key.GetValue("InstallPath") as string;
            }
        }

        public void WriteVersion(string version)
        {
            using (var baseKey = OpenBase())
            {
                using (var key = baseKey.OpenSubKey(ProductKeyPath, true))
                {
                    if (key == null) throw new InvalidOperationException("The ApexSenseBridge registry key is missing.");
                    SetString(key, ProductKeyPath, "Version", version);
                }

                using (var key = baseKey.OpenSubKey(UninstallKeyPath, true))
                {
                    if (key == null)
                    {
                        UpdaterLog.Info("The setup uninstall entry was not found; its DisplayVersion was left alone.");
                        return;
                    }
                    var oldVersion = key.GetValue("DisplayVersion") as string;
                    var oldName = key.GetValue("DisplayName") as string;
                    SetString(key, UninstallKeyPath, "DisplayVersion", version);
                    if (!string.IsNullOrEmpty(oldVersion) && string.Equals(oldName, DisplayNamePrefix + oldVersion, StringComparison.Ordinal))
                    {
                        SetString(key, UninstallKeyPath, "DisplayName", DisplayNamePrefix + version);
                    }
                }
            }
        }

        public bool Rollback()
        {
            var ok = true;
            for (var i = undo.Count - 1; i >= 0; i--)
            {
                try
                {
                    undo[i]();
                }
                catch (Exception ex)
                {
                    ok = false;
                    UpdaterLog.Info("Registry rollback step failed: " + ex.Message);
                }
            }
            undo.Clear();
            return ok;
        }

        private RegistryKey OpenBase()
        {
            if (testRoot != null) return Registry.CurrentUser.CreateSubKey(testRoot);
            return RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
        }

        private void SetString(RegistryKey key, string path, string name, string value)
        {
            var previous = key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
            var kind = previous == null ? RegistryValueKind.String : key.GetValueKind(name);
            undo.Add(delegate
            {
                using (var baseKey = OpenBase())
                using (var target = baseKey.OpenSubKey(path, true))
                {
                    if (target == null) return;
                    if (previous == null) target.DeleteValue(name, false);
                    else target.SetValue(name, previous, kind);
                }
            });
            key.SetValue(name, value, RegistryValueKind.String);
        }
    }
}
