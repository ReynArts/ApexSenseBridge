using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace ApexSenseBridgeUpdater
{
    internal static class ProcessControl
    {
        public static void WaitForExit(uint processId, TimeSpan timeout)
        {
            Process process;
            try
            {
                process = Process.GetProcessById((int)processId);
            }
            catch (ArgumentException)
            {
                return;
            }

            using (process)
            {
                try
                {
                    if (!process.WaitForExit((int)timeout.TotalMilliseconds))
                    {
                        UpdaterLog.Info("The calling process did not exit within " + (int)timeout.TotalSeconds + " s; continuing.");
                    }
                }
                catch (Exception ex)
                {
                    UpdaterLog.Info("Waiting for the calling process failed: " + ex.Message);
                }
            }
        }

        public static void RunEngineCommand(string installDirectory, string command)
        {
            var engine = Path.Combine(installDirectory, "ApexSenseBridge.exe");
            if (!File.Exists(engine) || SafePaths.IsReparsePoint(engine)) return;
            try
            {
                var info = new ProcessStartInfo(engine, command)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = installDirectory
                };
                using (var process = Process.Start(info))
                {
                    if (process == null) return;
                    if (!process.WaitForExit(30000))
                    {
                        UpdaterLog.Info("Engine command '" + command + "' timed out and was stopped.");
                        process.Kill();
                    }
                    else
                    {
                        UpdaterLog.Info("Engine command '" + command + "' exited with " + process.ExitCode + ".");
                    }
                }
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("Engine command '" + command + "' failed: " + ex.Message);
            }
        }

        public static void StopInstalledProcesses(string installDirectory)
        {
            StopImage(installDirectory, "ApexSenseBridgeTray");
            StopImage(installDirectory, "ApexSenseBridgeControl");
            RunEngineCommand(installDirectory, "stop-active-sessions");
            StopImage(installDirectory, "ApexSenseBridge");
            StopImage(installDirectory, "viiper");
        }

        public static bool IsImageRunning(string installDirectory, string name)
        {
            foreach (var process in Process.GetProcessesByName(name))
            {
                using (process)
                {
                    if (IsInstalledImage(process, installDirectory, name)) return true;
                }
            }
            return false;
        }

        private static void StopImage(string installDirectory, string name)
        {
            foreach (var process in Process.GetProcessesByName(name))
            {
                using (process)
                {
                    try
                    {
                        if (!IsInstalledImage(process, installDirectory, name)) continue;
                        UpdaterLog.Info("Stopping " + name + ".");
                        process.Kill();
                        process.WaitForExit(10000);
                    }
                    catch (Exception ex)
                    {
                        UpdaterLog.Info("Could not stop " + name + ": " + ex.Message);
                    }
                }
            }
        }

        private static bool IsInstalledImage(Process process, string installDirectory, string name)
        {
            var path = ReadImagePath(process.Id);
            return path != null &&
                   string.Equals(path, Path.Combine(installDirectory, name + ".exe"), StringComparison.OrdinalIgnoreCase);
        }

        private static string ReadImagePath(int processId)
        {
            var handle = OpenProcess(ProcessQueryLimitedInformation, false, processId);
            if (handle == IntPtr.Zero) return null;
            try
            {
                var builder = new StringBuilder(1024);
                var size = builder.Capacity;
                return QueryFullProcessImageName(handle, 0, builder, ref size) ? builder.ToString() : null;
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        private const uint ProcessQueryLimitedInformation = 0x1000;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint access, bool inherit, int processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder name, ref int size);
    }
}
