import argparse
import json
import os
import subprocess
import sys
import hashlib
import re
from pathlib import Path
from typing import List, Optional, Tuple, Dict, Any, Union
from concurrent.futures import ProcessPoolExecutor, as_completed
import multiprocessing

# 兼容 Python 3.11+ 的内置 tomllib 和旧版本的 tomli
try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        print("✗ 缺少 TOML 解析库。")
        print("请使用 Python 3.11+，或在较旧版本中执行: pip install tomli")
        sys.exit(1)

# 用于解析 #include 依赖的正则表达式
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

def flatten_options(options: Union[List[str], Dict[str, Any]]) -> List[str]:
    """将混合了纯选项和 KV 的配置展平为 subprocess 可用的列表"""
    result = []
    if isinstance(options, list):
        for opt in options:
            result.append(str(opt))
    elif isinstance(options, dict):
        for k, v in options.items():
            if isinstance(v, bool):
                if v:  # 如果是 True，则加入该选项 (例如 "-O3" = true)
                    result.append(k)
            elif isinstance(v, list):
                # 列表展开 (例如 "-D" = ["MACRO1", "MACRO2"] -> -D MACRO1 -D MACRO2)
                for item in v:
                    if k: result.append(k)
                    result.append(str(item))
            elif v == "" or v is None:
                result.append(k)
            else:
                # 普通 KV对 (例如 "-stage" = "compute")
                if k: result.append(k)
                result.append(str(v))
    return result

def get_file_and_deps_hash(filepath: Path, include_dirs: List[Path], visited: set = None) -> str:
    """递归计算文件及其所有 #include 依赖项的内容哈希值"""
    if visited is None:
        visited = set()

    filepath = filepath.resolve()
    if filepath in visited:
        return ""
    visited.add(filepath)

    if not filepath.is_file():
        return ""

    hasher = hashlib.sha256()
    try:
        content = filepath.read_text(encoding='utf-8', errors='ignore')
        hasher.update(content.encode('utf-8'))

        # 查找并解析 include 依赖
        for match in INCLUDE_RE.finditer(content):
            inc_name = match.group(1)
            inc_path = None

            # 1. 优先检查当前文件同级目录
            test_path = filepath.parent / inc_name
            if test_path.is_file():
                inc_path = test_path
            else:
                # 2. 检查全局 include 目录
                for inc_dir in include_dirs:
                    test_path = inc_dir / inc_name
                    if test_path.is_file():
                        inc_path = test_path
                        break

            if inc_path:
                dep_hash = get_file_and_deps_hash(inc_path, include_dirs, visited)
                hasher.update(dep_hash.encode('utf-8'))

        return hasher.hexdigest()
    except Exception as e:
        print(f"✗ 读取文件计算哈希时出错 {filepath}: {e}")
        return ""

def get_options_hash(options: List[str]) -> str:
    """计算编译选项的哈希值"""
    return hashlib.sha256((" ".join(options)).encode('utf-8')).hexdigest()

def check_slangc_compiler(slangc_path: Union[str, Path]) -> bool:
    try:
        result = subprocess.run([str(slangc_path), "-v"], capture_output=True, text=True, timeout=20)
        if result.returncode == 0:
            print(f"✓ slangc编译器有效: {slangc_path}")
            return True
        else:
            print(f"✗ slangc编译器无效: {result.stderr}")
            return False
    except FileNotFoundError:
        print(f"✗ 找不到slangc编译器: {slangc_path}")
        return False
    except subprocess.TimeoutExpired:
        print("✗ slangc编译器执行超时")
        return False
    except Exception as e:
        print(f"✗ 检查slangc编译器时出错: {e}")
        return False

def check_toml_file(toml_path: Union[str, Path]) -> bool:
    toml_path = Path(toml_path)
    if not toml_path.is_file():
        print(f"✗ TOML配置文件不存在: {toml_path}")
        return False
    try:
        with open(toml_path, 'rb') as f:
            tomllib.load(f)
        print(f"✓ TOML配置文件有效: {toml_path}")
        return True
    except Exception as e:
        print(f"✗ 读取/解析TOML配置文件时出错: {e}")
        return False

def parse_config(toml_path: Union[str, Path]) -> Tuple[Optional[List[str]], Optional[List[Dict[str, Any]]], Optional[str], Optional[List[str]]]:
    try:
        with open(toml_path, 'rb') as f:
            config: Dict[str, Any] = tomllib.load(f)

        if 'common_options' not in config:
            raise ValueError("TOML配置中缺少 'common_options' 字段")
        if 'shaders' not in config:
            raise ValueError("TOML配置中缺少 'shaders' 字段")

        raw_common = config.get('common_options', [])
        common_options = flatten_options(raw_common)

        shaders: List[Dict[str, Any]] = config.get('shaders', [])
        shader_root: str = config.get('shader_root', '')
        include_dirs: List[str] = config.get('include_dir', [])

        for i, shader in enumerate(shaders):
            if 'file' not in shader:
                raise ValueError(f"shaders[{i}] 中缺少 'file' 字段")
            if 'options' not in shader:
                shader['options'] = []
            else:
                shader['options'] = flatten_options(shader['options'])

        print(f"✓ 成功解析配置文件: {len(shaders)} 个着色器文件")
        if shader_root:
            print(f"✓ 使用着色器根目录: {shader_root}")

        return common_options, shaders, shader_root, include_dirs

    except Exception as e:
        print(f"✗ 解析配置文件时出错: {e}")
        return None, None, None, None

def compile_shader(
        slangc_path: str,
        output_file: str,
        shader_alias: str,
        input_file: str,
        full_options: List[str]
) -> Tuple[bool, str]:
    try:
        args: List[str] = [slangc_path]
        args.extend(full_options)
        args.extend(['-o', output_file])
        args.append(input_file)

        print(f"正在编译: {shader_alias} << {Path(input_file).name}")

        result = subprocess.run(args, capture_output=True, text=True, timeout=60)

        if result.returncode == 0:
            return True, result.stdout
        else:
            print(f"\n✗ 编译失败: {shader_alias}")
            print(f"错误信息: {result.stderr}")
            if os.path.exists(output_file):
                os.remove(output_file)
            return False, result.stderr

    except subprocess.TimeoutExpired:
        print(f"\n✗ 编译超时: {shader_alias}")
        if os.path.exists(output_file):
            os.remove(output_file)
        return False, "编译超时"
    except Exception as e:
        print(f"\n✗ 编译过程中发生异常: {e}")
        return False, str(e)

def main() -> None:
    parser = argparse.ArgumentParser(description='SLANG着色器批量编译工具 (增量并行版)')
    parser.add_argument('slangc_path', help='slangc编译器路径')
    parser.add_argument('output_dir', help='输出目录（相对路径）')
    parser.add_argument('config_file', help='配置文件路径（相对路径，TOML格式）')

    default_jobs = multiprocessing.cpu_count()
    parser.add_argument('-j', '--jobs', type=int, default=default_jobs, help=f'允许并行执行的任务数 (默认: {default_jobs})')

    args = parser.parse_args()

    script_dir = Path(__file__).parent.absolute()
    working_dir = Path.cwd()

    slangc_path = Path(args.slangc_path) if Path(args.slangc_path).is_absolute() else script_dir.joinpath(args.slangc_path)
    output_dir = Path(args.output_dir).absolute()
    config_file = Path(args.config_file).absolute()

    print("=" * 60)
    print("SLANG 着色器批量编译工具 (TOML增量并行版)")
    print("=" * 60)
    print(f"编译器路径: {slangc_path}")
    print(f"输出目录: {output_dir}")
    print(f"配置文件: {config_file}")
    print(f"并行度 (-j): {args.jobs}")
    print("-" * 60)

    if not slangc_path.exists():
        print(f"未找到 slangc 路径 {slangc_path}，将尝试使用环境变量中的 slangc")
        slangc_path = Path("slangc")

    if not check_slangc_compiler(slangc_path):
        sys.exit(1)
    if not check_toml_file(config_file):
        sys.exit(1)

    common_options, shaders, shader_root, include_dirs = parse_config(config_file)
    if common_options is None or shaders is None:
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    # 解析 Include 绝对路径列表 (用于依赖追踪和编译参数)
    resolved_includes = []
    if shader_root:
        base_shader_root = config_file.parent.joinpath(shader_root).resolve()
    else:
        base_shader_root = script_dir

    for inc in include_dirs:
        inc_path = base_shader_root.joinpath(inc).resolve()
        resolved_includes.append(inc_path)
        common_options.extend(['-I', str(inc_path)])

    # --- 增量编译：加载缓存 ---
    cache_dir = working_dir / ".slang_build_cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_file = cache_dir / "build_cache.json"

    build_cache = {}
    if cache_file.exists():
        try:
            with open(cache_file, 'r', encoding='utf-8') as f:
                build_cache = json.load(f)
        except Exception:
            print("⚠ 缓存文件损坏，将执行全量编译。")
            build_cache = {}

    new_cache = {}
    success_count = 0
    fail_count = 0
    skip_count = 0

    print("\n开始检查与编译着色器...")
    print("-" * 60)

    tasks_to_run = []

    # 预处理与增量检查
    for shader_config in shaders:
        shader_file: str = shader_config['file']
        shader_options: List[str] = shader_config['options']
        shader_alias: str = shader_config.get('alias', shader_file)

        full_shader_path = base_shader_root.joinpath(shader_file).resolve()
        if not full_shader_path.is_file():
            print(f"✗ 着色器文件不存在: {full_shader_path}")
            fail_count += 1
            continue

        # 生成输出文件路径
        shader_name = Path(shader_alias).with_suffix('.spv')
        shader_name_str = str(shader_name).replace(os.sep, '.').replace('/', '.')
        output_file_path = output_dir / shader_name_str

        # 合并编译参数
        full_options = common_options + shader_options

        # 计算 Hash
        file_hash = get_file_and_deps_hash(full_shader_path, resolved_includes)
        opts_hash = get_options_hash(full_options)

        # 检查是否可以跳过编译
        cached_data = build_cache.get(shader_alias, {})
        if (output_file_path.exists() and
                cached_data.get('file_hash') == file_hash and
                cached_data.get('options_hash') == opts_hash):
            print(f"⏭️ 增量跳过: {shader_alias} (文件与参数未修改)")
            success_count += 1
            skip_count += 1
            new_cache[shader_alias] = cached_data # 保留缓存
        else:
            tasks_to_run.append({
                'alias': shader_alias,
                'input': str(full_shader_path),
                'output': str(output_file_path),
                'options': full_options,
                'file_hash': file_hash,
                'opts_hash': opts_hash
            })

    # 多进程执行实际的编译任务
    if tasks_to_run:
        print(f"\n共 {len(tasks_to_run)} 个任务需要编译，使用 {args.jobs} 个并行进程...")
        with ProcessPoolExecutor(max_workers=args.jobs) as executor:
            futures = {}
            for task in tasks_to_run:
                future = executor.submit(
                    compile_shader,
                    str(slangc_path),
                    task['output'],
                    task['alias'],
                    task['input'],
                    task['options']
                )
                futures[future] = task

            for future in as_completed(futures):
                task = futures[future]
                try:
                    success, message = future.result()
                    if success:
                        success_count += 1
                        # 编译成功，更新新缓存
                        new_cache[task['alias']] = {
                            'file_hash': task['file_hash'],
                            'options_hash': task['opts_hash']
                        }
                    else:
                        fail_count += 1
                except Exception as e:
                    print(f"✗ 运行编译任务 '{task['alias']}' 时发生意外错误: {e}")
                    fail_count += 1

    # 保存新的缓存
    try:
        with open(cache_file, 'w', encoding='utf-8') as f:
            json.dump(new_cache, f, indent=4)
    except Exception as e:
        print(f"⚠ 写入缓存文件失败: {e}")

    # 输出总结
    print("\n" + "=" * 60)
    print("编译总结:")
    print(f"总计处理: {len(shaders)} 个着色器")
    print(f"成功: {success_count} 个 (其中增量跳过 {skip_count} 个)")
    print(f"失败: {fail_count} 个")

    if fail_count > 0:
        print("\n⚠ 注意: 有编译失败的文件，对应的目标文件已被清除。")
        sys.exit(1)
    else:
        print("\n✓ 所有着色器均处理完成!")

if __name__ == "__main__":
    main()