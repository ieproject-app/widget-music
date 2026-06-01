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
}
'@

if (-not ('WidgetMusicTrayDeskBand' -as [type])) {
  Add-Type -TypeDefinition $typeDef -Language CSharp
}

try {
  $result = [WidgetMusicTrayDeskBand]::EnsureShown($DeskBandClsid)
  Write-Host "[Enable] $result"
  if ($result -match 'shown_after=0x00000000') {
    Write-Host '[Enable] Widget Music is now shown on the taskbar.'
    exit 0
  }

  Write-Host '[Enable] Deskband show command completed but taskbar did not report shown state.'
  exit 1
} catch {
  Write-Host ("[Enable] Failed to enable Widget Music on taskbar: " + $_.Exception.Message)
  exit 1
}
