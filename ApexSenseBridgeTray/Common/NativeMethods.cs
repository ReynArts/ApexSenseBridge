using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ApexSenseBridgeTray.Common
{
    internal static class NativeMethods
    {
        public const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern int GetWindowTextLength(IntPtr hWnd);

        [DllImport("dwmapi.dll")]
        public static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

        public const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        public const int DWMWCP_ROUND = 2;

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint processAccess, bool bInheritHandle, uint processId);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern bool QueryFullProcessImageName(IntPtr hProcess, uint flags, StringBuilder lpExeName, ref uint lpdwSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("psapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumProcesses(
            [Out] uint[] processIds,
            uint bufferSize,
            out uint bytesReturned);

        public static uint[] GetProcessIds()
        {
            var capacity = 512;
            while (capacity <= 32768)
            {
                var processIds = new uint[capacity];
                uint bytesReturned;
                if (!EnumProcesses(
                    processIds,
                    (uint)(processIds.Length * sizeof(uint)),
                    out bytesReturned))
                {
                    return new uint[0];
                }

                int count = (int)(bytesReturned / sizeof(uint));
                if (count < processIds.Length)
                {
                    var result = new uint[count];
                    Array.Copy(processIds, result, count);
                    return result;
                }
                capacity *= 2;
            }
            return new uint[0];
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ProcessMemoryCountersEx
        {
            public uint cb;
            public uint PageFaultCount;
            public UIntPtr PeakWorkingSetSize;
            public UIntPtr WorkingSetSize;
            public UIntPtr QuotaPeakPagedPoolUsage;
            public UIntPtr QuotaPagedPoolUsage;
            public UIntPtr QuotaPeakNonPagedPoolUsage;
            public UIntPtr QuotaNonPagedPoolUsage;
            public UIntPtr PagefileUsage;
            public UIntPtr PeakPagefileUsage;
            public UIntPtr PrivateUsage;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct MemoryStatusEx
        {
            public uint dwLength;
            public uint dwMemoryLoad;
            public ulong ullTotalPhys;
            public ulong ullAvailPhys;
            public ulong ullTotalPageFile;
            public ulong ullAvailPageFile;
            public ulong ullTotalVirtual;
            public ulong ullAvailVirtual;
            public ulong ullAvailExtendedVirtual;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct MemoryBasicInformation
        {
            public IntPtr BaseAddress;
            public IntPtr AllocationBase;
            public uint AllocationProtect;
            public UIntPtr RegionSize;
            public uint State;
            public uint Protect;
            public uint Type;
        }

        internal const uint MEM_FREE = 0x10000;

        [DllImport("kernel32.dll")]
        internal static extern IntPtr GetCurrentProcess();

        [DllImport("psapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetProcessMemoryInfo(
            IntPtr process, out ProcessMemoryCountersEx counters, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx buffer);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern UIntPtr VirtualQuery(
            IntPtr address, out MemoryBasicInformation buffer, UIntPtr length);

        public static string GetActiveProcessPath(IntPtr hwnd, out uint processId)
        {
            processId = 0;
            if (hwnd == IntPtr.Zero) return null;

            GetWindowThreadProcessId(hwnd, out processId);
            if (processId == 0) return null;

            return GetProcessPath(processId);
        }

        public static string GetProcessPath(uint processId)
        {
            if (processId == 0) return null;

            var hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
            if (hProcess == IntPtr.Zero) return null;

            try
            {
                var sb = new StringBuilder(32768);
                uint size = (uint)sb.Capacity;
                if (QueryFullProcessImageName(hProcess, 0, sb, ref size))
                {
                    return sb.ToString();
                }
            }
            finally
            {
                CloseHandle(hProcess);
            }
            return null;
        }

        public static string GetActiveWindowTitle(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero) return string.Empty;
            int length = GetWindowTextLength(hwnd);
            if (length <= 0) return string.Empty;

            var sb = new StringBuilder(length + 1);
            GetWindowText(hwnd, sb, sb.Capacity);
            return sb.ToString();
        }
    }
}
