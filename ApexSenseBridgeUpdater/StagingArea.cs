using System;
using System.IO;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;

namespace ApexSenseBridgeUpdater
{
    internal sealed class StagingArea : IDisposable
    {
        public const long MaxPackageBytes = 200L * 1024 * 1024;

        private FileStream held;

        public string Directory { get; private set; }
        public string PackageCopy { get; private set; }
        public bool FailureIsPackageProblem { get; private set; }

        private StagingArea()
        {
        }

        public static StagingArea Create(string programDataDirectory, bool testMode, out string error)
        {
            error = null;
            try
            {
                var updatesRoot = Path.Combine(programDataDirectory, "updates");
                if (System.IO.Directory.Exists(updatesRoot))
                {
                    if (SafePaths.IsReparsePoint(updatesRoot))
                    {
                        error = "The update staging root is a link.";
                        return null;
                    }
                    if (!testMode && !SafePaths.OwnerIsTrusted(updatesRoot))
                    {
                        error = "The update staging root is not owned by an administrator.";
                        return null;
                    }
                    new DirectoryInfo(updatesRoot).SetAccessControl(BuildSecurity(testMode));
                }
                else
                {
                    System.IO.Directory.CreateDirectory(updatesRoot, BuildSecurity(testMode));
                }

                RemoveStale(updatesRoot);

                var directory = Path.Combine(updatesRoot, Guid.NewGuid().ToString("N"));
                if (System.IO.Directory.Exists(directory))
                {
                    error = "The update staging directory already exists.";
                    return null;
                }
                System.IO.Directory.CreateDirectory(directory, BuildSecurity(testMode));
                if (SafePaths.IsReparsePoint(directory))
                {
                    error = "The update staging directory is a link.";
                    return null;
                }

                var applied = new DirectoryInfo(directory).GetAccessControl(AccessControlSections.Access | AccessControlSections.Owner);
                if (!applied.AreAccessRulesProtected || (!testMode && !HasOnlyAdministrativeRules(applied)))
                {
                    error = "The update staging directory is not restricted to administrators.";
                    System.IO.Directory.Delete(directory, true);
                    return null;
                }
                UpdaterLog.Info("Staging ACL " + applied.GetSecurityDescriptorSddlForm(AccessControlSections.Access));

                return new StagingArea { Directory = directory, PackageCopy = Path.Combine(directory, "package.zip") };
            }
            catch (Exception ex)
            {
                error = "The update staging directory could not be prepared: " + ex.Message;
                return null;
            }
        }

        public bool CopyPackage(string sourcePath, out string error)
        {
            error = null;
            try
            {
                byte[] writtenHash;
                using (var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    if (source.Length > MaxPackageBytes)
                    {
                        error = "The update package is larger than the allowed size.";
                        FailureIsPackageProblem = true;
                        return false;
                    }
                    using (var destination = new FileStream(PackageCopy, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    using (var sha = SHA256.Create())
                    {
                        var buffer = new byte[81920];
                        long total = 0;
                        int read;
                        while ((read = source.Read(buffer, 0, buffer.Length)) > 0)
                        {
                            total += read;
                            if (total > MaxPackageBytes)
                            {
                                error = "The update package is larger than the allowed size.";
                                FailureIsPackageProblem = true;
                                return false;
                            }
                            sha.TransformBlock(buffer, 0, read, null, 0);
                            destination.Write(buffer, 0, read);
                        }
                        sha.TransformFinalBlock(buffer, 0, 0);
                        writtenHash = sha.Hash;
                        destination.Flush(true);
                    }
                }

                held = new FileStream(PackageCopy, FileMode.Open, FileAccess.Read, FileShare.Read);
                using (var sha = SHA256.Create())
                {
                    var rereadHash = sha.ComputeHash(held);
                    held.Position = 0;
                    if (!HashesEqual(writtenHash, rereadHash))
                    {
                        error = "The staged update package changed while it was being secured.";
                        FailureIsPackageProblem = true;
                        return false;
                    }
                }
                return true;
            }
            catch (Exception ex)
            {
                error = "The update package could not be staged: " + ex.Message;
                return false;
            }
        }

        public void Dispose()
        {
            if (held != null)
            {
                held.Dispose();
                held = null;
            }
            try
            {
                if (Directory != null && System.IO.Directory.Exists(Directory)) System.IO.Directory.Delete(Directory, true);
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("Staging cleanup failed: " + ex.Message);
            }
        }

        private static DirectorySecurity BuildSecurity(bool testMode)
        {
            var security = new DirectorySecurity();
            security.SetAccessRuleProtection(true, false);
            AddFullControl(security, new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null));
            AddFullControl(security, new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null));
#if DEBUG
            if (testMode) AddFullControl(security, WindowsIdentity.GetCurrent().User);
#endif
            return security;
        }

        private static void AddFullControl(DirectorySecurity security, SecurityIdentifier sid)
        {
            security.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                PropagationFlags.None, AccessControlType.Allow));
        }

        private static bool HasOnlyAdministrativeRules(DirectorySecurity security)
        {
            var any = false;
            foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            {
                var sid = (SecurityIdentifier)rule.IdentityReference;
                if (rule.AccessControlType != AccessControlType.Allow ||
                    !(sid.IsWellKnown(WellKnownSidType.LocalSystemSid) ||
                      sid.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid)))
                {
                    return false;
                }
                any = true;
            }
            return any;
        }

        private static bool HashesEqual(byte[] left, byte[] right)
        {
            if (left == null || right == null || left.Length != right.Length) return false;
            var difference = 0;
            for (var i = 0; i < left.Length; i++) difference |= left[i] ^ right[i];
            return difference == 0;
        }

        private static void RemoveStale(string updatesRoot)
        {
            try
            {
                foreach (var directory in System.IO.Directory.GetDirectories(updatesRoot))
                {
                    Guid ignored;
                    if (!Guid.TryParseExact(Path.GetFileName(directory), "N", out ignored)) continue;
                    if (SafePaths.IsReparsePoint(directory)) continue;
                    if (DateTime.UtcNow - System.IO.Directory.GetCreationTimeUtc(directory) < TimeSpan.FromDays(1)) continue;
                    System.IO.Directory.Delete(directory, true);
                }
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("Stale staging cleanup skipped: " + ex.Message);
            }
        }
    }
}
