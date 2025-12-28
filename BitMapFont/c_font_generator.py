# This script is a derivative work based on MicroPython-uFont
# Original Repository: https://github.com/AntonVanke/MicroPython-uFont
# Original Author: AntonVanke
# Licensed under the MIT License.

# c_font_generator.py

from PIL import Image, ImageDraw, ImageFont
import math

def get_char_image(char, width, height, font, offset=(0, 0)) -> Image.Image:
    im = Image.new('1', (width, height), 1)
    draw = ImageDraw.Draw(im)
    draw.text(offset, char, font=font, fill=0)
    return im

def convert_image_to_column_major_bytes(im: Image.Image) -> list[int]:
    width, height = im.size
    bytes_per_col = (height + 7) // 8
    output_bytes = []
    for x in range(width):
        col_bytes = [0] * bytes_per_col
        for y in range(height):
            if im.getpixel((x, y)) == 0:
                byte_index = y // 8
                bit_index = y % 8
                col_bytes[byte_index] |= (1 << bit_index)
        output_bytes.extend(col_bytes)
    return output_bytes

def generate_c_font_data(font_file, font_size, text, offset, base_name="custom_font"):
    """
    生成优化的C字体文件:
    1. ASCII (fonta): 固定包含 ASCII 32-126, 支持 O(1) 索引查找。
    2. Unicode (fontu): 仅包含 text 中出现的高位字符, 按编码排序, 支持二分查找。
    """
    font = ImageFont.truetype(font=font_file, size=font_size)
    
    # 尺寸计算
    font_height = font_size
    full_width = font_size
    half_width = font_size // 2  # 强制 ASCII 为字号的一半宽度

    # ---- 1. 处理 ASCII 部分 (32-126) ----
    # 无论输入 text 是什么，这里都生成标准 ASCII 表
    # 这样驱动中可以直接用 (char - 32) 查找，速度最快
    ascii_range = range(32, 127) # ' ' to '~'
    
    # ---- 2. 处理 Unicode 部分 (>= 128) ----
    # 过滤、去重、排序 (排序是二分查找的关键!)
    if text:
        unique_chars = sorted(list(set([c for c in text if ord(c) >= 128])))
    else:
        unique_chars = []

    # ==========================================================
    #                  生成 .h 文件
    # ==========================================================
    h_guard = f"__FONT_{base_name.upper()}_H__"
    h_code = f"#ifndef {h_guard}\n#define {h_guard}\n\n"
    h_code += '#include "stdint.h"\n\n'
    
    # 定义 ASCII 字体结构体
    h_code += "typedef struct {\n"
    h_code += "    uint8_t h;\n"
    h_code += "    uint8_t w;\n"
    h_code += "    const uint8_t *chars;\n"
    h_code += "} ASCIIFont;\n\n"

    # 定义 Unicode 字体结构体
    h_code += "typedef struct {\n"
    h_code += "    uint8_t h;\n"
    h_code += "    uint8_t w;\n"
    h_code += "    uint16_t len;           // 字符个数\n"
    h_code += "    const uint8_t *chars;   // 数据数组起始地址\n"
    h_code += "} UnicodeFont;\n\n"
    
    # 外部变量声明 (统一命名)
    h_code += f"// Font: {font_file}, Size: {font_size}px\n"
    h_code += f"extern const ASCIIFont fonta;\n"
    if unique_chars:
        h_code += f"extern const UnicodeFont fontu;\n"
        
    h_code += f"\n#endif // {h_guard}\n"

    # ==========================================================
    #                  生成 .c 文件
    # ==========================================================
    c_code = f'#include "{base_name}.h"\n\n'
    
    # ---- 生成 ASCII 数据 (fonta) ----
    bytes_per_col = (font_height + 7) // 8
    bytes_per_ascii_char = half_width * bytes_per_col
    
    ascii_data_name = f"fonta_data"
    
    c_code += f"// ASCII Data (32-126), {half_width}x{font_height}\n"
    c_code += f"static const uint8_t {ascii_data_name}[][{bytes_per_ascii_char}] = {{\n"
    
    for i in ascii_range:
        char = chr(i)
        im = get_char_image(char, half_width, font_height, font, offset)
        byte_data = convert_image_to_column_major_bytes(im)
        hex_str = ", ".join([f"0x{b:02x}" for b in byte_data])
        # 转义特殊字符以便在注释显示
        comment_char = char.replace('\\', '\\\\').replace('"', '\\"')
        c_code += f"    {{ {hex_str} }}, // '{comment_char}'\n"
    c_code += "};\n\n"

    c_code += f"const ASCIIFont fonta = {{\n"
    c_code += f"    .h = {font_height},\n"
    c_code += f"    .w = {half_width},\n"
    c_code += f"    .chars = (const uint8_t *){ascii_data_name}\n"
    c_code += "};\n\n"

    # ---- 生成 Unicode 数据 (fontu) ----
    if unique_chars:
        bytes_per_char_data = full_width * bytes_per_col
        # 结构: [UTF-8 Key (4 bytes)] + [Bitmap Data]
        # 这样保证每个元素大小一致，可以直接进行指针偏移计算，方便二分查找
        total_bytes_per_entry = 4 + bytes_per_char_data
        
        zh_data_name = f"fontu_data"
        c_code += f"// Unicode Data (Sorted), {full_width}x{font_height}\n"
        c_code += f"// Each entry: 4 bytes UTF-8 code + Bitmap data\n"
        c_code += f"static const uint8_t {zh_data_name}[][{total_bytes_per_entry}] = {{\n"
        
        for i, char in enumerate(unique_chars):
            im = get_char_image(char, full_width, font_height, font, offset)
            byte_data = convert_image_to_column_major_bytes(im)
            
            # UTF-8 编码 (补齐4字节以便内存对齐和统一比较)
            utf8_bytes = char.encode('utf-8')
            utf8_hex = [f"0x{b:02x}" for b in utf8_bytes]
            padding = ['0x00'] * (4 - len(utf8_hex))
            
            bitmap_hex = [f"0x{b:02x}" for b in byte_data]
            
            c_code += f"    {{ {', '.join(utf8_hex + padding)}, {', '.join(bitmap_hex)} }}, // {char}\n"
        c_code += "};\n\n"

        c_code += f"const UnicodeFont fontu = {{\n"
        c_code += f"    .h = {font_height},\n"
        c_code += f"    .w = {full_width},\n"
        c_code += f"    .len = {len(unique_chars)},\n"
        c_code += f"    .chars = (const uint8_t *){zh_data_name}\n"
        c_code += "};\n"

    return {"c_code": c_code, "h_code": h_code}

def estimate_compiled_size(font_size, text):
    # 简单估算，不影响生成逻辑
    if font_size <= 0: return 0
    font_height = font_size
    bytes_per_col = (font_height + 7) // 8
    
    # ASCII Size
    ascii_size = 95 * (font_size // 2) * bytes_per_col
    
    # Unicode Size
    cjk_count = 0
    if text:
        cjk_count = len(set([c for c in text if ord(c) >= 128]))
    cjk_size = cjk_count * (4 + font_size * bytes_per_col)
    
    return ascii_size + cjk_size