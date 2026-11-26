"""生成内置 wav 样本 `fml/src/fml_audio_sample.c`（构建期素材，勿手改）。

为什么要生成而不是手写：P9e-2 的音乐播放器要解析真实 RIFF/wav 头（fmt/data），
如果样本是"运行时算出来的正弦"，解析器就永远走不到解析分支。所以这里生成一份
完整、合规的 PCM wav 字节流（8 kHz / 单声道 / 16 位 / 0.6 s / 440-554-659 Hz 三音阶），
播放器和 POST 对它和对卷内文件走同一条解析路径。

用法：python build/gen_wav_sample.py    （输出 fml/src/fml_audio_sample.c）
"""

import io
import math
import struct

RATE = 8000
CH = 1
BITS = 16
NOTES = ((440.0, 0.2), (554.0, 0.2), (659.0, 0.2))
AMP = 0.78          # 相对满幅
FADE = 0.005        # 每个音符首尾 5 ms 渐入渐出，避免切换时的"咔"

def pcm_frames():
    out = []
    for hz, sec in NOTES:
        n = int(RATE * sec)
        fade_n = int(RATE * FADE)
        for i in range(n):
            v = math.sin(2.0 * math.pi * hz * (i / RATE))
            g = 1.0
            if i < fade_n:
                g = i / fade_n
            elif i >= n - fade_n:
                g = (n - i - 1) / fade_n
            out.append(int(round(v * g * AMP * 32767.0)))
    return out

def main():
    frames = pcm_frames()
    data = b''.join(struct.pack('<h', s) for s in frames)
    data_size = len(data)
    byte_rate = RATE * CH * BITS // 8
    block_align = CH * BITS // 8
    hdr = b'RIFF' + struct.pack('<I', 36 + data_size) + b'WAVE'
    hdr += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, CH, RATE, byte_rate, block_align, BITS)
    hdr += b'data' + struct.pack('<I', data_size)
    blob = hdr + data

    lines = []
    lines.append('/* 自动生成，勿手改 —— 生成器 build/gen_wav_sample.py。')
    lines.append(' * %d Hz / %d ch / %d bit / %d 帧（%.3f s）/ 三音阶 440-554-659 Hz。' %
                 (RATE, CH, BITS, len(frames), len(frames) / RATE))
    lines.append(' * 只放字节流，解析与重采样全在 fml_audio.c 里做，和卷内 .WAV 同一条路径。 */')
    lines.append('')
    lines.append('#include <stddef.h>')
    lines.append('')
    lines.append('const unsigned char fml_audio_sample_wav[%d] = {' % len(blob))
    for i in range(0, len(blob), 12):
        chunk = blob[i:i + 12]
        lines.append('    ' + ' '.join('0x%02X,' % b for b in chunk))
    lines.append('};')
    lines.append('')
    lines.append('const unsigned int fml_audio_sample_wav_len = %dU;' % len(blob))
    lines.append('')

    p = r'D:\Users\Local\stm32-freertos\fml\src\fml_audio_sample.c'
    io.open(p, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print('wrote %s  frames=%d data=%d total=%d bytes' % (p, len(frames), data_size, len(blob)))

main()
