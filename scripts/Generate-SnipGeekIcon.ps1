param(
  [string]$SourcePng,
  [string]$OutputIco,
  [int[]]$Sizes = @(16, 32, 48, 64, 256)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if ([string]::IsNullOrWhiteSpace($SourcePng)) {
  $SourcePng = Join-Path $root 'assets\icons\snipgeek-512.png'
} elseif (-not [System.IO.Path]::IsPathRooted($SourcePng)) {
  $SourcePng = Join-Path $root $SourcePng
}

if ([string]::IsNullOrWhiteSpace($OutputIco)) {
  $OutputIco = Join-Path $root 'assets\icons\snipgeek.ico'
} elseif (-not [System.IO.Path]::IsPathRooted($OutputIco)) {
  $OutputIco = Join-Path $root $OutputIco
}

if (-not (Test-Path -LiteralPath $SourcePng -PathType Leaf)) {
  throw "Source PNG not found: $SourcePng"
}

Add-Type -AssemblyName System.Drawing

if (-not ('WidgetMusic.IconBuilder' -as [type])) {
  Add-Type -ReferencedAssemblies 'System.Drawing.dll' -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

namespace WidgetMusic {
  public static class IconBuilder {
    public static void Build(string sourcePath, string outputPath, int[] sizes) {
      if (sizes == null || sizes.Length == 0) {
        throw new ArgumentException("At least one icon size is required.", "sizes");
      }

      using (Image source = Image.FromFile(sourcePath)) {
        List<byte[]> frames = new List<byte[]>(sizes.Length);
        foreach (int size in sizes) {
          if (size <= 0 || size > 256) {
            throw new ArgumentOutOfRangeException("sizes", "Icon sizes must be between 1 and 256 pixels.");
          }

          frames.Add(RenderFrame(source, size));
        }

        string fullOutputPath = Path.GetFullPath(outputPath);
        string directory = Path.GetDirectoryName(fullOutputPath);
        if (!string.IsNullOrEmpty(directory)) {
          Directory.CreateDirectory(directory);
        }

        using (FileStream stream = new FileStream(fullOutputPath, FileMode.Create, FileAccess.Write))
        using (BinaryWriter writer = new BinaryWriter(stream)) {
          writer.Write((ushort)0);
          writer.Write((ushort)1);
          writer.Write((ushort)frames.Count);

          int offset = 6 + (frames.Count * 16);
          for (int i = 0; i < frames.Count; i++) {
            int size = sizes[i];
            byte dimension = size >= 256 ? (byte)0 : (byte)size;
            byte[] frame = frames[i];

            writer.Write(dimension);
            writer.Write(dimension);
            writer.Write((byte)0);
            writer.Write((byte)0);
            writer.Write((ushort)1);
            writer.Write((ushort)32);
            writer.Write(frame.Length);
            writer.Write(offset);

            offset += frame.Length;
          }

          foreach (byte[] frame in frames) {
            writer.Write(frame);
          }
        }
      }
    }

    private static byte[] RenderFrame(Image source, int size) {
      using (Bitmap bitmap = new Bitmap(size, size, PixelFormat.Format32bppArgb))
      using (Graphics graphics = Graphics.FromImage(bitmap))
      using (MemoryStream stream = new MemoryStream()) {
        graphics.Clear(Color.Transparent);
        graphics.CompositingMode = CompositingMode.SourceOver;
        graphics.CompositingQuality = CompositingQuality.HighQuality;
        graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        graphics.SmoothingMode = SmoothingMode.AntiAlias;
        graphics.DrawImage(
          source,
          new Rectangle(0, 0, size, size),
          new Rectangle(0, 0, source.Width, source.Height),
          GraphicsUnit.Pixel);

        bitmap.Save(stream, ImageFormat.Png);
        return stream.ToArray();
      }
    }
  }
}
'@
}

[WidgetMusic.IconBuilder]::Build($SourcePng, $OutputIco, $Sizes)

$label = ($Sizes | ForEach-Object { "${_}x${_}" }) -join ', '
Write-Host "Generated $OutputIco with sizes: $label"
