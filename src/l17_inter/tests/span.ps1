# Span-size survey for inter_prediction.c candidates. Reads spans from
# stdin lines "a b" and prints strict nb (non-blank, non-bare-brace) per span.
$lines = [IO.File]::ReadAllLines("third_party/SVT-AV1/Source/Lib/Codec/inter_prediction.c")
$enc = [IO.File]::ReadAllLines("third_party/SVT-AV1/Source/Lib/Codec/enc_inter_prediction.c")
$spans = [IO.File]::ReadAllLines("src/l17_inter/tests/spans.txt") | Where-Object { $_ -match "^\s*\d+\s+\d+\s+\S+" }
foreach ($s in $spans) {
    $f = $s -split "\s+"
    $a = [int]$f[0]; $b = [int]$f[1]; $file = $f[2]
    $src = if ($file -eq "enc") { $enc } else { $lines }
    $nb = 0
    for ($i = $a; $i -le $b; $i++) {
        $t = $src[$i - 1].Trim()
        if ($t -ne "" -and $t -ne "{" -and $t -ne "}") { $nb++ }
    }
    "span $a-$b ($file) : $nb nb"
}
