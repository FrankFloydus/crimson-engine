param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$ExecutablePath
)
$ErrorActionPreference = 'Stop'
if (-not $ExecutablePath) {
    $ExecutablePath = Join-Path (Split-Path -Parent $PSScriptRoot) "build\$Configuration\bin\crimson-renderer.exe"
}
$executable = Get-Item -LiteralPath $ExecutablePath
if ($executable.PSIsContainer -or $executable.Extension -ne '.exe') {
    throw 'ExecutablePath must identify an existing executable file.'
}
$ExecutablePath = $executable.FullName

if (-not ('CrimsonShellIconRefresh' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CrimsonShellIconRefresh
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHFILEINFO
    {
        public IntPtr hIcon;
        public int iIcon;
        public uint dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string szTypeName;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern IntPtr SHGetFileInfoW(string path, uint attributes,
        out SHFILEINFO info, uint size, uint flags);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern void SHUpdateImageW(string iconPath, int iconIndex, uint flags, int imageIndex);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern void SHChangeNotify(int eventId, uint flags, string item, IntPtr unused);

    public static void NotifyItem(string path)
    {
        // UPDATEITEM, PATHW | FLUSH: deliver a notification for this item only.
        SHChangeNotify(0x2000, 0x1005, path, IntPtr.Zero);
    }

    public static int Refresh(string path)
    {
        SHFILEINFO image;
        uint size = (uint)Marshal.SizeOf(typeof(SHFILEINFO));
        // No SHGFI_ICON: these queries do not allocate icon handles.
        if (SHGetFileInfoW(path, 0, out image, size, 0x4000) == IntPtr.Zero)
            throw new InvalidOperationException("Cannot locate the cached Shell icon for " + path);
        // CrimsonResources embeds exactly one icon group. Its existing Shell
        // cache key is this executable's first icon (the shortcut uses path,0).
        // ICONLOCATION can be empty for executable Shell handlers.
        SHUpdateImageW(path, 0, 0, image.iIcon);
        NotifyItem(path);
        return image.iIcon;
    }
}
'@
}

$imageIndex = [CrimsonShellIconRefresh]::Refresh($ExecutablePath)
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Crimson Engine.lnk'
if (Test-Path -LiteralPath $shortcutPath -PathType Leaf) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $null
    try {
        $shortcut = $shell.CreateShortcut($shortcutPath)
        if ($shortcut.TargetPath -and [string]::Equals(
            [IO.Path]::GetFullPath($shortcut.TargetPath), $ExecutablePath, [StringComparison]::OrdinalIgnoreCase)) {
            [CrimsonShellIconRefresh]::NotifyItem($shortcutPath)
        }
    } finally {
        if ($shortcut) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut) }
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    }
}
Write-Host "Refreshed Shell icon $imageIndex for $ExecutablePath"
