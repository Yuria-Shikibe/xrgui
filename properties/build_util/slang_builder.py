import argparse
import json
import os
import subprocess
import sys
import hashlib
import threading
from pathlib import Path
from typing import List, Optional, Tuple, Dict, Any, Union
from concurrent.futures import ThreadPoolExecutor, as_completed
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

# ==========================================
# 性能优化：全局文件哈希缓存及线程锁
# ==========================================
_FILE_HASH_CACHE: Dict[str, str] = {}
_CACHE_LOCK = threading.Lock()

def flatten_options(options: Union[List[str], Dict[str, Any]]) -> List[str]:
    """将混合了纯选项和 KV 的配置展平为 subprocess 可用的列表"""
    result = []
    if isinstance(options, list):
        for opt in options:
            result.append(str(opt))
    elif isinstance(options, dict):
        for k, v in options.items():
            if isinstance(v, bool):
                if v:
                    result.append(k)
            elif isinstance(v, list):
                for item in v:
                    if k: result.append(k)
                    result.append(str(item))
            elif v == "" or v is None:
                result.append(k)
            else:
                if k: result.append(k)
                result.append(str(v))
    return result

def get_file_hash(filepath: Union[str, Path]) -> str:
    """计算单个文件的 SHA-256 哈希值，并带有线程安全的内存缓存"""
    # 优化：使用 os.path.normpath 和 abspath 替代慢速的 Path.resolve()
    path_str = os.path.normpath(os.path.abspath(filepath))

    # 检查缓存（加锁）
    with _CACHE_LOCK:
        if path_str in _FILE_HASH_CACHE:
            return _FILE_HASH_CACHE[path_str]

    if not os.path.isfile(path_str):
        return ""

    try:
        with open(path_str, 'rb') as f:
            h = hashlib.sha256(f.read()).hexdigest()

        # 写入缓存（加锁）
        with _CACHE_LOCK:
            _FILE_HASH_CACHE[path_str] = h
        return h
    except Exception:
        return ""

def parse_depfile(depfile_path: Path) -> List[str]:
    """解析 slangc 生成的 Makefile 格式的 .d 依赖文件"""
    if not depfile_path.is_file():
        return []
    try:
        content = depfile_path.read_text(encoding='utf-8')
        content = content.replace('\\\n', ' ')

        parts = content.split(':', 1)
        if len(parts) < 2:
            return []

        deps_str = parts[1]
        deps_str = deps_str.replace('\\ ', '\x00')
        tokens = deps_str.split()

        deps = []
        for token in tokens:
            token = token.replace('\x00', ' ')
            # 优化：剥离 Path 对象的创建和 resolve，使用原生 os.path
            p_str = os.path.normpath(os.path.abspath(token))
            if os.path.isfile(p_str):
                deps.append(p_str)

        return sorted(list(set(deps)))
    except Exception as e:
        print(f"⚠ 解析依赖文件失败 {depfile_path}: {e}")
        return []

def get_deps_hash(dep_files: List[str]) -> str:
    """计算一整组依赖文件的联合哈希（大幅优化计算量）"""
    hasher = hashlib.sha256()
    for fpath in sorted(dep_files):
        # 获取依赖项的独立 Hash (绝大部分都会命中内存缓存)
        file_hash = get_file_hash(fpath)
        if not file_hash:
            # 依赖文件丢失，强制返回空值以触发重编
            return ""

        # 只 Hash 字符串签名，而不是把几百MB的原始文件流全塞进去
        hasher.update(file_hash.encode('utf-8'))
    return hasher.hexdigest()

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
        full_options: List[str],
        show_cmds: bool = False
) -> Tuple[bool, str]:
    try:
        args: List[str] = [slangc_path]
        args.extend(full_options)
        args.extend(['-o', output_file])
        args.append(input_file)

        if show_cmds:
            print(f"\n[CMD] {' '.join(args)}")

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

def compile_and_hash_task(slangc_path: str, task: dict, show_cmds: bool) -> Tuple[bool, str, dict, Optional[List[str]], Optional[str]]:
    """子线程工作任务：包含编译自身以及后置的重度 I/O 和 Hash 计算"""
    success, msg = compile_shader(
        slangc_path,
        task['output'],
        task['alias'],
        task['input'],
        task['options'],
        show_cmds
    )

    if not success:
        return False, msg, task, None, None

    # 将重度 I/O 和计算留在子线程执行，避免阻塞主线程合并
    depfile_path = Path(task['depfile'])
    new_deps = parse_depfile(depfile_path)

    if not new_deps:
        new_deps = [os.path.normpath(os.path.abspath(task['input']))]

    deps_hash = get_deps_hash(new_deps)

    return True, msg, task, new_deps, deps_hash

def main() -> None:
    parser = argparse.ArgumentParser(description='SLANG着色器批量编译工具 (极速增量版)')
    parser.add_argument('slangc_path', help='slangc编译器路径')
    parser.add_argument('output_dir', help='输出目录（相对路径）')
    parser.add_argument('config_file', help='配置文件路径（相对路径，TOML格式）')

    default_jobs = multiprocessing.cpu_count()
    parser.add_argument('-j', '--jobs', type=int, default=default_jobs, help=f'允许并行执行的任务数 (默认: {default_jobs})')
    parser.add_argument('--show-cmds', action='store_true', help='输出实际执行的编译命令')

    args = parser.parse_args()

    script_dir = Path(__file__).parent.absolute()
    working_dir = Path.cwd()

    slangc_path = Path(args.slangc_path) if Path(args.slangc_path).is_absolute() else script_dir.joinpath(args.slangc_path)
    output_dir = Path(args.output_dir).absolute()
    config_file = Path(args.config_file).absolute()

    print("=" * 60)
    print("SLANG 着色器批量编译工具 (TOML极速增量验证版 - 多线程优化)")
    print("=" * 60)
    print(f"编译器路径: {slangc_path}")
    print(f"输出目录: {output_dir}")
    print(f"配置文件: {config_file}")
    print(f"期望并行度 (-j): {args.jobs}")
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

    if shader_root:
        base_shader_root = config_file.parent.joinpath(shader_root).resolve()
    else:
        base_shader_root = script_dir

    # 追加搜索目录
    for inc in include_dirs:
        inc_path = base_shader_root.joinpath(inc).resolve()
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

    print("\n开始快速检查着色器依赖树...")
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

        shader_name = Path(shader_alias).with_suffix('.spv')
        shader_name_str = str(shader_name).replace(os.sep, '.').replace('/', '.')
        output_file_path = output_dir / shader_name_str
        dep_file_path = output_dir / f"{shader_name_str}.d"

        # 获取参数 Hash 和入口主文件的独立 Hash
        full_options = common_options + shader_options
        opts_hash = get_options_hash(full_options)
        main_file_hash = get_file_hash(full_shader_path)

        # 检查是否可以跳过编译
        cached_data = build_cache.get(shader_alias, {})
        cached_deps = cached_data.get('deps', [])

        can_skip = False
        if output_file_path.exists() and cached_deps:
            # 此时的 get_deps_hash 会利用内存缓存，速度极快
            current_deps_hash = get_deps_hash(cached_deps)

            if (main_file_hash == cached_data.get('main_file_hash')) and \
                    (current_deps_hash and current_deps_hash == cached_data.get('deps_hash')) and \
                    (opts_hash == cached_data.get('options_hash')):
                can_skip = True

        if can_skip:
            print(f"⏭️ 增量跳过: {shader_alias} (主文件与依赖树未修改)")
            success_count += 1
            skip_count += 1
            new_cache[shader_alias] = cached_data
        else:
            if dep_file_path.exists():
                try:
                    dep_file_path.unlink()
                except Exception:
                    pass

            task_options = full_options + ['-depfile', str(dep_file_path)]

            tasks_to_run.append({
                'alias': shader_alias,
                'input': str(full_shader_path),
                'output': str(output_file_path),
                'depfile': str(dep_file_path),
                'options': task_options,
                'opts_hash': opts_hash,
                'main_file_hash': main_file_hash
            })

    if tasks_to_run:
        actual_jobs = min(args.jobs, len(tasks_to_run))
        print(f"\n共 {len(tasks_to_run)} 个任务需要编译，使用 {actual_jobs} 个并行进程/线程...")

        with ThreadPoolExecutor(max_workers=actual_jobs) as executor:
            futures = {}
            for task in tasks_to_run:
                # 提交全新的包装函数，将后处理任务也放入子线程
                future = executor.submit(
                    compile_and_hash_task,
                    str(slangc_path),
                    task,
                    args.show_cmds
                )
                futures[future] = task

            # 主线程此时只需极速合并结果字典
            for future in as_completed(futures):
                try:
                    success, message, task_info, new_deps, deps_hash = future.result()
                    if success:
                        success_count += 1

                        # 直接写入缓存，零计算成本
                        new_cache[task_info['alias']] = {
                            'main_file_hash': task_info['main_file_hash'],
                            'deps': new_deps,
                            'deps_hash': deps_hash,
                            'options_hash': task_info['opts_hash']
                        }
                    else:
                        fail_count += 1
                except Exception as e:
                    # 获取当前报错的 task，以防在解包未来结果之前就抛出了异常
                    failed_task = futures[future]
                    print(f"✗ 运行编译任务 '{failed_task['alias']}' 时发生意外错误: {e}")
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