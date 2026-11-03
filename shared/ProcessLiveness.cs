using System;
using System.Runtime.InteropServices;

namespace ApexSenseBridge.Common
{
    internal enum ProcessLivenessState
    {
        Alive,
        Exited,
        Replaced,
        Unknown
    }

    internal static class ProcessLiveness
    {
        private const uint ProcessQueryLimitedInformation = 0x1000;
        private const int StillActive = 259;
        private const int ErrorInvalidParameter = 87;
        private const int StatusInfoLengthMismatch = unchecked((int)0xC0000004);
        private const int SystemProcessInformationClass = 5;
        private static readonly object snapshotLock = new object();
        private static int snapshotBufferSize = 512 * 1024;

        public static ProcessLivenessState Check(uint processId, long expectedCreationUtcTicks)
        {
            long creationUtcTicks;
            return Check(processId, expectedCreationUtcTicks, out creationUtcTicks);
        }

        public static ProcessLivenessState Check(uint processId, long expectedCreationUtcTicks, out long creationUtcTicks)
        {
            creationUtcTicks = 0;
            if (processId == 0) return ProcessLivenessState.Exited;

            var handle = OpenProcess(ProcessQueryLimitedInformation, false, processId);
            if (handle != IntPtr.Zero)
            {
                try
                {
                    int exitCode;
                    if (GetExitCodeProcess(handle, out exitCode))
                    {
                        if (exitCode != StillActive) return ProcessLivenessState.Exited;
                        creationUtcTicks = ReadCreationUtcTicks(handle);
                        return Classify(expectedCreationUtcTicks, creationUtcTicks);
                    }
                }
                finally
                {
                    CloseHandle(handle);
                }
            }
            else if (Marshal.GetLastWin32Error() == ErrorInvalidParameter)
            {
                return ProcessLivenessState.Exited;
            }

            bool listed;
            if (TryFindInSnapshot(processId, out listed, out creationUtcTicks))
            {
                if (!listed) return ProcessLivenessState.Exited;
                return Classify(expectedCreationUtcTicks, creationUtcTicks);
            }

            var processIds = EnumerateProcessIds();
            if (processIds == null) return ProcessLivenessState.Unknown;
            return Array.IndexOf(processIds, processId) < 0
                ? ProcessLivenessState.Exited
                : ProcessLivenessState.Unknown;
        }

        public static bool IsRunning(uint processId, long expectedCreationUtcTicks)
        {
            var state = Check(processId, expectedCreationUtcTicks);
            return state == ProcessLivenessState.Alive || state == ProcessLivenessState.Unknown;
        }

        public static long TryGetCreationTimeUtcTicks(uint processId)
        {
            long creationUtcTicks;
            var state = Check(processId, 0, out creationUtcTicks);
            return state == ProcessLivenessState.Alive ? creationUtcTicks : 0;
        }

        private static ProcessLivenessState Classify(long expectedCreationUtcTicks, long creationUtcTicks)
        {
            if (expectedCreationUtcTicks > 0 && creationUtcTicks > 0 && expectedCreationUtcTicks != creationUtcTicks)
                return ProcessLivenessState.Replaced;
            return ProcessLivenessState.Alive;
        }

        private static long ReadCreationUtcTicks(IntPtr handle)
        {
            long creation, exit, kernel, user;
            if (!GetProcessTimes(handle, out creation, out exit, out kernel, out user) || creation <= 0) return 0;
            return ToUtcTicks(creation);
        }

        private static long ToUtcTicks(long fileTime)
        {
            try { return DateTime.FromFileTimeUtc(fileTime).Ticks; }
            catch (ArgumentOutOfRangeException) { return 0; }
        }

        private static bool TryFindInSnapshot(uint processId, out bool listed, out long creationUtcTicks)
        {
            listed = false;
            creationUtcTicks = 0;
            lock (snapshotLock)
            {
                for (int attempt = 0; attempt < 4; attempt++)
                {
                    var size = snapshotBufferSize;
                    var buffer = Marshal.AllocHGlobal(size);
                    try
                    {
                        int returned;
                        var status = NtQuerySystemInformation(SystemProcessInformationClass, buffer, size, out returned);
                        if (status == StatusInfoLengthMismatch)
                        {
                            snapshotBufferSize = Math.Max(size * 2, returned + 64 * 1024);
                            continue;
                        }
                        if (status < 0) return false;

                        long offset = 0;
                        while (offset >= 0 && offset < size)
                        {
                            var entry = (SystemProcessInformation)Marshal.PtrToStructure(
                                new IntPtr(buffer.ToInt64() + offset), typeof(SystemProcessInformation));
                            if ((uint)entry.UniqueProcessId.ToInt64() == processId)
                            {
                                listed = true;
                                creationUtcTicks = entry.CreateTime > 0 ? ToUtcTicks(entry.CreateTime) : 0;
                                return true;
                            }
                            if (entry.NextEntryOffset == 0) break;
                            offset += entry.NextEntryOffset;
                        }
                        return true;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            }
            return false;
        }

        private static uint[] EnumerateProcessIds()
        {
            var capacity = 1024;
            while (capacity <= 65536)
            {
                var processIds = new uint[capacity];
                uint bytesReturned;
                if (!EnumProcesses(processIds, (uint)(processIds.Length * sizeof(uint)), out bytesReturned)) return null;
                int count = (int)(bytesReturned / sizeof(uint));
                if (count < processIds.Length)
                {
                    var result = new uint[count];
                    Array.Copy(processIds, result, count);
                    return result;
                }
                capacity *= 2;
            }
            return null;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SystemProcessInformation
        {
            public uint NextEntryOffset;
            public uint NumberOfThreads;
            public long WorkingSetPrivateSize;
            public uint HardFaultCount;
            public uint NumberOfThreadsHighWatermark;
            public ulong CycleTime;
            public long CreateTime;
            public long UserTime;
            public long KernelTime;
            public ushort ImageNameLength;
            public ushort ImageNameMaximumLength;
            public IntPtr ImageNameBuffer;
            public int BasePriority;
            public IntPtr UniqueProcessId;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint desiredAccess, bool inheritHandle, uint processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetExitCodeProcess(IntPtr process, out int exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetProcessTimes(IntPtr process, out long creationTime, out long exitTime, out long kernelTime, out long userTime);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("ntdll.dll")]
        private static extern int NtQuerySystemInformation(int informationClass, IntPtr information, int informationLength, out int returnLength);

        [DllImport("psapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumProcesses([Out] uint[] processIds, uint size, out uint bytesReturned);
    }
}
