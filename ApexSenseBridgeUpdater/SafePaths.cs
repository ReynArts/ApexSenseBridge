using System;
using System.IO;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text.RegularExpressions;

namespace ApexSenseBridgeUpdater
{
    internal static class SafePaths
    {
        private static readonly Regex DrivePrefix = new Regex(@"^[A-Za-z]:\\", RegexOptions.CultureInvariant);

        private const FileSystemRights WriteLikeRights =
            FileSystemRights.WriteData | FileSystemRights.AppendData | FileSystemRights.Delete |
            FileSystemRights.DeleteSubdirectoriesAndFiles | FileSystemRights.ChangePermissions |
            FileSystemRights.TakeOwnership | FileSystemRights.WriteExtendedAttributes;

        public static bool TryNormalizeLocalPath(string value, out string normalized)
        {
            normalized = null;
            if (string.IsNullOrWhiteSpace(value) || value.IndexOf('\0') >= 0) return false;
            var candidate = value.Replace('/', '\\');
            if (!DrivePrefix.IsMatch(candidate)) return false;
            if (candidate.IndexOf(':', 2) >= 0) return false;
            try
            {
                normalized = Path.GetFullPath(candidate);
            }
            catch (Exception)
            {
                return false;
            }
            return DrivePrefix.IsMatch(normalized);
        }

        public static bool IsReparsePoint(string path)
        {
            try
            {
                return (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0;
            }
            catch (FileNotFoundException)
            {
                return false;
            }
            catch (DirectoryNotFoundException)
            {
                return false;
            }
        }

        public static bool IsInside(string root, string path)
        {
            var prefix = root.TrimEnd('\\') + "\\";
            return path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
        }

        public static bool TryResolveRelative(string root, string relativePath, out string target)
        {
            target = null;
            if (string.IsNullOrWhiteSpace(relativePath) || relativePath.IndexOf('\0') >= 0 ||
                relativePath.IndexOf(':') >= 0 || relativePath.StartsWith("\\", StringComparison.Ordinal) ||
                relativePath.StartsWith("/", StringComparison.Ordinal))
            {
                return false;
            }

            foreach (var segment in relativePath.Replace('/', '\\').Split('\\'))
            {
                if (segment.Length == 0 || segment == "." || segment == ".." ||
                    segment != segment.TrimEnd(' ', '.'))
                {
                    return false;
                }
            }

            string full;
            try
            {
                full = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', '\\')));
            }
            catch (Exception)
            {
                return false;
            }
            if (!IsInside(root, full)) return false;
            target = full;
            return true;
        }

        public static bool ChainIsPlain(string root, string target, out string problem)
        {
            problem = null;
            if (IsReparsePoint(root))
            {
                problem = "The install directory is a link.";
                return false;
            }

            var rest = target.Substring(root.TrimEnd('\\').Length).Trim('\\');
            var current = root.TrimEnd('\\');
            var segments = rest.Length == 0 ? new string[0] : rest.Split('\\');
            foreach (var segment in segments)
            {
                current = current + "\\" + segment;
                if (!File.Exists(current) && !Directory.Exists(current)) return true;
                if (IsReparsePoint(current))
                {
                    problem = "A link was found inside the install directory at " + current.Substring(root.TrimEnd('\\').Length) + ".";
                    return false;
                }
            }
            return true;
        }

        public static bool IsWritableByNonAdministrators(string directory, out string detail)
        {
            detail = null;
            if (!OwnerIsTrusted(directory))
            {
                detail = "owner";
                return true;
            }

            var security = new DirectoryInfo(directory).GetAccessControl(AccessControlSections.Access);
            foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            {
                if (rule.AccessControlType != AccessControlType.Allow) continue;
                if ((rule.PropagationFlags & PropagationFlags.InheritOnly) != 0) continue;
                if ((rule.FileSystemRights & WriteLikeRights) == 0) continue;
                var sid = (SecurityIdentifier)rule.IdentityReference;
                if (sid.IsWellKnown(WellKnownSidType.LocalSystemSid) ||
                    sid.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid) ||
                    sid.IsWellKnown(WellKnownSidType.CreatorOwnerSid) ||
                    sid.Value.StartsWith("S-1-5-80-", StringComparison.Ordinal))
                {
                    continue;
                }
                detail = sid.Value;
                return true;
            }
            return false;
        }

        public static bool OwnerIsTrusted(string directory)
        {
            var owner = new DirectoryInfo(directory).GetAccessControl(AccessControlSections.Owner)
                .GetOwner(typeof(SecurityIdentifier)) as SecurityIdentifier;
            if (owner == null) return false;
            return owner.IsWellKnown(WellKnownSidType.LocalSystemSid) ||
                   owner.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid) ||
                   owner.Value.StartsWith("S-1-5-80-", StringComparison.Ordinal);
        }
    }
}
