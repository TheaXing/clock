"""Regenerate fixed reference matrices with the independent qrcode package."""
from pathlib import Path
import qrcode
from qrcode.util import QRData, MODE_8BIT_BYTE

payloads = {
    "wifi": "WIFI:T:WPA;S:Clock-Setup-1234ABCD;P:0123456789ABCDEF;;",
    "url": "http://192.168.4.1/setup",
}
lines = ["/* 使用独立 Python qrcode 8.2 编码器生成的版本四、中纠错、掩码三参考位图。 */"]
for name, text in payloads.items():
    qr = qrcode.QRCode(version=4, error_correction=qrcode.constants.ERROR_CORRECT_M,
                       border=0, mask_pattern=3)
    qr.add_data(QRData(text, mode=MODE_8BIT_BYTE), optimize=0)
    qr.make(fit=False)
    lines.append(f"static const char *const {name}_reference[] = {{")
    for row in qr.get_matrix():
        lines.append('    "' + ''.join('1' if bit else '0' for bit in row) + '",')
    lines.append("};")
Path(__file__).with_name("reference.h").write_text("\n".join(lines) + "\n", encoding="utf-8")
