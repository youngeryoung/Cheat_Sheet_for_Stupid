# This script is a derivative work based on MicroPython-uFont
# Original Repository: https://github.com/AntonVanke/MicroPython-uFont
# Original Author: AntonVanke
# Licensed under the MIT License.

import tkinter as tk
from tkinter.filedialog import askopenfilename, asksaveasfilename
from tkinter.messagebox import showinfo, showerror
import os
from PIL import ImageTk, ImageDraw, Image, ImageFont

# from bitmapfonts import run

from c_font_generator import generate_c_font_data, estimate_compiled_size

def set_font_file():
    global font_file
    font_file = askopenfilename(title='选择字体文件',
                                filetypes=[('TrueType Font', '*.ttf'), ('TrueType Font', '*.ttc'), ('All Files', '*')],
                                initialdir="./")
    font_file_show.set(font_file)
    update_preview_and_estimation()

def set_text_file():
    global text_file, text
    text_file = askopenfilename(title='选择字符集文件',
                                filetypes=[('文本文件', '*.txt'), ('All Files', '*')], initialdir="./")
    if not text_file: return
    try:
        with open(text_file, "r", encoding="utf-8") as f:
            text = f.read()
        text_input.delete("1.0", tk.END)
        text_input.insert(tk.END, text)
        text_len.set(f'字符数量: {len(set(text))}')
        text_file_show.set(text_file)
        update_preview_and_estimation()
    except Exception as e:
        showerror("错误", f"读取文件失败: {e}")

def update_preview_and_estimation(*args):
    global text
    text = text_input.get("1.0", tk.END).strip()
    text_len.set(f'字符数量: {len(set(text))}')

    get_image()
    
    if font_file and text and font_size.get() > 0:
        try:
            estimated_bytes = estimate_compiled_size(font_size.get(), text)
            estimated_size.set(f"预计增量: {estimated_bytes / 1024:.2f} KB")
        except:
            estimated_size.set("预计增量: 计算错误")
    else:
        estimated_size.set("预计增量: 0.00 KB")

def save_file():
    if font_size.get() == 0 or text == "" or font_file == "":
        showerror(title="生成失败", message="信息填写不完全!")
        return

    filepath = asksaveasfilename(
        title="选择保存位置和基础文件名",
        initialdir="./",
        defaultextension=".c",
        filetypes=[("C Source/Header Pair", "*.c *.h")]
    )
    
    if not filepath: return

    base_path, full_filename = os.path.split(filepath)
    base_name, _ = os.path.splitext(full_filename)
    c_filepath = os.path.join(base_path, base_name + ".c")
    h_filepath = os.path.join(base_path, base_name + ".h")

    try:
        font_data = generate_c_font_data(
            font_file=font_file,
            font_size=font_size.get(),
            text=text,
            offset=(offset_x.get(), offset_y.get()),
            base_name=base_name
        )
        
        with open(c_filepath, 'w', encoding='utf-8') as f:
            f.write(font_data["c_code"])
        with open(h_filepath, 'w', encoding='utf-8') as f:
            f.write(font_data["h_code"])

        showinfo(title="生成成功", message=f"字体文件已成功生成:\n{c_filepath}\n{h_filepath}")
    except Exception as e:
        showerror(title="生成失败", message=f"发生错误: {e}")

def get_image(*args):
    global img
    if font_file == "":
        return False
    
    # 防止字号为0报错
    size = font_size.get()
    if size <= 0: size = 16

    im = Image.new('1', (size, size), (1,))
    draw = ImageDraw.Draw(im)
    
    preview_char = preview_text.get()
    if len(preview_char) >= 1:
        try:
            draw.text((offset_x.get(), offset_y.get()), preview_char[0],
                      font=ImageFont.truetype(font=font_file, size=size))
            img = ImageTk.BitmapImage(im)
            # 更新已存在的 label，而不是新建
            preview_image_label.config(image=img)
            return True
        except Exception as e:
            print(f"Preview error: {e}")
            return False
    return False

# --- UI 构建部分 (重构后) ---

root = tk.Tk()
root.title("BitMap Font Tools")
# 移除固定大小，设置最小尺寸，允许拉伸
root.geometry("700x600") 
root.minsize(600, 500)

# 定义全局变量
font_file = ""
text_file = ""
text = ""

estimated_size = tk.StringVar(value="预计增量: 0.00 KB")
font_size = tk.IntVar(value=16)
font_file_show = tk.StringVar()
text_file_show = tk.StringVar()
offset_x = tk.IntVar(value=0)
offset_y = tk.IntVar(value=0)
text_len = tk.StringVar(value="字符数量: 0")
preview_text = tk.StringVar(value="你")

# 配置根布局 Grid 权重：左侧(0)主要内容，右侧(1)侧边栏
root.columnconfigure(0, weight=3)
root.columnconfigure(1, minsize=220, weight=1)
root.rowconfigure(0, weight=0) # 上方设置区自适应高度
root.rowconfigure(1, weight=1) # 下方文本区自动填充

# === 左侧面板 ===
left_panel = tk.Frame(root)
left_panel.grid(row=0, column=0, rowspan=2, sticky="nsew", padx=10, pady=10)
left_panel.columnconfigure(0, weight=1)
left_panel.rowconfigure(1, weight=1) # 让文本框区域可伸缩

# 1. 参数设置区域
setting_frame = tk.LabelFrame(left_panel, text="参数设置")
setting_frame.grid(row=0, column=0, sticky="ew", pady=(0, 10))

# 字体选择
tk.Label(setting_frame, text="字体文件:").grid(row=0, column=0, sticky="e", padx=5, pady=5)
tk.Entry(setting_frame, textvariable=font_file_show).grid(row=0, column=1, sticky="ew", padx=5)
tk.Button(setting_frame, text="选择", command=set_font_file).grid(row=0, column=2, padx=5)

# 字符集选择
tk.Label(setting_frame, text="字符集文件:").grid(row=1, column=0, sticky="e", padx=5, pady=5)
tk.Entry(setting_frame, textvariable=text_file_show).grid(row=1, column=1, sticky="ew", padx=5)
tk.Button(setting_frame, text="选择", command=set_text_file).grid(row=1, column=2, padx=5)

# 字号
tk.Label(setting_frame, text="字号:").grid(row=2, column=0, sticky="e", padx=5, pady=5)
tk.Scale(setting_frame, variable=font_size, from_=8, to=64, orient=tk.HORIZONTAL, 
         command=update_preview_and_estimation).grid(row=2, column=1, sticky="ew", padx=5)
tk.Button(setting_frame, text="重置", command=lambda: font_size.set(16) or update_preview_and_estimation()).grid(row=2, column=2, padx=5)

# 偏移
tk.Label(setting_frame, text="X轴偏移:").grid(row=3, column=0, sticky="e", padx=5)
tk.Scale(setting_frame, variable=offset_x, from_=-16, to=16, orient=tk.HORIZONTAL,
         command=update_preview_and_estimation).grid(row=3, column=1, sticky="ew", padx=5)

tk.Label(setting_frame, text="Y轴偏移:").grid(row=4, column=0, sticky="e", padx=5)
tk.Scale(setting_frame, variable=offset_y, from_=-16, to=16, orient=tk.HORIZONTAL,
         command=update_preview_and_estimation).grid(row=4, column=1, sticky="ew", padx=5)

tk.Button(setting_frame, text="重置偏移", 
          command=lambda: (offset_x.set(0), offset_y.set(0), update_preview_and_estimation())).grid(row=3, column=2, rowspan=2, padx=5)

setting_frame.columnconfigure(1, weight=1) # 让中间的 Entry 和 Scale 拉伸

# 2. 字体集预览区域 (Text Area)
text_frame = tk.LabelFrame(left_panel, text="字体集预览")
text_frame.grid(row=1, column=0, sticky="nsew")

text_scroll = tk.Scrollbar(text_frame)
text_scroll.pack(side=tk.RIGHT, fill=tk.Y)

text_input = tk.Text(text_frame, wrap=tk.CHAR, undo=True, yscrollcommand=text_scroll.set, height=10)
text_input.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=5, pady=5)
text_scroll.config(command=text_input.yview)
text_input.bind('<KeyRelease>', update_preview_and_estimation)

tk.Label(text_frame, textvariable=text_len).pack(side=tk.BOTTOM, anchor="w", padx=5)


# === 右侧面板 ===
right_panel = tk.Frame(root)
right_panel.grid(row=0, column=1, rowspan=2, sticky="nsew", padx=(0, 10), pady=10)

# 3. 预览区域
font_preview = tk.LabelFrame(right_panel, text="单字预览")
font_preview.pack(fill=tk.X, pady=(0, 10))

input_frame = tk.Frame(font_preview)
input_frame.pack(fill=tk.X, padx=5, pady=5)
preview_text_input = tk.Entry(input_frame, textvariable=preview_text, width=5)
preview_text_input.pack(side=tk.LEFT, padx=5)
preview_text_input.bind("<KeyRelease>", update_preview_and_estimation)
tk.Button(input_frame, text="刷新", command=update_preview_and_estimation).pack(side=tk.LEFT, padx=5)

# 图片预览标签 (创建一个固定的Label容器)
preview_container = tk.Frame(font_preview, bd=2, relief="sunken", bg="white", height=100)
preview_container.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
preview_container.pack_propagate(False) # 防止 Label 撑大容器
preview_image_label = tk.Label(preview_container, bg="white")
preview_image_label.place(relx=0.5, rely=0.5, anchor="center") # 在容器内居中

# 4. 生成区域
bmf_generate = tk.LabelFrame(right_panel, text="生成操作")
bmf_generate.pack(fill=tk.BOTH, expand=True)

tk.Label(bmf_generate, textvariable=estimated_size, font=("Arial", 10, "bold"), fg="#333").pack(pady=20)
tk.Button(bmf_generate, text="生成点阵文件", command=save_file, height=2, bg="#ddd").pack(fill=tk.X, padx=20)

# 启动主循环
root.mainloop()