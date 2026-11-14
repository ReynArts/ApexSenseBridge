using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using ApexSenseBridge.Common;

namespace ApexSenseBridgeUpdater
{
    internal static class SessionGuard
    {
        private const string DefaultDiscoveryName = "Local\\ApexSenseBridge.ActiveSession.Info.v1";

        public static bool HasActiveSession(string discoveryName, out string description)
        {
            description = null;
            var name = string.IsNullOrEmpty(discoveryName) ? DefaultDiscoveryName : discoveryName;
            try
            {
                using (MemoryMappedFile.OpenExisting(name, MemoryMappedFileRights.Read))
                {
                }
            }
            catch (FileNotFoundException)
            {
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                description = "A bridge session exists but cannot be inspected.";
                return true;
            }
            catch (Exception)
            {
                return false;
            }

            var info = string.IsNullOrEmpty(discoveryName)
                ? BridgeSession.ReadActiveSession()
                : BridgeSession.ReadActiveSession(discoveryName);
            if (info == null) return false;
            if (info.Phase == SessionPhase.Stopped || info.Phase == SessionPhase.Failed) return false;
            description = "A bridge session is active (owner " + (info.Owner ?? "unknown") + ", phase " + info.Phase + ").";
            return true;
        }
    }
}
