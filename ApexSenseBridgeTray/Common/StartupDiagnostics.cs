using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace ApexSenseBridgeTray.Common
{
    internal static class StartupDiagnostics
    {
        internal const int MaxPhases = 32;
        private const long Megabyte = 1024 * 1024;
        private const int MaxRegions = 65536;

        private static readonly object syncRoot = new object();
        private static readonly string[] phaseNames = new string[MaxPhases];
        private static readonly long[] phaseElapsedMs = new long[MaxPhases];
        private static readonly long[] phasePrivateBytes = new long[MaxPhases];
        private static readonly long[] phaseManagedBytes = new long[MaxPhases];
        private static readonly DateTime processStartUtc = ReadProcessStartUtc();
        private static int phaseCount;
        private static bool completed;

        public static string CurrentPhase
        {
            get
            {
                lock (syncRoot)
                {
                    return phaseCount > 0 ? phaseNames[phaseCount - 1] : "init";
                }
            }
        }

        public static void Mark(string phase)
        {
            long elapsed = ElapsedMs();
            long privateBytes = PrivateBytes();
            long managedBytes = GC.GetTotalMemory(false);
            lock (syncRoot)
            {
                if (completed || phaseCount >= MaxPhases) return;
                phaseNames[phaseCount] = string.IsNullOrWhiteSpace(phase) ? "?" : phase;
                phaseElapsedMs[phaseCount] = elapsed;
                phasePrivateBytes[phaseCount] = privateBytes;
                phaseManagedBytes[phaseCount] = managedBytes;
                phaseCount++;
            }
        }

        public static void Complete()
        {
            lock (syncRoot)
            {
                completed = true;
            }
        }

        public static string Describe()
        {
            var inv = CultureInfo.InvariantCulture;
            var sb = new StringBuilder(2048);
            sb.AppendFormat(inv, "Startup diagnostics: {0}-bit process, culture {1}/{2}",
                Environment.Is64BitProcess ? 64 : 32,
                SafeCultureName(true), SafeCultureName(false));
            sb.AppendLine();
            lock (syncRoot)
            {
                for (int i = 0; i < phaseCount; i++)
                {
                    sb.AppendFormat(inv, "  phase {0} at {1} ms: private {2} MB, managed {3} MB",
                        phaseNames[i], phaseElapsedMs[i],
                        phasePrivateBytes[i] / Megabyte, phaseManagedBytes[i] / Megabyte);
                    sb.AppendLine();
                }
            }
            sb.AppendFormat(inv, "  failure at {0} ms during {1}: private {2} MB, managed {3} MB, GC gen0/1/2 {4}/{5}/{6}",
                ElapsedMs(), CurrentPhase, PrivateBytes() / Megabyte, GC.GetTotalMemory(false) / Megabyte,
                GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2));
            sb.AppendLine();

            long totalVirtual;
            long availableVirtual;
            if (TryReadAddressSpace(out totalVirtual, out availableVirtual))
            {
                sb.AppendFormat(inv, "  address space: {0} MB free of {1} MB, largest free block {2} MB",
                    availableVirtual / Megabyte, totalVirtual / Megabyte, LargestFreeBlock() / Megabyte);
                sb.AppendLine();
            }
            return sb.ToString();
        }

        internal static void ResetForTests()
        {
            lock (syncRoot)
            {
                phaseCount = 0;
                completed = false;
            }
        }

        private static long ElapsedMs()
        {
            if (processStartUtc == DateTime.MinValue) return -1;
            return (long)(DateTime.UtcNow - processStartUtc).TotalMilliseconds;
        }

        private static DateTime ReadProcessStartUtc()
        {
            try
            {
                using (var process = Process.GetCurrentProcess())
                {
                    return process.StartTime.ToUniversalTime();
                }
            }
            catch
            {
                return DateTime.MinValue;
            }
        }

        private static string SafeCultureName(bool ui)
        {
            try
            {
                var culture = ui ? CultureInfo.CurrentUICulture : CultureInfo.CurrentCulture;
                return string.IsNullOrEmpty(culture.Name) ? "invariant" : culture.Name;
            }
            catch
            {
                return "?";
            }
        }

        internal static long PrivateBytes()
        {
            try
            {
                NativeMethods.ProcessMemoryCountersEx counters;
                uint size = (uint)Marshal.SizeOf(typeof(NativeMethods.ProcessMemoryCountersEx));
                if (NativeMethods.GetProcessMemoryInfo(NativeMethods.GetCurrentProcess(), out counters, size))
                    return (long)counters.PrivateUsage.ToUInt64();
            }
            catch
            {
            }
            return -1;
        }

        private static bool TryReadAddressSpace(out long totalVirtual, out long availableVirtual)
        {
            totalVirtual = 0;
            availableVirtual = 0;
            try
            {
                var status = new NativeMethods.MemoryStatusEx();
                status.dwLength = (uint)Marshal.SizeOf(typeof(NativeMethods.MemoryStatusEx));
                if (!NativeMethods.GlobalMemoryStatusEx(ref status)) return false;
                totalVirtual = (long)Math.Min(status.ullTotalVirtual, (ulong)long.MaxValue);
                availableVirtual = (long)Math.Min(status.ullAvailVirtual, (ulong)long.MaxValue);
                return true;
            }
            catch
            {
                return false;
            }
        }

        internal static long LargestFreeBlock()
        {
            long largest = 0;
            try
            {
                uint infoSize = (uint)Marshal.SizeOf(typeof(NativeMethods.MemoryBasicInformation));
                ulong address = 0;
                for (int i = 0; i < MaxRegions; i++)
                {
                    NativeMethods.MemoryBasicInformation info;
                    if (NativeMethods.VirtualQuery(ToPointer(address), out info, new UIntPtr(infoSize)) == UIntPtr.Zero)
                        break;
                    ulong regionSize = info.RegionSize.ToUInt64();
                    if (info.State == NativeMethods.MEM_FREE && regionSize > (ulong)largest)
                        largest = (long)Math.Min(regionSize, (ulong)long.MaxValue);
                    ulong next = ToAddress(info.BaseAddress) + regionSize;
                    if (regionSize == 0 || next <= address) break;
                    if (!Environment.Is64BitProcess && next > uint.MaxValue) break;
                    address = next;
                }
            }
            catch
            {
            }
            return largest;
        }

        private static IntPtr ToPointer(ulong address)
        {
            return Environment.Is64BitProcess
                ? new IntPtr(unchecked((long)address))
                : new IntPtr(unchecked((int)(uint)address));
        }

        private static ulong ToAddress(IntPtr pointer)
        {
            return Environment.Is64BitProcess
                ? unchecked((ulong)pointer.ToInt64())
                : unchecked((uint)pointer.ToInt32());
        }
    }
}
