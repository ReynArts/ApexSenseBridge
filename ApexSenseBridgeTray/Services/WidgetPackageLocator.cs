using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;

namespace ApexSenseBridgeTray.Services
{
    internal static class WidgetPackageLocator
    {
        internal const string IdentityName = "ReynArts.ApexSenseBridgeWidget";
        private const string RepositoryKey =
            @"Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages";

        internal static string FamilyFromFullName(string fullName)
        {
            if (string.IsNullOrEmpty(fullName)) return null;
            var parts = fullName.Split('_');
            if (parts.Length != 5 || !string.Equals(parts[0], IdentityName, StringComparison.OrdinalIgnoreCase)) return null;
            var publisherId = parts[4];
            if (publisherId.Length != 13) return null;
            foreach (var character in publisherId)
            {
                var digit = character >= '0' && character <= '9';
                var letter = character >= 'a' && character <= 'z';
                if (!digit && !letter) return null;
            }
            return parts[0] + "_" + publisherId;
        }

        internal static string[] FamiliesFromFullNames(IEnumerable<string> fullNames)
        {
            var result = new List<string>();
            if (fullNames == null) return result.ToArray();
            foreach (var fullName in fullNames)
            {
                var family = FamilyFromFullName(fullName);
                if (family != null && !result.Contains(family, StringComparer.OrdinalIgnoreCase)) result.Add(family);
            }
            return result.ToArray();
        }

        internal static string[] FindFamilies()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(RepositoryKey))
                {
                    return key == null ? new string[0] : FamiliesFromFullNames(key.GetSubKeyNames());
                }
            }
            catch
            {
                return new string[0];
            }
        }
    }
}
