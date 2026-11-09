using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class WidgetControlServer : IDisposable
    {
        internal const string DefaultPipeName = "ApexSenseBridge.Widget.v1";
        internal const int MaximumRequestBytes = 4096;
        internal const int ClientTimeoutMilliseconds = 2000;
        internal static readonly string[] KnownPackageFamilies =
        {
            "ReynArts.ApexSenseBridgeWidget_tcqvdfrmphh62"
        };

        private readonly WidgetControlService service;
        private readonly string pipeName;
        private readonly string[] packageFamilies;
        private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
        private Thread thread;
        private bool disposed;

        internal WidgetControlServer(WidgetControlService service, string pipeName, IEnumerable<string> packageFamilies)
        {
            this.service = service;
            this.pipeName = pipeName;
            this.packageFamilies = new List<string>(packageFamilies ?? new string[0]).ToArray();
        }

        internal void Start()
        {
            if (thread != null) return;
            thread = new Thread(Run) { IsBackground = true, Name = "WidgetControlServer" };
            thread.Start();
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            stopEvent.Set();
            if (thread != null && thread.IsAlive) thread.Join(1000);
        }

        internal PipeSecurity BuildSecurity()
        {
            var security = new PipeSecurity();
            security.AddAccessRule(new PipeAccessRule(
                WindowsIdentity.GetCurrent().User, PipeAccessRights.FullControl, AccessControlType.Allow));
            foreach (var family in packageFamilies)
            {
                var sid = DeriveAppContainerSid(family);
                if (sid != null)
                    security.AddAccessRule(new PipeAccessRule(sid, PipeAccessRights.ReadWrite, AccessControlType.Allow));
            }
            security.AddAccessRule(new PipeAccessRule(
                new SecurityIdentifier(WellKnownSidType.NetworkSid, null), PipeAccessRights.FullControl, AccessControlType.Deny));
            return security;
        }

        private void Run()
        {
            PipeSecurity security;
            try { security = BuildSecurity(); }
            catch { return; }
            while (!stopEvent.WaitOne(0))
            {
                try
                {
                    using (var pipe = new NamedPipeServerStream(
                        pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous, 4096, 4096, security))
                    {
                        var connecting = pipe.BeginWaitForConnection(null, null);
                        if (WaitHandle.WaitAny(new[] { connecting.AsyncWaitHandle, stopEvent }) != 0) return;
                        pipe.EndWaitForConnection(connecting);
                        Serve(pipe);
                    }
                }
                catch (Exception)
                {
                    if (stopEvent.WaitOne(500)) return;
                }
            }
        }

        private void Serve(NamedPipeServerStream pipe)
        {
            try
            {
                var line = ReadLine(pipe);
                if (line == null) return;
                var reply = service.Handle(line);
                var bytes = new UTF8Encoding(false).GetBytes(reply + "\n");
                pipe.Write(bytes, 0, bytes.Length);
                pipe.Flush();
                pipe.WaitForPipeDrain();
            }
            catch (IOException) { }
            finally
            {
                try { if (pipe.IsConnected) pipe.Disconnect(); } catch { }
            }
        }

        private static string ReadLine(Stream pipe)
        {
            var buffer = new byte[MaximumRequestBytes];
            var count = 0;
            var one = new byte[1];
            while (count < buffer.Length)
            {
                var read = pipe.BeginRead(one, 0, 1, null, null);
                if (!read.AsyncWaitHandle.WaitOne(ClientTimeoutMilliseconds)) return null;
                if (pipe.EndRead(read) == 0) break;
                if (one[0] == (byte)'\n') break;
                buffer[count++] = one[0];
            }
            return count == 0 ? null : Encoding.UTF8.GetString(buffer, 0, count).TrimEnd('\r');
        }

        [DllImport("userenv.dll", CharSet = CharSet.Unicode)]
        private static extern int DeriveAppContainerSidFromAppContainerName(string name, out IntPtr sid);

        [DllImport("advapi32.dll")]
        private static extern IntPtr FreeSid(IntPtr sid);

        internal static SecurityIdentifier DeriveAppContainerSid(string packageFamilyName)
        {
            IntPtr raw;
            if (DeriveAppContainerSidFromAppContainerName(packageFamilyName, out raw) != 0 || raw == IntPtr.Zero) return null;
            try { return new SecurityIdentifier(raw); }
            finally { FreeSid(raw); }
        }
    }
}
