import argparse
import os
import sys
import multiprocessing
import subprocess
import shutil
import json
from pathlib import Path

# 缓存配置
CACHE_DIR_NAME = ".svg_normalize_cache"
CACHE_FILE_NAME = "cache.json"

def load_cache(cache_path):
    """加载缓存文件"""
    if cache_path.exists():
        try:
            with open(cache_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception:
            return {}
    return {}

def save_cache(cache_path, cache_data):
    """保存缓存文件"""
    # 确保缓存文件夹存在，且在 Windows 上可以尝试设置隐藏属性 (可选)
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt':
        try:
            subprocess.run(["attrib", "+H", str(cache_path.parent)], check=False)
        except Exception:
            pass

    with open(cache_path, 'w', encoding='utf-8') as f:
        json.dump(cache_data, f, ensure_ascii=False, indent=2)

def process_single_svg(args):
    """
    处理单个SVG文件：调用 Inkscape 无头模式，将所有形状转为 Path，并轮廓化描边。
    """
    input_file, output_file, mtime = args
    try:
        # 确保输出路径所在的父文件夹存在
        output_file.parent.mkdir(parents=True, exist_ok=True)

        # Inkscape 1.x 命令行语法
        cmd = [
            'inkscape',
            str(input_file),
            '--actions=select-all;object-to-path;object-stroke-to-path',
            '--export-plain-svg',
            f'--export-filename={str(output_file)}'
        ]

        # 执行命令，捕获输出以便排错
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)

        return True, str(input_file), "", mtime

    except subprocess.CalledProcessError as e:
        error_info = e.stderr.strip() if e.stderr else "未知转换错误"
        return False, str(input_file), f"Inkscape 处理失败: {error_info}", mtime
    except Exception as e:
        return False, str(input_file), str(e), mtime

def main():
    parser = argparse.ArgumentParser(description="递归扫描SVG文件夹，多进程将所有形状与描边轮廓化为Path (带有增量缓存机制)。")
    parser.add_argument("-i", "--input", required=True, help="输入文件夹路径 (支持相对或绝对路径)")
    parser.add_argument("-o", "--output", required=True, help="输出文件夹路径 (支持相对或绝对路径)")
    parser.add_argument("-w", "--workers", type=int, default=multiprocessing.cpu_count(), help="并行工作进程数 (默认为CPU核心数)")

    args = parser.parse_args()

    input_dir = Path(args.input).resolve()
    output_dir = Path(args.output).resolve()

    if not input_dir.is_dir():
        print(f"❌ 错误: 输入目录 '{input_dir}' 不存在或不是一个文件夹。")
        sys.exit(1)

    # 初始化缓存机制
    cache_dir = Path.cwd() / CACHE_DIR_NAME
    cache_file = cache_dir / CACHE_FILE_NAME
    cache_data = load_cache(cache_file)

    # 递归扫描所有 .svg 文件
    svg_files = list(input_dir.rglob("*.svg"))

    if not svg_files:
        print(f"⚠️ 在 '{input_dir}' 及其子目录中未找到任何 .svg 文件。")
        sys.exit(0)

    print(f"🔍 扫描到 {len(svg_files)} 个 SVG 文件。正在检查文件变动...")

    # 构建任务队列，过滤掉未修改且已存在输出的文件
    tasks = []
    skipped_count = 0

    for svg_file in svg_files:
        rel_path = svg_file.relative_to(input_dir)

        # 将文件名中的空格和 "-" 替换为 "_"
        new_filename = rel_path.name.replace(' ', '_').replace('-', '_')
        # 重新组合输出路径，仅修改文件名部分，保持目录结构不变
        out_file = output_dir / rel_path.with_name(new_filename)

        # 获取当前文件的最后修改时间戳
        current_mtime = svg_file.stat().st_mtime
        file_key = str(svg_file)

        # 检查是否命中缓存：文件没改过，且输出文件确实存在
        if file_key in cache_data and cache_data[file_key] == current_mtime and out_file.exists():
            skipped_count += 1
            continue

        tasks.append((svg_file, out_file, current_mtime))

    if not tasks:
        print(f"✅ 所有 {len(svg_files)} 个文件均未发生变动，且输出文件完整。无需重新转换！")
        sys.exit(0)

    print(f"⏭️  跳过未变动文件: {skipped_count} 个")
    print(f"🚀 开始并行转换剩余的 {len(tasks)} 个任务，启动了 {args.workers} 个工作进程...\n")

    success_count = 0
    fail_count = 0
    cache_updated = False

    # 启用多进程池执行任务
    with multiprocessing.Pool(processes=args.workers) as pool:
        for i, (success, filepath, error_msg, mtime) in enumerate(pool.imap_unordered(process_single_svg, tasks), 1):
            filename = Path(filepath).name
            progress = f"[{i}/{len(tasks)}]"

            if success:
                success_count += 1
                print(f"{progress} [成功] {filename} (✅ 已完全轮廓化)")
                # 任务成功后，更新缓存字典
                cache_data[filepath] = mtime
                cache_updated = True
            else:
                fail_count += 1
                print(f"{progress} [失败] {filename} (❌ 错误: {error_msg})")

    # 如果有成功处理的文件，保存最新的缓存
    if cache_updated:
        save_cache(cache_file, cache_data)
        print("\n💾 缓存已更新。")

    print("\n" + "=" * 50)
    print("🎉 转换任务全部完成！")
    print(f"📊 统计数据: 总计 {len(svg_files)} 个 | 跳过 {skipped_count} 个 | 成功 {success_count} 个 | 失败 {fail_count} 个")
    print(f"📂 输出目录: {output_dir}")
    print("=" * 50)


if __name__ == "__main__":
    main()