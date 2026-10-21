$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$packRoot = Split-Path -Parent $PSScriptRoot
$previewRoot = Join-Path $packRoot 'previews'

function New-AssetContactSheet
{
    param([string]$Output, [string[]]$Names, [string[]]$Labels, [int]$Columns = 3)

    $cell = 400
    $rowHeight = 440
    $rows = [int][Math]::Ceiling($Names.Length / [double]$Columns)
    $bitmap = [System.Drawing.Bitmap]::new($cell * $Columns, $rowHeight * $rows)
    $canvas = [System.Drawing.Graphics]::FromImage($bitmap)
    $font = [System.Drawing.Font]::new('Segoe UI', 14)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(221, 215, 196))

    try
    {
        $canvas.Clear([System.Drawing.Color]::FromArgb(43, 47, 45))
        $canvas.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

        for ($index = 0; $index -lt $Names.Length; $index++)
        {
            $imagePath = Join-Path $previewRoot ($Names[$index] + '.png')
            $sourceImage = [System.Drawing.Image]::FromFile($imagePath)

            try
            {
                $left = ($index % $Columns) * $cell
                $top = [int][Math]::Floor($index / [double]$Columns) * $rowHeight
                $canvas.DrawImage($sourceImage, $left, $top, $cell, $cell)
                $canvas.DrawString($Labels[$index], $font, $brush, $left + 12, $top + $cell + 7)
            }
            finally
            {
                $sourceImage.Dispose()
            }
        }

        $bitmap.Save((Join-Path $previewRoot $Output), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally
    {
        $canvas.Dispose()
        $font.Dispose()
        $brush.Dispose()
        $bitmap.Dispose()
    }
}

New-AssetContactSheet -Output 'asset_overview.png' `
    -Names @('human_protagonist', 'human_porter', 'human_herbalist', 'animal_tiger', 'animal_wolf', 'animal_deer', 'animal_ox', 'scene_medicine_shop', 'nature_library') `
    -Labels @('PROTAGONIST', 'PORTER / JIGE', 'HERBALIST', 'TIGER / DANGEROUS', 'WOLF / TERRITORIAL', 'DEER / PASSIVE', 'OX / DOMESTIC', 'MODULAR MEDICINE SHOP', 'NATURE / TERRAIN')

New-AssetContactSheet -Output 'animation_poses.png' -Columns 2 `
    -Names @('pose_human_protagonist_run', 'pose_human_protagonist_jump', 'pose_animal_tiger_run', 'pose_animal_deer_jump') `
    -Labels @('HUMAN / RUN', 'HUMAN / JUMP', 'TIGER / RUN', 'DEER / JUMP')

New-AssetContactSheet -Output 'story_humans.png' -Columns 3 `
    -Names @('human_mira_engineer', 'human_doyun_clerk', 'human_serat_inspector', 'human_ian_broker', 'human_haram_apprentice') `
    -Labels @('MIRA / DRAINAGE ENGINEER', 'DOYUN / UNION CLERK', 'SERAT / PORT INSPECTOR', 'IAN / NIGHT-MARKET BROKER', 'HARAM / REPAIR APPRENTICE')

New-AssetContactSheet -Output 'all_humans.png' -Columns 4 `
    -Names @('human_protagonist', 'human_porter', 'human_herbalist', 'human_mira_engineer', 'human_doyun_clerk', 'human_serat_inspector', 'human_ian_broker', 'human_haram_apprentice') `
    -Labels @('PROTAGONIST', 'PORTER', 'HERBALIST', 'MIRA / ENGINEER', 'DOYUN / CLERK', 'SERAT / INSPECTOR', 'IAN / BROKER', 'HARAM / APPRENTICE')
