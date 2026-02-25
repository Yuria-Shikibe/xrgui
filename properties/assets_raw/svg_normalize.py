import argparse
import os
import sys
import multiprocessing
from pathlib import Path
import xml.etree.ElementTree as ET

try:
    from svgelements import Circle, Rect, Ellipse, Polygon, Polyline, Line, Path as SvgPath
except ImportError:
    print("错误: 缺少 'svgelements' 库。请通过运行 'pip install svgelements' 进行安装。")
    sys.exit(1)

# 注册常见的SVG命名空间，防止输出的XML出现自动生成的ns0前缀
ET.register_namespace('', 'http://www.w3.org/2000/svg')
ET.register_namespace('xlink', 'http://www.w3.org/1999/xlink')

def process_single_svg(args):
    """
    处理单个SVG文件：解析XML，寻找基础形状，利用svgelements将其转为Path，再写回XML。
    """
    input_file, output_file = args
    try:
        tree = ET.parse(input_file)
        root = tree.getroot()

        # 定义需要转换的基础形状和它们专属的坐标/尺寸属性
        shape_tags = {
            'circle': ['cx', 'cy', 'r'],
            'rect': ['x', 'y', 'width', 'height', 'rx', 'ry'],
            'ellipse': ['cx', 'cy', 'rx', 'ry'],
            'line': ['x1', 'y1', 'x2', 'y2'],
            'polygon': ['points'],
            'polyline': ['points']
        }

        converted_count = 0

        for elem in root.iter():
            # 剥离命名空间以获取纯净的标签名 (例如 '{http://www.w3.org/2000/svg}rect' -> 'rect')
            tag_clean = elem.tag.split('}')[-1]

            if tag_clean in shape_tags:
                attribs = elem.attrib

                # 提取形状构造所需的参数，忽略诸如 fill, stroke, transform 等通用样式参数
                kwargs = {}
                for k, v in attribs.items():
                    clean_k = k.split('}')[-1]
                    if clean_k in shape_tags[tag_clean]:
                        kwargs[clean_k] = str(v)

                shape_obj = None
                try:
                    # 利用 svgelements 实例化对应的形状
                    if tag_clean == 'circle': shape_obj = Circle(**kwargs)
                    elif tag_clean == 'rect': shape_obj = Rect(**kwargs)
                    elif tag_clean == 'ellipse': shape_obj = Ellipse(**kwargs)
                    elif tag_clean == 'line': shape_obj = Line(**kwargs)
                    elif tag_clean == 'polygon': shape_obj = Polygon(**kwargs)
                    elif tag_clean == 'polyline': shape_obj = Polyline(**kwargs)
                except Exception:
                    # 如果由于某些畸形数据导致svgelements解析失败，则跳过此节点
                    continue

                if shape_obj is not None:
                    # 转换为 Path 对象
                    svg_path = SvgPath(shape_obj)

                    # 替换标签名为 path，同时保留可能存在的原有命名空间前缀
                    ns_prefix = elem.tag[:elem.tag.find('}')+1] if '}' in elem.tag else ''
                    elem.tag = f"{ns_prefix}path"

                    # 删除旧形状的专属属性
                    for attr in shape_tags[tag_clean]:
                        if attr in elem.attrib:
                            del elem.attrib[attr]

                    # 注入计算好的 path 轨道数据 'd'
                    elem.attrib['d'] = svg_path.d()
                    converted_count += 1

        # 确保输出路径所在的父文件夹存在
        output_file.parent.mkdir(parents=True, exist_ok=True)
        # 写回文件，保留原始编码和声明
        tree.write(output_file, encoding='utf-8', xml_declaration=True)

        return True, str(input_file), converted_count, ""

    except Exception as e:
        return False, str(input_file), 0, str(e)


def main():
    parser = argparse.ArgumentParser(description="递归扫描SVG文件夹，多进程将所有基础形状转为Path并保留目录结构。")
    parser.add_argument("-i", "--input", required=True, help="输入文件夹路径 (支持相对或绝对路径)")
    parser.add_argument("-o", "--output", required=True, help="输出文件夹路径 (支持相对或绝对路径)")
    parser.add_argument("-w", "--workers", type=int, default=multiprocessing.cpu_count(), help="并行工作进程数 (默认为CPU核心数)")

    args = parser.parse_args()

    input_dir = Path(args.input).resolve()
    output_dir = Path(args.output).resolve()

    if not input_dir.is_dir():
        print(f"❌ 错误: 输入目录 '{input_dir}' 不存在或不是一个文件夹。")
        sys.exit(1)

    # 递归扫描所有 .svg 文件
    svg_files = list(input_dir.rglob("*.svg"))

    if not svg_files:
        print(f"⚠️ 在 '{input_dir}' 及其子目录中未找到任何 .svg 文件。")
        sys.exit(0)

    print(f"🔍 扫描到 {len(svg_files)} 个 SVG 文件。")
    print(f"🚀 开始并行转换任务，启动了 {args.workers} 个工作进程...\n")

    # 构建任务队列，预先计算好输出路径以保证结构一致
    tasks = []
    for svg_file in svg_files:
        rel_path = svg_file.relative_to(input_dir)
        out_file = output_dir / rel_path
        tasks.append((svg_file, out_file))

    success_count = 0
    fail_count = 0

    # 启用多进程池执行任务
    with multiprocessing.Pool(processes=args.workers) as pool:
        # imap_unordered可以无序但立即地返回已完成的任务，非常适合打印实时进度
        for i, (success, filepath, conv_count, error_msg) in enumerate(pool.imap_unordered(process_single_svg, tasks), 1):
            filename = Path(filepath).name
            progress = f"[{i}/{len(svg_files)}]"

            if success:
                success_count += 1
                if conv_count > 0:
                    print(f"{progress} [成功] {filename} (✅ 转换了 {conv_count} 个形状)")
                else:
                    print(f"{progress} [跳过] {filename} (ℹ️ 没有找到需要转换的形状)")
            else:
                fail_count += 1
                print(f"{progress} [失败] {filename} (❌ 错误: {error_msg})")

    print("\n" + "=" * 50)
    print("🎉 转换任务全部完成！")
    print(f"📊 统计数据: 总计 {len(svg_files)} 个 | 成功 {success_count} 个 | 失败 {fail_count} 个")
    print(f"📂 输出目录: {output_dir}")
    print("=" * 50)


if __name__ == "__main__":
    # Windows系统下的多进程必须在 __main__ 块中运行
    main()