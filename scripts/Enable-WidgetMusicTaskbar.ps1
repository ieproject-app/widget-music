param(
  [string]$DeskBandClsid = '0E716D1F-3D3D-4A57-878D-A7DFC29D9115'
)

$ErrorActionPreference = 'Stop'

$typeDef = @'
using System;
using System.Runtime.InteropServices;

[ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("6D67E846-5B9C-4db8-9CBC-DDE12F4254F1")]
public interface ITrayDeskBand
{
    [PreserveSig] int ShowDeskBand(ref Guid clsid);
    [PreserveSig] int HideDeskBand(ref Guid clsid);
    [PreserveSig] int IsDeskBandShown(ref Guid clsid);
    [PreserveSig] int DeskBandRegistrationChanged();
}

public static class WidgetMusicTrayDeskBand
{
    public static string EnsureShown(string deskBandClsid)
    {
        Guid trayClsid = new Guid("E6442437-6C68-4F52-94DD-2CFED267EFB9");
        Guid bandClsid = new Guid(deskBandClsid);
        Type t = Type.GetTypeFromCLSID(trayClsid, true);
        ITrayDeskBand api = (ITrayDeskBand)Activator.CreateInstance(t);
        try
        {
            int hrBefore = api.IsDeskBandShown(ref bandClsid);
            int hrRefresh = api.DeskBandRegistrationChanged();
            int hrShow = api.ShowDeskBand(ref bandClsid);
            int hrAfter = api.IsDeskBandShown(ref bandClsid);
            int hrRefreshAfter = api.DeskBandRegistrationChanged();

            return string.Format(
                "shown_before=0x{0:X8}; refresh=0x{1:X8}; show=0x{2:X8}; shown_after=0x{3:X8}; refresh_after=0x{4:X8}",
                hrBefore, hrRefresh, hrShow, hrAfter, hrRefreshAfter);
        }
        finally
        {
            if (api != null) Marshal.ReleaseComObject(api);
        }
    }

    public static string EnsureShownWithRetry(string deskBandClsid, int attempts, int pollCount, int pollDelayMs)
    {
        if (attempts < 1) attempts = 1;
        if (pollCount < 1) pollCount = 1;
        if (pollDelayMs < 0) pollDelayMs = 0;

        int lastBefore = 1;
        int lastRefresh = unchecked((int)0x80004005);
        int lastShow = unchecked((int)0x80004005);
        int lastAfter = 1;
        int lastRefreshAfter = unchecked((int)0x80004005);
        int usedAttempts = 0;
        int lastErrorHr = 0;
        string lastError = string.Empty;

        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            usedAttempts = attempt;
            try
            {
                Guid trayClsid = new Guid("E6442437-6C68-4F52-94DD-2CFED267EFB9");
                Guid bandClsid = new Guid(deskBandClsid);
                Type t = Type.GetTypeFromCLSID(trayClsid, true);
                ITrayDeskBand api = (ITrayDeskBand)Activator.CreateInstance(t);
                try
                {
                    lastBefore = api.IsDeskBandShown(ref bandClsid);
                    lastRefresh = api.DeskBandRegistrationChanged();
                    lastShow = api.ShowDeskBand(ref bandClsid);
                    lastAfter = api.IsDeskBandShown(ref bandClsid);
                    lastRefreshAfter = api.DeskBandRegistrationChanged();

                    if (lastAfter == 0)
                    {
                        break;
                    }

                    for (int poll = 0; poll < pollCount; poll++)
                    {
                        if (pollDelayMs > 0)
                        {
                            System.Threading.Thread.Sleep(pollDelayMs);
                        }

                        lastAfter = api.IsDeskBandShown(ref bandClsid);
                        if (lastAfter == 0)
                        {
                            break;
                        }
                    }

                    if (lastAfter == 0)
                    {
                        break;
                    }
                }
                finally
                {
                    if (api != null) Marshal.ReleaseComObject(api);
                }
            }
            catch (Exception ex)
            {
                lastErrorHr = Marshal.GetHRForException(ex);
                lastError = ex.Message;
                if (pollDelayMs > 0)
                {
                    System.Threading.Thread.Sleep(pollDelayMs);
                }
            }
        }

        return string.Format(
            "attempts={0}; shown_before=0x{1:X8}; refresh=0x{2:X8}; show=0x{3:X8}; shown_after=0x{4:X8}; refresh_after=0x{5:X8}; last_error_hr=0x{6:X8}; last_error={7}",
            usedAttempts, lastBefore, lastRefresh, lastShow, lastAfter, lastRefreshAfter, lastErrorHr, lastError);
    }
}
'@

if (-not ('WidgetMusicTrayDeskBand' -as [type])) {
  Add-Type -TypeDefinition $typeDef -Language CSharp
}

try {
  $result = [WidgetMusicTrayDeskBand]::EnsureShownWithRetry($DeskBandClsid, 5, 5, 200)
  Write-Host "[Enable] $result"
  if ($result -match 'shown_after=0x00000000') {
    Write-Host '[Enable] SnipTune 10 is now shown on the taskbar.'
    exit 0
  }

  Write-Host '[Enable] Deskband show command completed but taskbar did not report shown state.'
  exit 1
} catch {
  Write-Host ("[Enable] Failed to enable SnipTune 10 on taskbar: " + $_.Exception.Message)
  exit 1
}
