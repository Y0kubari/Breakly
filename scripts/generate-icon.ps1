$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$images=[Collections.Generic.List[byte[]]]::new()
$sizes=@(16,32,48,256)
foreach($size in $sizes) {
    $bitmap=[Drawing.Bitmap]::new($size,$size)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $brush=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(20,125,112))
    $white=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(245,255,255))
    $graphics.FillEllipse($brush,1,1,$size-2,$size-2)
    $graphics.FillRectangle($white,[single]($size*.33),[single]($size*.27),[single]($size*.12),[single]($size*.46))
    $graphics.FillRectangle($white,[single]($size*.55),[single]($size*.27),[single]($size*.12),[single]($size*.46))
    $memory=[IO.MemoryStream]::new()
    $bitmap.Save($memory,[Drawing.Imaging.ImageFormat]::Png)
    $images.Add($memory.ToArray())
    $memory.Dispose();$white.Dispose();$brush.Dispose();$graphics.Dispose();$bitmap.Dispose()
}
$path=Join-Path (Split-Path $PSScriptRoot -Parent) 'resources\Breakly.ico'
$stream=[IO.File]::Create($path)
$writer=[IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
    $offset=6+16*$sizes.Count
    for($i=0;$i -lt $sizes.Count;$i++) {
        $side=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
        $writer.Write([byte]$side);$writer.Write([byte]$side);$writer.Write([byte]0);$writer.Write([byte]0)
        $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$images[$i].Length);$writer.Write([uint32]$offset)
        $offset+=$images[$i].Length
    }
    foreach($bytes in $images){$writer.Write($bytes)}
} finally {$writer.Dispose();$stream.Dispose()}
