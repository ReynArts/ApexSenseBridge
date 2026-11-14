using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace ApexSenseBridge.Common
{
    internal sealed class SessionTuningChange
    {
        public uint? TriggerStrengthPercent { get; set; }
        public uint? VibrationStrengthPercent { get; set; }
        public bool? GripMute { get; set; }
        public bool? TriggerEffectsOff { get; set; }
        public uint? TouchpadProfile { get; set; }
        public uint? ApexProfileSlot { get; set; }
    }

    internal sealed class SessionTuningState
    {
        public uint RequestSequence { get; set; }
        public uint TriggerStrengthPercent { get; set; }
        public uint VibrationStrengthPercent { get; set; }
        public bool GripMute { get; set; }
        public bool TriggerEffectsOff { get; set; }
        public uint TouchpadProfile { get; set; }
        public uint? ApexProfileSlot { get; set; }
        public uint SampleRateHz { get; set; }
        public uint ControllerModel { get; set; }
    }

    internal sealed class SessionTuningClient : IDisposable
    {
        private const uint Magic = 0x54425341;
        private const uint Version = 1;
        private const uint Keep = 0xFFFFFFFFu;
        private const uint NoSlot = 0xFFu;
        private const int BlockSize = 128;
        private const int RequestOffset = 8;
        private const int AppliedOffset = 40;
        private const int WriterTimeoutMilliseconds = 250;
        private const int ReadAttempts = 8;

        private readonly MemoryMappedFile mapping;
        private readonly MemoryMappedViewAccessor view;
        private readonly string token;
        private bool disposed;

        private SessionTuningClient(string token, MemoryMappedFile mapping, MemoryMappedViewAccessor view)
        {
            this.token = token;
            this.mapping = mapping;
            this.view = view;
        }

        internal static string MappingName(string token)
        {
            return "Local\\ApexSenseBridge.Session." + token + ".Tuning";
        }

        internal static string WriterLockName(string token)
        {
            return "Local\\ApexSenseBridge.Session." + token + ".Tuning.Lock";
        }

        public static SessionTuningClient Open(string token)
        {
            if (token == null || token.Length != 32) return null;
            MemoryMappedFile mapping = null;
            try
            {
                mapping = MemoryMappedFile.OpenExisting(MappingName(token), MemoryMappedFileRights.ReadWrite);
                var view = mapping.CreateViewAccessor(0, BlockSize, MemoryMappedFileAccess.ReadWrite);
                if (view.ReadUInt32(0) != Magic || view.ReadUInt32(4) != Version)
                {
                    view.Dispose();
                    mapping.Dispose();
                    return null;
                }
                return new SessionTuningClient(token, mapping, view);
            }
            catch (FileNotFoundException) { if (mapping != null) mapping.Dispose(); return null; }
            catch (UnauthorizedAccessException) { if (mapping != null) mapping.Dispose(); return null; }
            catch (IOException) { if (mapping != null) mapping.Dispose(); return null; }
        }

        public bool Submit(SessionTuningChange change)
        {
            if (change == null) throw new ArgumentNullException("change");
            if (disposed) return false;
            using (var writerLock = new Mutex(false, WriterLockName(token)))
            {
                try
                {
                    if (!writerLock.WaitOne(WriterTimeoutMilliseconds)) return false;
                }
                catch (AbandonedMutexException) { }
                try
                {
                    var current = view.ReadUInt32(RequestOffset);
                    var odd = (current & ~1u) + 1u;
                    view.Write(RequestOffset, odd);
                    Thread.MemoryBarrier();
                    view.Write(RequestOffset + 4, change.TriggerStrengthPercent ?? Keep);
                    view.Write(RequestOffset + 8, change.VibrationStrengthPercent ?? Keep);
                    view.Write(RequestOffset + 12, FlagValue(change.GripMute));
                    view.Write(RequestOffset + 16, FlagValue(change.TriggerEffectsOff));
                    view.Write(RequestOffset + 20, change.TouchpadProfile ?? Keep);
                    view.Write(RequestOffset + 24, change.ApexProfileSlot ?? Keep);
                    Thread.MemoryBarrier();
                    view.Write(RequestOffset, odd + 1u);
                    return true;
                }
                finally { writerLock.ReleaseMutex(); }
            }
        }

        public SessionTuningState ReadApplied()
        {
            if (disposed) return null;
            for (var attempt = 0; attempt < ReadAttempts; attempt++)
            {
                var before = view.ReadUInt32(AppliedOffset);
                if ((before & 1u) != 0u) { Thread.Yield(); continue; }
                Thread.MemoryBarrier();
                var state = new SessionTuningState
                {
                    RequestSequence = view.ReadUInt32(AppliedOffset + 4),
                    TriggerStrengthPercent = view.ReadUInt32(AppliedOffset + 8),
                    VibrationStrengthPercent = view.ReadUInt32(AppliedOffset + 12),
                    GripMute = view.ReadUInt32(AppliedOffset + 16) == 1u,
                    TriggerEffectsOff = view.ReadUInt32(AppliedOffset + 20) == 1u,
                    TouchpadProfile = view.ReadUInt32(AppliedOffset + 24),
                    SampleRateHz = view.ReadUInt32(AppliedOffset + 32),
                    ControllerModel = view.ReadUInt32(AppliedOffset + 36)
                };
                var slot = view.ReadUInt32(AppliedOffset + 28);
                state.ApexProfileSlot = slot == NoSlot ? (uint?)null : slot;
                Thread.MemoryBarrier();
                if (view.ReadUInt32(AppliedOffset) == before && before != 0u) return state;
                if (before == 0u) return null;
            }
            return null;
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            view.Dispose();
            mapping.Dispose();
        }

        private static uint FlagValue(bool? value)
        {
            if (!value.HasValue) return Keep;
            return value.Value ? 1u : 0u;
        }
    }
}
