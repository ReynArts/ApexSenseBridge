using ApexSenseBridgeTray.Services;
using System;
using System.Collections.Generic;
using System.Threading;

namespace ApexSenseBridge
{
    internal sealed class WidgetHost : IDisposable
    {
        private const string TrayMutexName = @"Global\ApexSenseBridgeTrayMutex";
        private readonly WidgetControlServer server;

        internal WidgetHost(Func<string> languageCode)
        {
            server = new WidgetControlServer(
                new WidgetControlService(new LiveWidgetBackend(), null, BuildRemapEntries, null, languageCode),
                WidgetControlServer.DefaultPipeName,
                WidgetControlServer.KnownPackageFamilies,
                WidgetPackageLocator.FindFamilies,
                () => !TrayRunning());
        }

        internal void Start()
        {
            server.Start();
        }

        public void Dispose()
        {
            server.Dispose();
        }

        internal static bool TrayRunning()
        {
            try
            {
                Mutex existing;
                if (!Mutex.TryOpenExisting(TrayMutexName, out existing)) return false;
                existing.Dispose();
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static object[] BuildRemapEntries(string profile)
        {
            var entries = new List<object>();
            foreach (var entry in TouchpadRemapGuide.BuildEntries(profile))
            {
                entries.Add(new Dictionary<string, object>
                {
                    { "buttons", entry.Buttons },
                    { "hold", entry.Hold },
                    { "holdLabel", string.Empty },
                    { "output", entry.Output.ToString() },
                    { "outputLabel", string.Empty },
                    { "use", entry.UseText ?? string.Empty }
                });
            }
            return entries.ToArray();
        }
    }
}
