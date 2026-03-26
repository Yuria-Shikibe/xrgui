import argparse
import os
import sys
import subprocess
import shutil
import json
import tempfile
from pathlib import Path

# 缓存配置
CACHE_DIR_NAME = ".svg_normalize_cache"
CACHE_FILE_NAME = "cache.json"

# 跨平台处理 npx 命令
NPX_CMD = "npx.cmd" if os.name == "nt" else "npx"

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
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt':
        try:
            subprocess.run(["attrib", "+H", str(cache_path.parent)], check=False)
        except Exception:
            pass

    with open(cache_path, 'w', encoding='utf-8') as f:
        json.dump(cache_data, f, ensure_ascii=False, indent=2)

def check_dependencies():
    """检查是否安装了 Node.js (npx)"""
    try:
        # 在 Windows 上必须调用 npx.cmd
        subprocess.run([NPX_CMD, "--version"], capture_output=True, check=True)
    except FileNotFoundError:
        print(f"❌ 致命错误: 未检测到 Node.js (找不到 {NPX_CMD} 命令)。")
        print("💡 请确保 Node.js 已正确安装，并且已添加到系统的环境变量 PATH 中。")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="增量扫描并使用 oslllo-svg-fixer 完美合并、轮廓化 SVG 形状。")
    parser.add_argument("-i", "--input", required=True, help="输入文件夹路径")
    parser.add_argument("-o", "--output", required=True, help="输出文件夹路径")

    args = parser.parse_args()

    check_dependencies()

    input_dir = Path(args.input).resolve()
    output_dir = Path(args.output).resolve()

    if not input_dir.is_dir():
        print(f"❌ 错误: 输入目录 '{input_dir}' 不存在。")
        sys.exit(1)

    # 初始化缓存
    cache_dir = Path.cwd() / CACHE_DIR_NAME
    cache_file = cache_dir / CACHE_FILE_NAME
    cache_data = load_cache(cache_file)

    svg_files = list(input_dir.rglob("*.svg"))

    if not svg_files:
        print(f"⚠️ 未找到任何 .svg 文件。")
        sys.exit(0)

    print(f"🔍 扫描到 {len(svg_files)} 个 SVG 文件。正在比对缓存...")

    # 记录需要处理的任务
    tasks = []
    skipped_count = 0

    for svg_file in svg_files:
        rel_path = svg_file.relative_to(input_dir)
        new_filename = rel_path.name.replace(' ', '_').replace('-', '_')
        out_file = output_dir / rel_path.with_name(new_filename)

        current_mtime = svg_file.stat().st_mtime
        file_key = str(svg_file)

        # 缓存命中：文件未修改且输出文件存在
        if file_key in cache_data and cache_data[file_key] == current_mtime and out_file.exists():
            skipped_count += 1
            continue

        tasks.append((svg_file, out_file, current_mtime))

    if not tasks:
        print(f"✅ 所有文件均未变动，无需重新转换！")
        sys.exit(0)

    print(f"⏭️  跳过未变动文件: {skipped_count} 个")
    print(f"🚀 开始处理 {len(tasks)} 个变动文件...\n")

    # 创建安全的临时目录
    temp_in = Path(tempfile.mkdtemp(prefix="svg_in_"))
    temp_out = Path(tempfile.mkdtemp(prefix="svg_out_"))
    mapping = {}

    try:
        # 将需要处理的文件拍平复制到临时目录，并记录映射关系
        for i, (svg_file, out_file, mtime) in enumerate(tasks):
            safe_name = f"task_{i}.svg"
            shutil.copy2(svg_file, temp_in / safe_name)
            mapping[safe_name] = (svg_file, out_file, mtime)

        print("⚙️  正在调用浏览器内核解析并合并路径 (首次运行可能需要几秒钟下载依赖)...")

        # 使用平台兼容的 NPX_CMD 变量
        cmd = [NPX_CMD, "--yes", "oslllo-svg-fixer", "-s", str(temp_in), "-d", str(temp_out)]
        subprocess.run(cmd, check=True, text=True, capture_output=True)

        success_count = 0
        fail_count = 0
        cache_updated = False

        # 将处理完的文件移动到真正的输出目录，并更新缓存
        for safe_name, (svg_file, out_file, mtime) in mapping.items():
            processed_file = temp_out / safe_name
            filename = Path(svg_file).name

            if processed_file.exists():
                out_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(processed_file), str(out_file))
                cache_data[str(svg_file)] = mtime
                cache_updated = True
                success_count += 1
                print(f"✅ [成功] {filename} (已合并重叠路径与描边)")
            else:
                fail_count += 1
                print(f"❌ [失败] {filename} (转换引擎未能生成输出)")

        if cache_updated:
            save_cache(cache_file, cache_data)
            print("\n💾 缓存已更新。")

        print("\n" + "=" * 50)
        print("🎉 转换任务全部完成！")
        print(f"📊 统计: 总计 {len(svg_files)} | 跳过 {skipped_count} | 成功 {success_count} | 失败 {fail_count}")
        print("=" * 50)

    except subprocess.CalledProcessError as e:
        print(f"\n❌ 底层转换工具执行失败:\n{e.stderr or e.stdout}")
    except Exception as e:
        print(f"\n❌ 发生意外错误: {str(e)}")
    finally:
        # 无论成功与否，清理临时文件夹
        shutil.rmtree(temp_in, ignore_errors=True)
        shutil.rmtree(temp_out, ignore_errors=True)

if __name__ == "__main__":
    main()