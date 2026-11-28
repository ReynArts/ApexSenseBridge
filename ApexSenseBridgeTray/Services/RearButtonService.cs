using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace ApexSenseBridgeTray.Services
{
    public enum RearButton
    {
        M1,
        M2,
        M3,
        M4,
        LM,
        RM,
    }

    public struct RearButtonEvent
    {
        public RearButton Button;
        public bool Pressed;
    }

    // Streams Apex 5 rear/side button edges (M1-M4, LM, RM) by launching the
    // native engine's read-only "test-rear --stream" diagnostic. This never
    // opens the HID interface directly from the Tray process: the engine owns
    // the single verified reader, exactly like StartGyroStream.
    public sealed class RearButtonService
    {
        private readonly object gate = new object();
        private CancellationTokenSource streamCts;

        public void Start(Action<RearButtonEvent> onEvent, Action<string> onError = null)
        {
            Stop();

            var engine = InstallLocator.ResolveEngine();
            if (string.IsNullOrWhiteSpace(engine) || !System.IO.File.Exists(engine))
            {
                if (onError != null) onError("ApexSenseBridge.exe was not found.");
                return;
            }

            var cancellation = new CancellationTokenSource();
            lock (gate) { streamCts = cancellation; }
            var token = cancellation.Token;

            Task.Run(() =>
            {
                BridgeSession session = null;
                try
                {
                    token.ThrowIfCancellationRequested();
                    string error;
                    session = BridgeSession.TryStart(
                        engine,
                        "test-rear --stream",
                        TimeSpan.FromSeconds(8),
                        line =>
                        {
                            if (token.IsCancellationRequested) return;
                            const string prefix = "[bridge] ";
                            if (line.StartsWith(prefix)) line = line.Substring(prefix.Length);
                            foreach (var evt in ParseRearLine(line))
                            {
                                if (onEvent != null) onEvent(evt);
                            }
                        },
                        line => AppLog.WriteLine("rear_button_test.log", line),
                        out error,
                        discoveryName: "Local\\ApexSenseBridge.RearButtonDiagnostic.Info.v1");

                    if (session == null)
                    {
                        if (!token.IsCancellationRequested && onError != null) onError(error);
                        return;
                    }

                    while (!token.IsCancellationRequested)
                    {
                        var status = session.ReadStatus();
                        if (status.Phase == SessionPhase.Failed || status.Phase == SessionPhase.Stopped)
                        {
                            if (onError != null) onError(status.Message);
                            break;
                        }
                        Thread.Sleep(50);
                    }
                }
                catch (OperationCanceledException) { }
                catch (Exception ex)
                {
                    if (!token.IsCancellationRequested && onError != null) onError(ex.Message);
                }
                finally
                {
                    if (session != null)
                    {
                        session.StopAndEnsureExit(TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(2));
                        session.Dispose();
                    }
                    cancellation.Dispose();
                }
            });
        }

        public void Stop()
        {
            CancellationTokenSource cancellation;
            lock (gate)
            {
                cancellation = streamCts;
                streamCts = null;
            }
            try
            {
                if (cancellation != null) cancellation.Cancel();
            }
            catch (ObjectDisposedException) { }
        }

        // Parses one "REAR: RM=0 LM=0 M1=1 M2=0 M3=0 M4=0" line into edge events
        // by comparing against the previous known state. The engine only prints
        // a line when at least one bit changed, so every field that differs
        // from the tracked state is a genuine press or release.
        // The engine starts every stream from all-released (byte 14 == 0) and
        // prints only on change, so this is the correct baseline; a button that
        // is already down produces its own first PRESS line.
        private bool rm, lm, m1, m2, m3, m4;

        internal System.Collections.Generic.IEnumerable<RearButtonEvent> ParseRearLine(string line)
        {
            var events = new System.Collections.Generic.List<RearButtonEvent>();
            if (string.IsNullOrWhiteSpace(line) || !line.StartsWith("REAR:")) return events;

            bool newRm = rm, newLm = lm, newM1 = m1, newM2 = m2, newM3 = m3, newM4 = m4;
            var parts = line.Substring(5).Trim().Split(' ');
            foreach (var part in parts)
            {
                var kv = part.Split('=');
                if (kv.Length != 2) continue;
                bool value = kv[1].Trim() == "1";
                switch (kv[0].Trim())
                {
                    case "RM": newRm = value; break;
                    case "LM": newLm = value; break;
                    case "M1": newM1 = value; break;
                    case "M2": newM2 = value; break;
                    case "M3": newM3 = value; break;
                    case "M4": newM4 = value; break;
                }
            }

            AddIfChanged(events, RearButton.RM, ref rm, newRm);
            AddIfChanged(events, RearButton.LM, ref lm, newLm);
            AddIfChanged(events, RearButton.M1, ref m1, newM1);
            AddIfChanged(events, RearButton.M2, ref m2, newM2);
            AddIfChanged(events, RearButton.M3, ref m3, newM3);
            AddIfChanged(events, RearButton.M4, ref m4, newM4);
            return events;
        }

        private static void AddIfChanged(
            System.Collections.Generic.List<RearButtonEvent> events,
            RearButton button, ref bool previous, bool current)
        {
            if (previous == current) return;
            previous = current;
            events.Add(new RearButtonEvent { Button = button, Pressed = current });
        }
    }
}