[CmdletBinding()]
param(
    [string]$Source,
    [string]$Output,
    [string]$PlateOutput,
    [switch]$NoPlate,
    [int]$NeutralColors = 30,
    [int]$ChromaColors = 40,
    [double]$Tolerance = 0.5,
    [int]$Oversample = 3,
    [double]$PlateFill = 0.8,
    [string]$DebugDirectory
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$assets = Join-Path $repoRoot 'assets'

function Resolve-Target([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) { return [System.IO.Path]::GetFullPath($path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).ProviderPath $path))
}

if (-not $Source) {
    $Source = Join-Path $assets 'logo-transparent.png'
    if (-not (Test-Path -LiteralPath $Source)) {
        $legacy = Join-Path $assets 'widget-logo.png'
        if (Test-Path -LiteralPath $legacy) { $Source = $legacy }
    }
}
if (-not $Output) { $Output = Join-Path $assets 'logo.svg' }
if (-not $NoPlate -and -not $PlateOutput) { $PlateOutput = Join-Path $assets 'logo-plate.svg' }

$Source = Resolve-Target $Source
$Output = Resolve-Target $Output
if (-not (Test-Path -LiteralPath $Source)) { throw "Source image not found: $Source" }
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'The dotnet SDK (10 or newer) is required to run the tracer.' }

$tracerArgs = @(
    '--source', $Source,
    '--output', $Output,
    '--ka', $NeutralColors,
    '--kb', $ChromaColors,
    '--tol', $Tolerance.ToString([System.Globalization.CultureInfo]::InvariantCulture),
    '--scale', $Oversample,
    '--plate-fill', $PlateFill.ToString([System.Globalization.CultureInfo]::InvariantCulture)
)
if (-not $NoPlate) { $tracerArgs += @('--plate', (Resolve-Target $PlateOutput)) }
if ($DebugDirectory) { $tracerArgs += @('--debug', (Resolve-Target $DebugDirectory)) }
if ($VerbosePreference -eq 'Continue') { $tracerArgs += '--verbose' }

$tracerSource = @'
#:property Nullable=disable
#:property InvariantGlobalization=true
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

struct V
{
    public double X, Y;
    public V(double x, double y) { X = x; Y = y; }
    public static V operator +(V a, V b) => new V(a.X + b.X, a.Y + b.Y);
    public static V operator -(V a, V b) => new V(a.X - b.X, a.Y - b.Y);
    public static V operator *(V a, double s) => new V(a.X * s, a.Y * s);
    public double Dot(V b) => X * b.X + Y * b.Y;
    public double Len => Math.Sqrt(X * X + Y * Y);
    public V Norm() { double l = Len; return l < 1e-12 ? new V(0, 0) : new V(X / l, Y / l); }
}

sealed class Img
{
    public int W, H;
    public byte[] Rgba;
}

static class PngIO
{
    static int BE(byte[] b, int p) => (b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3];

    public static Img Read(string path)
    {
        byte[] b = File.ReadAllBytes(path);
        int pos = 8, w = 0, h = 0, bd = 0, ct = 0, il = 0;
        var idat = new MemoryStream();
        while (pos + 8 <= b.Length)
        {
            int len = BE(b, pos);
            string type = Encoding.ASCII.GetString(b, pos + 4, 4);
            if (type == "IHDR") { w = BE(b, pos + 8); h = BE(b, pos + 12); bd = b[pos + 16]; ct = b[pos + 17]; il = b[pos + 20]; }
            else if (type == "IDAT") idat.Write(b, pos + 8, len);
            else if (type == "IEND") break;
            pos += 12 + len;
        }
        if (bd != 8 || il != 0 || (ct != 2 && ct != 6 && ct != 0 && ct != 4))
            throw new NotSupportedException("Only non-interlaced 8-bit grey/RGB/RGBA PNG is supported");
        int ch = ct == 6 ? 4 : ct == 2 ? 3 : ct == 4 ? 2 : 1;
        int stride = w * ch;
        byte[] raw = new byte[(stride + 1) * h];
        idat.Position = 0;
        using (var z = new ZLibStream(idat, CompressionMode.Decompress))
        {
            int got = 0;
            while (got < raw.Length)
            {
                int r = z.Read(raw, got, raw.Length - got);
                if (r <= 0) break;
                got += r;
            }
        }
        byte[] pix = new byte[stride * h];
        for (int y = 0; y < h; y++)
        {
            int f = raw[y * (stride + 1)];
            int ro = y * (stride + 1) + 1;
            int po = y * stride;
            for (int x = 0; x < stride; x++)
            {
                int cur = raw[ro + x];
                int a = x >= ch ? pix[po + x - ch] : 0;
                int up = y > 0 ? pix[po - stride + x] : 0;
                int ul = (y > 0 && x >= ch) ? pix[po - stride + x - ch] : 0;
                int v;
                switch (f)
                {
                    case 0: v = cur; break;
                    case 1: v = cur + a; break;
                    case 2: v = cur + up; break;
                    case 3: v = cur + ((a + up) >> 1); break;
                    default:
                        int p = a + up - ul;
                        int pa = Math.Abs(p - a), pb = Math.Abs(p - up), pc = Math.Abs(p - ul);
                        int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? up : ul);
                        v = cur + pr; break;
                }
                pix[po + x] = (byte)v;
            }
        }
        var img = new Img { W = w, H = h, Rgba = new byte[w * h * 4] };
        for (int i = 0; i < w * h; i++)
        {
            byte r, g, bl, a = 255;
            if (ch == 4) { r = pix[i * 4]; g = pix[i * 4 + 1]; bl = pix[i * 4 + 2]; a = pix[i * 4 + 3]; }
            else if (ch == 3) { r = pix[i * 3]; g = pix[i * 3 + 1]; bl = pix[i * 3 + 2]; }
            else if (ch == 2) { r = g = bl = pix[i * 2]; a = pix[i * 2 + 1]; }
            else { r = g = bl = pix[i]; }
            img.Rgba[i * 4] = r; img.Rgba[i * 4 + 1] = g; img.Rgba[i * 4 + 2] = bl; img.Rgba[i * 4 + 3] = a;
        }
        return img;
    }

    static uint[] crcTable;
    static uint Crc(byte[] d, int off, int len)
    {
        if (crcTable == null)
        {
            crcTable = new uint[256];
            for (uint n = 0; n < 256; n++)
            {
                uint c = n;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                crcTable[n] = c;
            }
        }
        uint r = 0xFFFFFFFFu;
        for (int i = 0; i < len; i++) r = crcTable[(r ^ d[off + i]) & 0xFF] ^ (r >> 8);
        return r ^ 0xFFFFFFFFu;
    }

    static void Chunk(Stream s, string type, byte[] data)
    {
        byte[] buf = new byte[data.Length + 4];
        Encoding.ASCII.GetBytes(type).CopyTo(buf, 0);
        data.CopyTo(buf, 4);
        byte[] len = { (byte)(data.Length >> 24), (byte)(data.Length >> 16), (byte)(data.Length >> 8), (byte)data.Length };
        s.Write(len, 0, 4);
        s.Write(buf, 0, buf.Length);
        uint c = Crc(buf, 0, buf.Length);
        byte[] cb = { (byte)(c >> 24), (byte)(c >> 16), (byte)(c >> 8), (byte)c };
        s.Write(cb, 0, 4);
    }

    public static void Write(string path, int w, int h, byte[] rgba)
    {
        using var fs = File.Create(path);
        fs.Write(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }, 0, 8);
        byte[] ihdr = new byte[13];
        ihdr[0] = (byte)(w >> 24); ihdr[1] = (byte)(w >> 16); ihdr[2] = (byte)(w >> 8); ihdr[3] = (byte)w;
        ihdr[4] = (byte)(h >> 24); ihdr[5] = (byte)(h >> 16); ihdr[6] = (byte)(h >> 8); ihdr[7] = (byte)h;
        ihdr[8] = 8; ihdr[9] = 6;
        Chunk(fs, "IHDR", ihdr);
        var ms = new MemoryStream();
        using (var z = new ZLibStream(ms, CompressionLevel.Fastest, true))
        {
            byte[] row = new byte[w * 4 + 1];
            for (int y = 0; y < h; y++)
            {
                row[0] = 0;
                Buffer.BlockCopy(rgba, y * w * 4, row, 1, w * 4);
                z.Write(row, 0, row.Length);
            }
        }
        Chunk(fs, "IDAT", ms.ToArray());
        Chunk(fs, "IEND", new byte[0]);
    }
}

static class Color
{
    static readonly float[] lin = BuildLin();
    static float[] BuildLin()
    {
        var t = new float[256];
        for (int i = 0; i < 256; i++)
        {
            double c = i / 255.0;
            t[i] = (float)(c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4));
        }
        return t;
    }
    static double F(double t) => t > 0.008856 ? Math.Cbrt(t) : 7.787 * t + 16.0 / 116.0;
    public static void ToLab(byte r, byte g, byte b, out float L, out float A, out float B)
    {
        double lr = lin[r], lg = lin[g], lb = lin[b];
        double x = (0.4124564 * lr + 0.3575761 * lg + 0.1804375 * lb) / 0.95047;
        double y = 0.2126729 * lr + 0.7151522 * lg + 0.0721750 * lb;
        double z = (0.0193339 * lr + 0.1191920 * lg + 0.9503041 * lb) / 1.08883;
        double fx = F(x), fy = F(y), fz = F(z);
        L = (float)(116 * fy - 16); A = (float)(500 * (fx - fy)); B = (float)(200 * (fy - fz));
    }
    static double Finv(double t) { double t3 = t * t * t; return t3 > 0.008856 ? t3 : (t - 16.0 / 116.0) / 7.787; }
    static double Gam(double v) { v = Math.Clamp(v, 0, 1); return 255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.Pow(v, 1 / 2.4) - 0.055); }
    public static void FromLab(double L, double a, double b, out double r, out double g, out double bl)
    {
        double fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200;
        double x = 0.95047 * Finv(fx), y = Finv(fy), z = 1.08883 * Finv(fz);
        r = Gam(3.2404542 * x - 1.5371385 * y - 0.4985314 * z);
        g = Gam(-0.9692660 * x + 1.8760108 * y + 0.0415560 * z);
        bl = Gam(0.0556434 * x - 0.2040259 * y + 1.0572252 * z);
    }
    public static string Hex(double r, double g, double b)
    {
        int R = (int)Math.Clamp(Math.Round(r), 0, 255), G = (int)Math.Clamp(Math.Round(g), 0, 255), B = (int)Math.Clamp(Math.Round(b), 0, 255);
        return "#" + R.ToString("x2") + G.ToString("x2") + B.ToString("x2");
    }
}

static class Bezier
{
    static V Q(V[] c, double t)
    {
        double m = 1 - t;
        return c[0] * (m * m * m) + c[1] * (3 * m * m * t) + c[2] * (3 * m * t * t) + c[3] * (t * t * t);
    }
    static V Q1(V[] c, double t)
    {
        double m = 1 - t;
        return (c[1] - c[0]) * (3 * m * m) + (c[2] - c[1]) * (6 * m * t) + (c[3] - c[2]) * (3 * t * t);
    }
    static V Q2(V[] c, double t)
    {
        double m = 1 - t;
        return (c[2] - c[1] * 2 + c[0]) * (6 * m) + (c[3] - c[2] * 2 + c[1]) * (6 * t);
    }

    static double[] Chord(V[] d, int f, int l)
    {
        var u = new double[l - f + 1];
        for (int i = f + 1; i <= l; i++) u[i - f] = u[i - f - 1] + (d[i] - d[i - 1]).Len;
        double tot = u[l - f];
        if (tot < 1e-12) { for (int i = 1; i < u.Length; i++) u[i] = (double)i / (u.Length - 1); }
        else for (int i = 1; i < u.Length; i++) u[i] /= tot;
        return u;
    }

    static V[] Generate(V[] d, int f, int l, double[] u, V t1, V t2)
    {
        int n = l - f + 1;
        double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
        V p0 = d[f], p3 = d[l];
        for (int i = 0; i < n; i++)
        {
            double t = u[i], m = 1 - t;
            double b0 = m * m * m, b1 = 3 * m * m * t, b2 = 3 * m * t * t, b3 = t * t * t;
            V a1 = t1 * b1, a2 = t2 * b2;
            c00 += a1.Dot(a1); c01 += a1.Dot(a2); c11 += a2.Dot(a2);
            V tmp = d[f + i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
            x0 += a1.Dot(tmp); x1 += a2.Dot(tmp);
        }
        double det = c00 * c11 - c01 * c01;
        double al = 0, ar = 0;
        if (Math.Abs(det) > 1e-12)
        {
            al = (x0 * c11 - x1 * c01) / det;
            ar = (c00 * x1 - c01 * x0) / det;
        }
        double seg = (p3 - p0).Len;
        double eps = 1e-6 * seg;
        if (al < eps || ar < eps || Math.Abs(det) <= 1e-12)
        {
            al = ar = seg / 3.0;
        }
        return new[] { p0, p0 + t1 * al, p3 + t2 * ar, p3 };
    }

    static double MaxErr(V[] d, int f, int l, V[] bz, double[] u, out int split)
    {
        split = (l - f + 1) / 2 + f;
        double mx = 0;
        for (int i = f + 1; i < l; i++)
        {
            V dv = Q(bz, u[i - f]) - d[i];
            double dist = dv.Dot(dv);
            if (dist >= mx) { mx = dist; split = i; }
        }
        return mx;
    }

    static double[] Reparam(V[] d, int f, int l, double[] u, V[] bz)
    {
        var r = new double[u.Length];
        for (int i = 0; i < u.Length; i++)
        {
            double t = u[i];
            V p = Q(bz, t) - d[f + i];
            V d1 = Q1(bz, t);
            V d2 = Q2(bz, t);
            double num = p.Dot(d1);
            double den = d1.Dot(d1) + p.Dot(d2);
            double nt = Math.Abs(den) < 1e-12 ? t : t - num / den;
            r[i] = Math.Clamp(nt, 0, 1);
        }
        for (int i = 1; i < r.Length; i++) if (r[i] < r[i - 1]) r[i] = r[i - 1];
        return r;
    }

    public static void Fit(V[] d, int f, int l, V t1, V t2, double err2, List<V[]> output)
    {
        int n = l - f + 1;
        if (n == 2)
        {
            double dist = (d[l] - d[f]).Len / 3.0;
            output.Add(new[] { d[f], d[f] + t1 * dist, d[l] + t2 * dist, d[l] });
            return;
        }
        double[] u = Chord(d, f, l);
        V[] bz = Generate(d, f, l, u, t1, t2);
        int split;
        double mx = MaxErr(d, f, l, bz, u, out split);
        if (mx < err2) { output.Add(bz); return; }
        if (mx < err2 * 16)
        {
            for (int it = 0; it < 12; it++)
            {
                var u2 = Reparam(d, f, l, u, bz);
                bz = Generate(d, f, l, u2, t1, t2);
                mx = MaxErr(d, f, l, bz, u2, out split);
                if (mx < err2) { output.Add(bz); return; }
                u = u2;
            }
        }
        if (split <= f) split = f + 1;
        if (split >= l) split = l - 1;
        V tc = (d[split - 1] - d[split + 1]).Norm();
        if (tc.Len < 0.5) tc = (d[split] - d[split - 1]).Norm();
        Fit(d, f, split, t1, tc, err2, output);
        Fit(d, split, l, tc * -1, t2, err2, output);
    }

    public static List<V[]> FitLoop(List<V> loop, double tol, double cornerDeg, double win)
    {
        var p = new List<V>();
        foreach (var q in loop)
            if (p.Count == 0 || (q - p[p.Count - 1]).Len > 1e-4) p.Add(q);
        while (p.Count > 1 && (p[0] - p[p.Count - 1]).Len < 1e-4) p.RemoveAt(p.Count - 1);
        int n = p.Count;
        var res = new List<V[]>();
        if (n < 3) return res;
        double[] s = new double[n + 1];
        for (int i = 0; i < n; i++) s[i + 1] = s[i] + (p[(i + 1) % n] - p[i]).Len;
        double total = s[n];
        int IdxAt(int i, double ds)
        {
            double target = s[i] + ds;
            target = ((target % total) + total) % total;
            int lo = 0, hi = n;
            while (hi - lo > 1) { int mid = (lo + hi) / 2; if (s[mid] <= target) lo = mid; else hi = mid; }
            return lo % n;
        }
        double[] ang = new double[n];
        for (int i = 0; i < n; i++)
        {
            int a = IdxAt(i, -win), b = IdxAt(i, win);
            V v1 = (p[i] - p[a]), v2 = (p[b] - p[i]);
            if (v1.Len < 1e-9 || v2.Len < 1e-9) continue;
            double c = Math.Clamp(v1.Norm().Dot(v2.Norm()), -1, 1);
            ang[i] = Math.Acos(c) * 180 / Math.PI;
        }
        var corners = new List<int>();
        for (int i = 0; i < n; i++)
        {
            if (ang[i] < cornerDeg) continue;
            bool best = true;
            for (int j = 1; j <= n && best; j++)
            {
                int k = (i + j) % n;
                double dd = Math.Min(s[k] > s[i] ? s[k] - s[i] : s[k] - s[i] + total, 1e9);
                if (dd > win) break;
                if (ang[k] > ang[i] || (ang[k] == ang[i] && k < i)) best = false;
            }
            for (int j = 1; j <= n && best; j++)
            {
                int k = ((i - j) % n + n) % n;
                double dd = s[i] > s[k] ? s[i] - s[k] : s[i] - s[k] + total;
                if (dd > win) break;
                if (ang[k] > ang[i] || (ang[k] == ang[i] && k < i)) best = false;
            }
            if (best) corners.Add(i);
        }
        if (corners.Count == 0) { corners.Add(0); corners.Add(IdxAt(0, total / 2)); }
        else if (corners.Count == 1) corners.Add(IdxAt(corners[0], total / 2));
        corners.Sort();
        int m = corners.Count;
        double tolSq = tol * tol;
        for (int ci = 0; ci < m; ci++)
        {
            int a = corners[ci], b = corners[(ci + 1) % m];
            int cnt = ((b - a) % n + n) % n;
            if (cnt == 0) cnt = n;
            var d = new V[cnt + 1];
            for (int i = 0; i <= cnt; i++) d[i] = p[(a + i) % n];
            if (cnt < 1) continue;
            V t1, t2;
            int look = Math.Min(3, cnt);
            bool realCornerA = ang[a] >= cornerDeg, realCornerB = ang[b] >= cornerDeg;
            if (realCornerA) t1 = (d[look] - d[0]).Norm();
            else t1 = (p[(a + 1) % n] - p[((a - 1) % n + n) % n]).Norm();
            if (realCornerB) t2 = (d[cnt - look] - d[cnt]).Norm();
            else t2 = (p[((b - 1) % n + n) % n] - p[(b + 1) % n]).Norm();
            Fit(d, 0, cnt, t1, t2, tolSq, res);
        }
        return res;
    }
}

static class Raster
{
    public static float[] Gauss(float[] src, int w, int h, double sigma)
    {
        int r = (int)Math.Ceiling(sigma * 3);
        float[] k = new float[2 * r + 1];
        double sum = 0;
        for (int i = -r; i <= r; i++) { k[i + r] = (float)Math.Exp(-i * i / (2 * sigma * sigma)); sum += k[i + r]; }
        for (int i = 0; i < k.Length; i++) k[i] /= (float)sum;
        float[] tmp = new float[w * h];
        Parallel.For(0, h, y =>
        {
            for (int x = 0; x < w; x++)
            {
                float acc = 0;
                for (int i = -r; i <= r; i++)
                {
                    int xx = x + i;
                    if (xx < 0 || xx >= w) continue;
                    acc += src[y * w + xx] * k[i + r];
                }
                tmp[y * w + x] = acc;
            }
        });
        float[] dst = new float[w * h];
        Parallel.For(0, h, y =>
        {
            for (int x = 0; x < w; x++)
            {
                float acc = 0;
                for (int i = -r; i <= r; i++)
                {
                    int yy = y + i;
                    if (yy < 0 || yy >= h) continue;
                    acc += tmp[yy * w + x] * k[i + r];
                }
                dst[y * w + x] = acc;
            }
        });
        return dst;
    }

    public static List<List<V>> Contours(float[] f, int w, int h, float t)
    {
        int[] n1 = new int[w * h * 2];
        int[] n2 = new int[w * h * 2];
        Array.Fill(n1, -1);
        Array.Fill(n2, -1);
        var used = new List<int>();
        void Link(int a, int b)
        {
            if (n1[a] < 0) { n1[a] = b; used.Add(a); } else n2[a] = b;
            if (n1[b] < 0) { n1[b] = a; used.Add(b); } else n2[b] = a;
        }
        for (int y = 0; y < h - 1; y++)
            for (int x = 0; x < w - 1; x++)
            {
                float v00 = f[y * w + x], v10 = f[y * w + x + 1], v11 = f[(y + 1) * w + x + 1], v01 = f[(y + 1) * w + x];
                int c = (v00 >= t ? 1 : 0) | (v10 >= t ? 2 : 0) | (v11 >= t ? 4 : 0) | (v01 >= t ? 8 : 0);
                if (c == 0 || c == 15) continue;
                int T = (y * w + x) * 2, Bm = ((y + 1) * w + x) * 2, Lf = (y * w + x) * 2 + 1, R = (y * w + x + 1) * 2 + 1;
                switch (c)
                {
                    case 1: case 14: Link(Lf, T); break;
                    case 2: case 13: Link(T, R); break;
                    case 3: case 12: Link(Lf, R); break;
                    case 4: case 11: Link(R, Bm); break;
                    case 6: case 9: Link(T, Bm); break;
                    case 7: case 8: Link(Lf, Bm); break;
                    case 5:
                        if ((v00 + v10 + v11 + v01) * 0.25f >= t) { Link(T, R); Link(Lf, Bm); }
                        else { Link(Lf, T); Link(R, Bm); }
                        break;
                    case 10:
                        if ((v00 + v10 + v11 + v01) * 0.25f >= t) { Link(Lf, T); Link(R, Bm); }
                        else { Link(T, R); Link(Lf, Bm); }
                        break;
                }
            }
        V Pt(int key)
        {
            int cell = key >> 1;
            int y = cell / w, x = cell % w;
            if ((key & 1) == 0)
            {
                float a = f[y * w + x], b = f[y * w + x + 1];
                double tt = (t - a) / (b - a);
                return new V(x + tt, y);
            }
            else
            {
                float a = f[y * w + x], b = f[(y + 1) * w + x];
                double tt = (t - a) / (b - a);
                return new V(x, y + tt);
            }
        }
        var loops = new List<List<V>>();
        var visited = new HashSet<int>();
        foreach (int start in used)
        {
            if (visited.Contains(start)) continue;
            var loop = new List<V>();
            int prev = -1, cur = start;
            while (true)
            {
                visited.Add(cur);
                loop.Add(Pt(cur));
                int next = n1[cur] == prev ? n2[cur] : n1[cur];
                if (next < 0) break;
                prev = cur; cur = next;
                if (cur == start) break;
            }
            if (loop.Count >= 4) loops.Add(loop);
        }
        return loops;
    }
}

static class Program
{
    static int W, H, N;
    static float[] L, A, B;
    static byte[] rgba;
    static bool[] op;

    static void Bilateral(int r, double ss, double sr, int iters)
    {
        int D = 2 * r + 1;
        float[] sw = new float[D * D];
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                sw[(dy + r) * D + dx + r] = (float)Math.Exp(-(dx * dx + dy * dy) / (2 * ss * ss));
        float inv = (float)(-1.0 / (2 * sr * sr));
        for (int it = 0; it < iters; it++)
        {
            float[] nL = new float[N], nA = new float[N], nB = new float[N];
            float[] sL = L, sA = A, sB = B;
            Parallel.For(0, H, y =>
            {
                for (int x = 0; x < W; x++)
                {
                    int i = y * W + x;
                    if (!op[i]) continue;
                    float cl = sL[i], ca = sA[i], cb = sB[i];
                    float ws = 0, l = 0, a = 0, b = 0;
                    for (int dy = -r; dy <= r; dy++)
                    {
                        int yy = y + dy;
                        if (yy < 0 || yy >= H) continue;
                        for (int dx = -r; dx <= r; dx++)
                        {
                            int xx = x + dx;
                            if (xx < 0 || xx >= W) continue;
                            int j = yy * W + xx;
                            if (!op[j]) continue;
                            float dl = sL[j] - cl, da = sA[j] - ca, db = sB[j] - cb;
                            float w = sw[(dy + r) * D + dx + r] * MathF.Exp((dl * dl + da * da + db * db) * inv);
                            ws += w; l += w * sL[j]; a += w * sA[j]; b += w * sB[j];
                        }
                    }
                    nL[i] = l / ws; nA[i] = a / ws; nB[i] = b / ws;
                }
            });
            L = nL; A = nA; B = nB;
        }
    }

    static float[] KMeans(float[] pts, int n, int k, int iters, int seed)
    {
        var rnd = new Random(seed);
        float[] cen = new float[k * 3];
        double[] dmin = new double[n];
        int first = rnd.Next(n);
        Array.Copy(pts, first * 3, cen, 0, 3);
        for (int i = 0; i < n; i++) dmin[i] = D2(pts, i, cen, 0);
        for (int c = 1; c < k; c++)
        {
            double tot = 0;
            for (int i = 0; i < n; i++) tot += dmin[i];
            double r = rnd.NextDouble() * tot, acc = 0;
            int pick = n - 1;
            for (int i = 0; i < n; i++) { acc += dmin[i]; if (acc >= r) { pick = i; break; } }
            Array.Copy(pts, pick * 3, cen, c * 3, 3);
            for (int i = 0; i < n; i++)
            {
                double d = D2(pts, i, cen, c);
                if (d < dmin[i]) dmin[i] = d;
            }
        }
        int[] asg = new int[n];
        for (int it = 0; it < iters; it++)
        {
            Parallel.For(0, n, i =>
            {
                double best = double.MaxValue; int bc = 0;
                for (int c = 0; c < k; c++)
                {
                    double d = D2(pts, i, cen, c);
                    if (d < best) { best = d; bc = c; }
                }
                asg[i] = bc;
            });
            double[] sum = new double[k * 3];
            int[] cnt = new int[k];
            for (int i = 0; i < n; i++)
            {
                int c = asg[i];
                sum[c * 3] += pts[i * 3]; sum[c * 3 + 1] += pts[i * 3 + 1]; sum[c * 3 + 2] += pts[i * 3 + 2];
                cnt[c]++;
            }
            for (int c = 0; c < k; c++)
                if (cnt[c] > 0)
                {
                    cen[c * 3] = (float)(sum[c * 3] / cnt[c]);
                    cen[c * 3 + 1] = (float)(sum[c * 3 + 1] / cnt[c]);
                    cen[c * 3 + 2] = (float)(sum[c * 3 + 2] / cnt[c]);
                }
        }
        return cen;
    }

    static double D2(float[] pts, int i, float[] cen, int c)
    {
        double a = pts[i * 3] - cen[c * 3], b = pts[i * 3 + 1] - cen[c * 3 + 1], d = pts[i * 3 + 2] - cen[c * 3 + 2];
        return a * a + b * b + d * d;
    }

    static float PixelDist(int i, float[] cen, int c)
    {
        float a = L[i] - cen[c * 3], b = A[i] - cen[c * 3 + 1], d = B[i] - cen[c * 3 + 2];
        return a * a + b * b + d * d;
    }

    static readonly int[] DX8 = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static readonly int[] DY8 = { -1, -1, -1, 0, 0, 1, 1, 1 };

    static void Reassign(int[] lab, float[] cen)
    {
        while (true)
        {
            var pending = new List<int>();
            for (int i = 0; i < N; i++) if (lab[i] == -1 && op[i]) pending.Add(i);
            if (pending.Count == 0) break;
            var newLab = new int[pending.Count];
            int progress = 0;
            Parallel.For(0, pending.Count, pi =>
            {
                int i = pending[pi];
                int x = i % W, y = i / W;
                float best = float.MaxValue; int bl = -1;
                for (int dy = -2; dy <= 2; dy++)
                {
                    int yy = y + dy;
                    if (yy < 0 || yy >= H) continue;
                    for (int dx = -2; dx <= 2; dx++)
                    {
                        int xx = x + dx;
                        if (xx < 0 || xx >= W) continue;
                        int l = lab[yy * W + xx];
                        if (l < 0) continue;
                        float dist = PixelDist(i, cen, l);
                        if (dist < best) { best = dist; bl = l; }
                    }
                }
                newLab[pi] = bl;
            });
            for (int pi = 0; pi < pending.Count; pi++)
                if (newLab[pi] >= 0) { lab[pending[pi]] = newLab[pi]; progress++; }
            if (progress == 0)
            {
                foreach (int i in pending) lab[i] = 0;
                break;
            }
        }
    }

    static double MixThr = 6.0, PairThr = 5.0, ContactPct = 7;

    static int IsMix(int own, List<int> nbs, float[] cen)
    {
        double cl = cen[own * 3], ca = cen[own * 3 + 1], cb = cen[own * 3 + 2];
        int bestSingle = -1; double bestD = MixThr;
        for (int i = 0; i < nbs.Count; i++)
        {
            int a = nbs[i];
            double d = Math.Sqrt(Math.Pow(cl - cen[a * 3], 2) + Math.Pow(ca - cen[a * 3 + 1], 2) + Math.Pow(cb - cen[a * 3 + 2], 2));
            if (d < bestD) { bestD = d; bestSingle = a; }
        }
        if (bestSingle >= 0) return bestSingle;
        for (int i = 0; i < nbs.Count; i++)
        {
            int a = nbs[i];
            double al = cen[a * 3], aa = cen[a * 3 + 1], ab = cen[a * 3 + 2];
            for (int j = i + 1; j < nbs.Count; j++)
            {
                int b = nbs[j];
                double bl = cen[b * 3], ba = cen[b * 3 + 1], bb = cen[b * 3 + 2];
                double vl = bl - al, va = ba - aa, vb = bb - ab;
                double len2 = vl * vl + va * va + vb * vb;
                if (len2 < 625) continue;
                double t = ((cl - al) * vl + (ca - aa) * va + (cb - ab) * vb) / len2;
                if (t < 0.02 || t > 0.98) continue;
                double pl = al + t * vl, pa = aa + t * va, pb = ab + t * vb;
                double dd = Math.Sqrt((cl - pl) * (cl - pl) + (ca - pa) * (ca - pa) + (cb - pb) * (cb - pb));
                if (dd < PairThr) return -2;
            }
        }
        return -1;
    }

    static int RemoveThin(int[] lab, float[] cen)
    {
        bool[] solid = new bool[N];
        Parallel.For(0, H, y =>
        {
            for (int x = 0; x < W; x++)
            {
                int i = y * W + x;
                if (!op[i]) continue;
                int l = lab[i];
                bool ok = true;
                for (int d = 0; d < 8 && ok; d++)
                {
                    int xx = x + DX8[d], yy = y + DY8[d];
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    int j = yy * W + xx;
                    if (!op[j]) continue;
                    if (lab[j] != l) ok = false;
                }
                solid[i] = ok;
            }
        });
        bool[] thin = new bool[N];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                int i = y * W + x;
                if (!op[i]) continue;
                int l = lab[i];
                bool cov = solid[i];
                for (int d = 0; d < 8 && !cov; d++)
                {
                    int xx = x + DX8[d], yy = y + DY8[d];
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    int j = yy * W + xx;
                    if (solid[j] && lab[j] == l) cov = true;
                }
                thin[i] = !cov;
            }
        bool[] seen = new bool[N];
        int removed = 0;
        var stack = new Stack<int>();
        var comp = new List<int>();
        var nbs = new List<int>();
        var contact = new Dictionary<int, int>();
        for (int s = 0; s < N; s++)
        {
            if (!thin[s] || seen[s]) continue;
            int l = lab[s];
            comp.Clear(); nbs.Clear(); contact.Clear();
            stack.Push(s); seen[s] = true;
            while (stack.Count > 0)
            {
                int i = stack.Pop();
                comp.Add(i);
                int x = i % W, y = i / W;
                for (int d = 0; d < 8; d++)
                {
                    int xx = x + DX8[d], yy = y + DY8[d];
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    int j = yy * W + xx;
                    if (!op[j]) continue;
                    if (lab[j] == l)
                    {
                        if (thin[j] && !seen[j]) { seen[j] = true; stack.Push(j); }
                    }
                    else if (lab[j] >= 0) { contact.TryGetValue(lab[j], out int cc); contact[lab[j]] = cc + 1; }
                }
            }
            if (contact.Count > 0)
            {
                int totalContact = 0;
                foreach (var kv in contact) totalContact += kv.Value;
                nbs.Clear();
                foreach (var kv in contact) if (kv.Value * 100 >= totalContact * ContactPct) nbs.Add(kv.Key);
            }
            int mix = IsMix(l, nbs, cen);
            if (mix >= 0)
            {
                foreach (int i in comp) lab[i] = mix;
                removed += comp.Count;
            }
            else if (mix == -2)
            {
                foreach (int i in comp) lab[i] = -1;
                removed += comp.Count;
            }
        }
        Reassign(lab, cen);
        return removed;
    }

    static int LowArea = 90;
    static double LowDE = 14;

    static int RemoveSmall(int[] lab, float[] cen, int minArea)
    {
        bool[] seen = new bool[N];
        int removed = 0;
        var stack = new Stack<int>();
        var comp = new List<int>();
        var nbs = new List<int>();
        for (int s = 0; s < N; s++)
        {
            if (seen[s] || !op[s]) continue;
            int l = lab[s];
            comp.Clear(); nbs.Clear();
            stack.Push(s); seen[s] = true;
            while (stack.Count > 0)
            {
                int i = stack.Pop();
                comp.Add(i);
                int x = i % W, y = i / W;
                for (int d = 0; d < 4; d++)
                {
                    int xx = x + (d == 0 ? -1 : d == 1 ? 1 : 0), yy = y + (d == 2 ? -1 : d == 3 ? 1 : 0);
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    int j = yy * W + xx;
                    if (!op[j]) continue;
                    if (lab[j] == l) { if (!seen[j]) { seen[j] = true; stack.Push(j); } }
                    else if (lab[j] >= 0 && !nbs.Contains(lab[j])) nbs.Add(lab[j]);
                }
            }
            bool kill = comp.Count < minArea;
            if (!kill && comp.Count < LowArea && nbs.Count > 0 && cen[l * 3] > 55)
            {
                double mn = double.MaxValue;
                foreach (int nb in nbs)
                {
                    double dl = cen[l * 3] - cen[nb * 3], da = cen[l * 3 + 1] - cen[nb * 3 + 1], db = cen[l * 3 + 2] - cen[nb * 3 + 2];
                    mn = Math.Min(mn, Math.Sqrt(dl * dl + da * da + db * db));
                }
                if (mn < LowDE) kill = true;
            }
            if (kill)
            {
                foreach (int i in comp) lab[i] = -1;
                removed += comp.Count;
            }
        }
        Reassign(lab, cen);
        return removed;
    }

    static string Fmt(double v)
    {
        double r = Math.Round(v * 10) / 10.0;
        if (r == 0) r = 0;
        return r.ToString("0.#", CultureInfo.InvariantCulture);
    }

    static List<double[]> MergePalette(float[] cen, int k, float[] tp, int tn, double thr)
    {
        double[] cnt = new double[k];
        for (int i = 0; i < tn; i++)
        {
            double best = double.MaxValue; int bc = 0;
            for (int c = 0; c < k; c++) { double d = D2(tp, i, cen, c); if (d < best) { best = d; bc = c; } }
            cnt[bc]++;
        }
        var list = new List<double[]>();
        for (int c = 0; c < k; c++) if (cnt[c] > 0) list.Add(new double[] { cen[c * 3], cen[c * 3 + 1], cen[c * 3 + 2], cnt[c] });
        while (list.Count > 2)
        {
            double bd = double.MaxValue; int bi = -1, bj = -1;
            for (int i = 0; i < list.Count; i++)
                for (int j = i + 1; j < list.Count; j++)
                {
                    double d = Math.Sqrt(Math.Pow(list[i][0] - list[j][0], 2) + Math.Pow(list[i][1] - list[j][1], 2) + Math.Pow(list[i][2] - list[j][2], 2));
                    if (d < bd) { bd = d; bi = i; bj = j; }
                }
            if (bd >= thr) break;
            var a = list[bi]; var b = list[bj];
            double tw = a[3] + b[3];
            list[bi] = new double[] { (a[0] * a[3] + b[0] * b[3]) / tw, (a[1] * a[3] + b[1] * b[3]) / tw, (a[2] * a[3] + b[2] * b[3]) / tw, tw };
            list.RemoveAt(bj);
        }
        return list;
    }


    sealed class Grad
    {
        public double X1, Y1, X2, Y2;
        public List<KeyValuePair<double, string>> Stops = new List<KeyValuePair<double, string>>();
    }

    static Grad TryGradient(List<int> mem, double minStd, double minR2, string tag, Action<string> log)
    {
        int n = mem.Count;
        double mx = 0, my = 0;
        foreach (int i in mem) { mx += i % W + 0.5; my += i / W + 0.5; }
        mx /= n; my /= n;
        double sxx = 0, sxy = 0, syy = 0;
        foreach (int i in mem)
        {
            double dx = i % W + 0.5 - mx, dy = i / W + 0.5 - my;
            sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
        }
        double ang = 0.5 * Math.Atan2(2 * sxy, sxx - syy);
        double cx = Math.Cos(ang), cy = Math.Sin(ang);
        double[] ts = new double[n];
        for (int k = 0; k < n; k++)
        {
            int i = mem[k];
            ts[k] = (i % W + 0.5 - mx) * cx + (i / W + 0.5 - my) * cy;
        }
        double[] sorted = (double[])ts.Clone();
        Array.Sort(sorted);
        double tmin = sorted[(int)(n * 0.005)], tmax = sorted[Math.Min(n - 1, (int)(n * 0.995))];
        if (tmax - tmin < 30) return null;
        int nb = 14;
        double[,] bl = new double[nb, 3];
        double[,] bc = new double[nb, 3];
        int[] cnt = new int[nb];
        double ml = 0, ma = 0, mb = 0;
        foreach (int i in mem) { ml += L[i]; ma += A[i]; mb += B[i]; }
        ml /= n; ma /= n; mb /= n;
        double tot = 0;
        int[] binOf = new int[n];
        for (int k = 0; k < n; k++)
        {
            int i = mem[k];
            int b = (int)Math.Clamp(Math.Floor((ts[k] - tmin) / (tmax - tmin) * nb), 0, nb - 1);
            binOf[k] = b;
            cnt[b]++;
            bl[b, 0] += L[i]; bl[b, 1] += A[i]; bl[b, 2] += B[i];
            bc[b, 0] += rgba[i * 4]; bc[b, 1] += rgba[i * 4 + 1]; bc[b, 2] += rgba[i * 4 + 2];
            tot += (L[i] - ml) * (L[i] - ml) + (A[i] - ma) * (A[i] - ma) + (B[i] - mb) * (B[i] - mb);
        }
        double res = 0;
        for (int k = 0; k < n; k++)
        {
            int i = mem[k], b = binOf[k];
            double dl = L[i] - bl[b, 0] / cnt[b], da = A[i] - bl[b, 1] / cnt[b], db = B[i] - bl[b, 2] / cnt[b];
            res += dl * dl + da * da + db * db;
        }
        double r2 = 1 - res / tot, std = Math.Sqrt(tot / n);
        log(tag + " n=" + n + " std=" + std.ToString("0.0", CultureInfo.InvariantCulture) + " r2=" + r2.ToString("0.00", CultureInfo.InvariantCulture));
        if (std < minStd || r2 < minR2) return null;
        var g = new Grad { X1 = mx + cx * tmin, Y1 = my + cy * tmin, X2 = mx + cx * tmax, Y2 = my + cy * tmax };
        double[] bch = new double[nb];
        for (int k = 0; k < n; k++) { int i = mem[k]; bch[binOf[k]] += Math.Sqrt(A[i] * A[i] + B[i] * B[i]); }
        double[,] sat = new double[nb, 3];
        int[] satCnt = new int[nb];
        for (int k = 0; k < n; k++)
        {
            int i = mem[k], b = binOf[k];
            if (Math.Sqrt(A[i] * A[i] + B[i] * B[i]) * cnt[b] < bch[b]) continue;
            sat[b, 0] += rgba[i * 4]; sat[b, 1] += rgba[i * 4 + 1]; sat[b, 2] += rgba[i * 4 + 2]; satCnt[b]++;
        }
        var raw = new List<KeyValuePair<double, string>>();
        for (int b = 0; b < nb; b++)
        {
            if (cnt[b] < 8) continue;
            raw.Add(new KeyValuePair<double, string>((b + 0.5) / nb, Color.Hex(sat[b, 0] / satCnt[b], sat[b, 1] / satCnt[b], sat[b, 2] / satCnt[b])));
        }
        if (raw.Count < 2) return null;
        g.Stops.Add(new KeyValuePair<double, string>(0, raw[0].Value));
        g.Stops.AddRange(raw);
        g.Stops.Add(new KeyValuePair<double, string>(1, raw[raw.Count - 1].Value));
        return g;
    }

    static string GradDef(Grad g, string id)
    {
        var s = new StringBuilder();
        s.Append("<linearGradient id=\"").Append(id).Append("\" gradientUnits=\"userSpaceOnUse\" x1=\"").Append(Fmt(g.X1)).Append("\" y1=\"").Append(Fmt(g.Y1)).Append("\" x2=\"").Append(Fmt(g.X2)).Append("\" y2=\"").Append(Fmt(g.Y2)).Append("\">");
        foreach (var st in g.Stops)
            s.Append("<stop offset=\"").Append(st.Key.ToString("0.###", CultureInfo.InvariantCulture)).Append("\" stop-color=\"").Append(st.Value).Append("\"/>");
        s.Append("</linearGradient>");
        return s.ToString();
    }

    static int Main(string[] args)
    {
        string src = null, outSvg = null, outPlate = null, dbg = null;
        int KA = 30, KB = 40, minArea = 14, S = 3, gradMin = 600;
        bool verbose = false;
        double sigma = 0.3, tol = 0.5, srange = 7, corner = 52, extPx = 1.7, mergeA = 4, mergeB = 2.5, chromaThr = 20, plateFill = 0.8, gradStd = 9, gradR2 = 0.55, linkDE = 24;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            string nx = i + 1 < args.Length ? args[i + 1] : null;
            double D() => double.Parse(nx, CultureInfo.InvariantCulture);
            switch (a)
            {
                case "--source": src = nx; i++; break;
                case "--output": outSvg = nx; i++; break;
                case "--plate": outPlate = nx; i++; break;
                case "--debug": dbg = nx; i++; break;
                case "--verbose": verbose = true; break;
                case "--ka": KA = int.Parse(nx); i++; break;
                case "--kb": KB = int.Parse(nx); i++; break;
                case "--min-area": minArea = int.Parse(nx); i++; break;
                case "--scale": S = int.Parse(nx); i++; break;
                case "--sigma": sigma = D(); i++; break;
                case "--tol": tol = D(); i++; break;
                case "--range": srange = D(); i++; break;
                case "--merge-a": mergeA = D(); i++; break;
                case "--merge-b": mergeB = D(); i++; break;
                case "--chroma": chromaThr = D(); i++; break;
                case "--corner": corner = D(); i++; break;
                case "--low-area": LowArea = int.Parse(nx); i++; break;
                case "--low-de": LowDE = D(); i++; break;
                case "--link-de": linkDE = D(); i++; break;
                case "--grad-min": gradMin = int.Parse(nx); i++; break;
                case "--grad-std": gradStd = D(); i++; break;
                case "--grad-r2": gradR2 = D(); i++; break;
                case "--plate-fill": plateFill = D(); i++; break;
            }
        }
        var sw = System.Diagnostics.Stopwatch.StartNew();
        void Log(string m) { Console.WriteLine("[" + sw.Elapsed.TotalSeconds.ToString("0.0", CultureInfo.InvariantCulture) + "s] " + m); }

        var img = PngIO.Read(src);
        W = img.W; H = img.H; N = W * H; rgba = img.Rgba;
        op = new bool[N];
        L = new float[N]; A = new float[N]; B = new float[N];
        for (int i = 0; i < N; i++)
        {
            op[i] = rgba[i * 4 + 3] >= 128;
            Color.ToLab(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], out L[i], out A[i], out B[i]);
        }
        float[] oL = (float[])L.Clone(), oA = (float[])A.Clone(), oB = (float[])B.Clone();
        Log("decoded " + W + "x" + H);
        Bilateral(3, 2.0, srange, 2);
        float[] sL = L, sA = A, sB = B;
        Log("bilateral");

        float[] chroma = new float[N];
        for (int i = 0; i < N; i++) chroma[i] = MathF.Sqrt(sA[i] * sA[i] + sB[i] * sB[i]);

        var trainA = new List<float>();
        var trainB = new List<float>();
        int cntOp = 0;
        for (int i = 0; i < N; i++) if (op[i]) cntOp++;
        int stride = Math.Max(1, cntOp / 200000);
        int pc = 0;
        int[] ox = { 2, 0, 2, 2 }, oy = { 0, 2, 2, -2 };
        for (int y = 2; y < H - 2; y++)
            for (int x = 2; x < W - 2; x++)
            {
                int i = y * W + x;
                if (!op[i]) continue;
                bool edge = false;
                int[] nb = { i - 1, i + 1, i - W, i + W };
                foreach (int j in nb)
                {
                    if (!op[j]) { edge = true; break; }
                    float dl = sL[j] - sL[i], da = sA[j] - sA[i], db = sB[j] - sB[i];
                    if (dl * dl + da * da + db * db > 16) { edge = true; break; }
                }
                bool ridge = false;
                if (edge)
                {
                    for (int d = 0; d < 4 && !ridge; d++)
                    {
                        int j1 = (y + oy[d]) * W + x + ox[d], j2 = (y - oy[d]) * W + x - ox[d];
                        if (!op[j1] || !op[j2]) continue;
                        float lmin = Math.Min(sL[j1], sL[j2]), lmax = Math.Max(sL[j1], sL[j2]);
                        float cmax = Math.Max(chroma[j1], chroma[j2]);
                        if (sL[i] < lmin - 8 || sL[i] > lmax + 8 || chroma[i] > cmax + 12) ridge = true;
                    }
                }
                if (edge && !ridge) continue;
                if (!edge && (++pc % stride) != 0) continue;
                var tgt = chroma[i] > chromaThr ? trainB : trainA;
                tgt.Add(sL[i]); tgt.Add(sA[i]); tgt.Add(sB[i]);
            }
        float[] tpA = trainA.ToArray(), tpB = trainB.ToArray();
        int tnA = tpA.Length / 3, tnB = tpB.Length / 3;
        Log("train samples A=" + tnA + " B=" + tnB);
        float[] cenA = KMeans(tpA, tnA, KA, 30, 7);
        var palA = MergePalette(cenA, KA, tpA, tnA, mergeA);
        List<double[]> palB = new List<double[]>();
        if (tnB > KB * 4)
        {
            float[] cenB = KMeans(tpB, tnB, KB, 30, 11);
            palB = MergePalette(cenB, KB, tpB, tnB, mergeB);
        }
        int K = palA.Count + palB.Count;
        int chromaStart = palA.Count;
        float[] cen = new float[K * 3];
        int ci = 0;
        foreach (var p in palA.Concat(palB)) { cen[ci * 3] = (float)p[0]; cen[ci * 3 + 1] = (float)p[1]; cen[ci * 3 + 2] = (float)p[2]; ci++; }
        Log("palette " + palA.Count + " + " + palB.Count);

        int[] lab = new int[N];
        Array.Fill(lab, -2);
        Parallel.For(0, N, i =>
        {
            if (!op[i]) return;
            float best = float.MaxValue; int bc = 0;
            for (int c = 0; c < K; c++)
            {
                float d = PixelDist(i, cen, c);
                if (d < best) { best = d; bc = c; }
            }
            lab[i] = bc;
        });
        L = oL; A = oA; B = oB;
        for (int pass = 0; pass < 3; pass++)
        {
            int r1 = RemoveThin(lab, cen);
            int r2 = RemoveSmall(lab, cen, minArea);
            Log("cleanup pass " + pass + " thin=" + r1 + " small=" + r2);
        }

        int[] comp = new int[N];
        Array.Fill(comp, -1);
        var grads = new List<Grad>();
        bool[] linked = new bool[K * K];
        for (int a = 0; a < K; a++)
            for (int b = 0; b < K; b++)
                linked[a * K + b] = Math.Sqrt(Math.Pow(cen[a * 3] - cen[b * 3], 2) + Math.Pow(cen[a * 3 + 1] - cen[b * 3 + 1], 2) + Math.Pow(cen[a * 3 + 2] - cen[b * 3 + 2], 2)) < linkDE;
        {
            bool[] vis = new bool[N];
            var stack = new Stack<int>();
            var mem = new List<int>();
            for (int s = 0; s < N; s++)
            {
                if (vis[s] || !op[s] || lab[s] < chromaStart) continue;
                mem.Clear();
                stack.Push(s); vis[s] = true;
                while (stack.Count > 0)
                {
                    int i = stack.Pop();
                    mem.Add(i);
                    int x = i % W, y = i / W;
                    for (int d = 0; d < 4; d++)
                    {
                        int xx = x + (d == 0 ? -1 : d == 1 ? 1 : 0), yy = y + (d == 2 ? -1 : d == 3 ? 1 : 0);
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        int j = yy * W + xx;
                        if (vis[j] || !op[j] || lab[j] < chromaStart || !linked[lab[i] * K + lab[j]]) continue;
                        vis[j] = true; stack.Push(j);
                    }
                }
                if (mem.Count < gradMin) continue;
                Grad g = TryGradient(mem, gradStd, gradR2, "chroma component at " + (s % W) + "," + (s / W), verbose ? (Action<string>)Log : (m => { }));
                if (g == null) continue;
                grads.Add(g);
                foreach (int i in mem) comp[i] = grads.Count - 1;
            }
        }
        Log("gradient regions " + grads.Count);

        double[] sr = new double[K], sg = new double[K], sb = new double[K];
        int[] sc = new int[K];
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++)
            {
                int i = y * W + x;
                if (!op[i]) continue;
                int l = lab[i];
                if (lab[i - 1] == l && lab[i + 1] == l && lab[i - W] == l && lab[i + W] == l)
                {
                    sr[l] += rgba[i * 4]; sg[l] += rgba[i * 4 + 1]; sb[l] += rgba[i * 4 + 2]; sc[l]++;
                }
            }
        string[] hex = new string[256];
        for (int c = 0; c < K; c++)
        {
            if (sc[c] >= 6) hex[c] = Color.Hex(sr[c] / sc[c], sg[c] / sc[c], sb[c] / sc[c]);
            else
            {
                Color.FromLab(cen[c * 3], cen[c * 3 + 1], cen[c * 3 + 2], out double rr, out double gg, out double bb);
                hex[c] = Color.Hex(rr, gg, bb);
            }
        }
        for (int g = 0; g < grads.Count; g++) hex[128 + g] = "url(#asb-grad-" + (g + 1) + ")";

        int HW = W * S, HH = H * S;
        byte[] lab3 = new byte[(long)HW * HH];
        Parallel.For(0, HH, Y =>
        {
            int[] cand = new int[25];
            int[] candC = new int[25];
            for (int X = 0; X < HW; X++)
            {
                double u = (X + 0.5) / S - 0.5, v = (Y + 0.5) / S - 0.5;
                int x0 = (int)Math.Floor(u), y0 = (int)Math.Floor(v);
                double fx = u - x0, fy = v - y0;
                double al = 0, wl = 0, wa = 0, wb = 0, ws = 0;
                for (int q = 0; q < 4; q++)
                {
                    int xx = x0 + (q & 1), yy = y0 + (q >> 1);
                    double w = ((q & 1) == 1 ? fx : 1 - fx) * ((q >> 1) == 1 ? fy : 1 - fy);
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    int j = yy * W + xx;
                    al += w * rgba[j * 4 + 3] / 255.0;
                    if (!op[j]) continue;
                    wl += w * sL[j]; wa += w * sA[j]; wb += w * sB[j]; ws += w;
                }
                if (al < 0.5 || ws <= 0) { lab3[(long)Y * HW + X] = 255; continue; }
                wl /= ws; wa /= ws; wb /= ws;
                int xn = (int)Math.Round(u), yn = (int)Math.Round(v);
                int nc = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        int xx = xn + dx, yy = yn + dy;
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        int j = yy * W + xx;
                        if (!op[j]) continue;
                        int l = lab[j];
                        bool dup = false;
                        for (int t = 0; t < nc; t++) if (cand[t] == l) { dup = true; break; }
                        if (!dup) { cand[nc] = l; candC[nc] = comp[j]; nc++; }
                    }
                double best = double.MaxValue; int bl = 0, bt = 0;
                for (int t = 0; t < nc; t++)
                {
                    int l = cand[t];
                    double dl = wl - cen[l * 3], da = wa - cen[l * 3 + 1], db = wb - cen[l * 3 + 2];
                    double d = dl * dl + da * da + db * db;
                    if (d < best) { best = d; bl = l; bt = t; }
                }
                lab3[(long)Y * HW + X] = (byte)(candC[bt] >= 0 ? 128 + candC[bt] : bl);
            }
        });
        Log("hi-res labels");

        if (dbg != null)
        {
            Directory.CreateDirectory(dbg);
            byte[] o = new byte[N * 4];
            for (int i = 0; i < N; i++)
            {
                if (!op[i]) continue;
                string h = hex[lab[i]];
                o[i * 4] = Convert.ToByte(h.Substring(1, 2), 16);
                o[i * 4 + 1] = Convert.ToByte(h.Substring(3, 2), 16);
                o[i * 4 + 2] = Convert.ToByte(h.Substring(5, 2), 16);
                o[i * 4 + 3] = 255;
            }
            PngIO.Write(Path.Combine(dbg, "labels.png"), W, H, o);
        }

        int[] bx0 = new int[256], by0 = new int[256], bx1 = new int[256], by1 = new int[256];
        long[] cnt3 = new long[256];
        for (int c = 0; c < 256; c++) { bx0[c] = int.MaxValue; by0[c] = int.MaxValue; bx1[c] = -1; by1[c] = -1; }
        for (int Y = 0; Y < HH; Y++)
            for (int X = 0; X < HW; X++)
            {
                int c = lab3[(long)Y * HW + X];
                if (c == 255) continue;
                cnt3[c]++;
                if (X < bx0[c]) bx0[c] = X;
                if (X > bx1[c]) bx1[c] = X;
                if (Y < by0[c]) by0[c] = Y;
                if (Y > by1[c]) by1[c] = Y;
            }
        var order = Enumerable.Range(0, 255).Where(c => cnt3[c] > 0).OrderByDescending(c => cnt3[c]).ToList();
        int[] rank = new int[256];
        for (int r = 0; r < order.Count; r++) rank[order[r]] = r;
        Log("layers " + order.Count);

        int R = (int)Math.Round(extPx * S);
        int pad = R + (int)Math.Ceiling(3 * sigma * S) + 3;
        double sigHi = sigma * S;
        int totalSeg = 0, totalLoops = 0;
        var layerSvg = new List<string>();
        int layerNo = 0;
        foreach (int c in order)
        {
            int x0 = Math.Max(0, bx0[c] - pad), y0 = Math.Max(0, by0[c] - pad);
            int x1 = Math.Min(HW - 1, bx1[c] + pad), y1 = Math.Min(HH - 1, by1[c] + pad);
            int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
            byte[] own = new byte[bw * bh];
            for (int y = 0; y < bh; y++)
            {
                long ro = (long)(y + y0) * HW + x0;
                for (int x = 0; x < bw; x++) own[y * bw + x] = (byte)(lab3[ro + x] == c ? 1 : 0);
            }
            byte[] hz = new byte[bw * bh];
            Parallel.For(0, bh, y =>
            {
                int[] pre = new int[bw + 1];
                for (int x = 0; x < bw; x++) pre[x + 1] = pre[x] + own[y * bw + x];
                for (int x = 0; x < bw; x++)
                {
                    int lo = Math.Max(0, x - R), hi = Math.Min(bw - 1, x + R);
                    hz[y * bw + x] = (byte)(pre[hi + 1] - pre[lo] > 0 ? 1 : 0);
                }
            });
            float[] m = new float[bw * bh];
            Parallel.For(0, bw, x =>
            {
                int[] pre = new int[bh + 1];
                for (int y = 0; y < bh; y++) pre[y + 1] = pre[y] + hz[y * bw + x];
                for (int y = 0; y < bh; y++)
                {
                    bool v = own[y * bw + x] == 1;
                    if (!v)
                    {
                        int gl = lab3[(long)(y + y0) * HW + x + x0];
                        if (gl != 255 && rank[gl] > rank[c])
                        {
                            int lo = Math.Max(0, y - R), hi = Math.Min(bh - 1, y + R);
                            if (pre[hi + 1] - pre[lo] > 0) v = true;
                        }
                    }
                    m[y * bw + x] = v ? 1f : 0f;
                }
            });
            float[] f = Raster.Gauss(m, bw, bh, sigHi);
            var loops = Raster.Contours(f, bw, bh, 0.5f);
            var d2 = new StringBuilder();
            int segs = 0, nl = 0;
            foreach (var loop in loops)
            {
                var pts = new List<V>(loop.Count);
                foreach (var p in loop) pts.Add(new V((p.X + x0 + 0.5) / S, (p.Y + y0 + 0.5) / S));
                double ar = 0;
                for (int i = 0; i < pts.Count; i++)
                {
                    V a = pts[i], b = pts[(i + 1) % pts.Count];
                    ar += a.X * b.Y - b.X * a.Y;
                }
                if (Math.Abs(ar) * 0.5 < 5) continue;
                var curves = Bezier.FitLoop(pts, tol, corner, 2.5);
                if (curves.Count == 0) continue;
                d2.Append('M').Append(Fmt(curves[0][0].X)).Append(' ').Append(Fmt(curves[0][0].Y));
                foreach (var cv in curves)
                {
                    d2.Append('C');
                    d2.Append(Fmt(cv[1].X)).Append(' ').Append(Fmt(cv[1].Y)).Append(' ');
                    d2.Append(Fmt(cv[2].X)).Append(' ').Append(Fmt(cv[2].Y)).Append(' ');
                    d2.Append(Fmt(cv[3].X)).Append(' ').Append(Fmt(cv[3].Y));
                    segs++;
                }
                d2.Append('Z');
                nl++;
            }
            if (nl == 0) continue;
            layerNo++;
            totalSeg += segs; totalLoops += nl;
            layerSvg.Add("<path id=\"l" + layerNo.ToString("00") + "\" fill=\"" + hex[c] + "\" fill-rule=\"evenodd\" d=\"" + d2 + "\"/>");
            if (verbose) Console.WriteLine("  layer " + hex[c] + " area=" + (cnt3[c] / (S * S)) + " loops=" + nl + " segs=" + segs);
        }
        Log("traced");

        var defs = new StringBuilder();
        for (int g = 0; g < grads.Count; g++) defs.Append(GradDef(grads[g], "asb-grad-" + (g + 1))).Append('\n');
        var body = new StringBuilder();
        foreach (var s in layerSvg) body.Append(s).Append('\n');

        var svg = new StringBuilder();
        svg.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\" width=\"1024\" height=\"1024\">\n");
        if (grads.Count > 0) svg.Append("<defs>\n").Append(defs).Append("</defs>\n");
        svg.Append("<g id=\"emblem\">\n").Append(body).Append("</g>\n</svg>\n");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outSvg)));
        File.WriteAllText(outSvg, svg.ToString(), new UTF8Encoding(false));
        Log("wrote " + outSvg + " bytes=" + new FileInfo(outSvg).Length + " paths=" + layerSvg.Count + " loops=" + totalLoops + " segs=" + totalSeg);

        if (outPlate != null)
        {
            double minX = double.MaxValue, minY = double.MaxValue, maxX = -1, maxY = -1;
            foreach (int c in order)
            {
                minX = Math.Min(minX, bx0[c]); minY = Math.Min(minY, by0[c]);
                maxX = Math.Max(maxX, bx1[c] + 1); maxY = Math.Max(maxY, by1[c] + 1);
            }
            minX /= S; maxX /= S; minY /= S; maxY /= S;
            double sc2 = Math.Round(512 * plateFill / (maxX - minX), 4);
            double tx = Math.Round(256 - sc2 * (minX + maxX) / 2, 2), ty = Math.Round(256 - sc2 * (minY + maxY) / 2, 2);
            var ps = new StringBuilder();
            ps.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\" width=\"512\" height=\"512\">\n<defs>\n");
            ps.Append("<linearGradient id=\"asb-plate\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#14141c\"/><stop offset=\"1\" stop-color=\"#0a0a0e\"/></linearGradient>\n");
            ps.Append(defs).Append("</defs>\n");
            ps.Append("<rect x=\"0\" y=\"0\" width=\"512\" height=\"512\" rx=\"100\" ry=\"100\" fill=\"url(#asb-plate)\"/>\n");
            ps.Append("<g id=\"emblem\" transform=\"translate(").Append(tx.ToString("0.##", CultureInfo.InvariantCulture)).Append(' ').Append(ty.ToString("0.##", CultureInfo.InvariantCulture)).Append(") scale(").Append(sc2.ToString("0.####", CultureInfo.InvariantCulture)).Append(")\">\n").Append(body).Append("</g>\n</svg>\n");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPlate)));
            File.WriteAllText(outPlate, ps.ToString(), new UTF8Encoding(false));
            Log("wrote " + outPlate + " bytes=" + new FileInfo(outPlate).Length + " emblem bbox=" + minX.ToString("0.0", CultureInfo.InvariantCulture) + "," + minY.ToString("0.0", CultureInfo.InvariantCulture) + "," + maxX.ToString("0.0", CultureInfo.InvariantCulture) + "," + maxY.ToString("0.0", CultureInfo.InvariantCulture) + " scale=" + sc2 + " translate=" + tx + "," + ty);
        }
        return 0;
    }
}
'@

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('asb-vectorize-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDir | Out-Null
try {
    [System.IO.File]::WriteAllText((Join-Path $workDir 'Program.cs'), $tracerSource, (New-Object System.Text.UTF8Encoding($false)))
    Push-Location $workDir
    try {
        $ErrorActionPreference = 'Continue'
        & dotnet run Program.cs -- @tracerArgs 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -notmatch 'warning' } | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { throw "Tracer failed with exit code $LASTEXITCODE" }
    }
    finally {
        Pop-Location
    }
}
finally {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}
