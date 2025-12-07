import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple, Dict, Any, Union
from concurrent.futures import ProcessPoolExecutor, as_completed
import multiprocessing


def check_slangc_compiler(slangc_path: Union[str, Path]) -> bool:
    """检查slangc编译器是否有效

    Args:
        slangc_path: slangc编译器路径

    Returns:
        bool: 编译器是否有效
    """
    try:
        result = subprocess.run(
            [str(slangc_path), "-v"],
            capture_output=True,
            text=True,
            timeout=10
        )
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


def check_json_file(json_path: Union[str, Path]) -> bool:
    """检查JSON配置文件是否存在且格式正确

    Args:
        json_path: JSON配置文件路径

    Returns:
        bool: 配置文件是否有效
    """
    json_path = Path(json_path)
    if not json_path.is_file():
        print(f"✗ JSON配置文件不存在: {json_path}")
        return False

    try:
        with open(json_path, 'r', encoding='utf-8') as f:
            json.load(f)  # 尝试解析JSON
        print(f"✓ JSON配置文件有效: {json_path}")
        return True
    except json.JSONDecodeError as e:
        print(f"✗ JSON配置文件格式错误: {e}")
        return False
    except Exception as e:
        print(f"✗ 读取JSON配置文件时出错: {e}")
        return False


def parse_config(json_path: Union[str, Path]) -> Tuple[Optional[List[str]],
Optional[List[Dict[str, Any]]],
Optional[str],
Optional[List[str]]]:
    """解析JSON配置文件

    Args:
        json_path: JSON配置文件路径

    Returns:
        Tuple: (common_options, shaders, shader_root, include_dirs)
    """
    try:
        with open(json_path, 'r', encoding='utf-8') as f:
            config: Dict[str, Any] = json.load(f)

        # 验证必需字段
        if 'common_options' not in config:
            raise ValueError("JSON配置中缺少 'common_options' 字段")
        if 'shaders' not in config:
            raise ValueError("JSON配置中缺少 'shaders' 字段")

        common_options: List[str] = config.get('common_options', [])
        shaders: List[Dict[str, Any]] = config.get('shaders', [])
        shader_root: str = config.get('shader_root', '')  # 可选字段
        include_dirs: List[str] = config.get('include_dir', [])

        # 验证shaders数组结构
        for i, shader in enumerate(shaders):
            if 'file' not in shader:
                raise ValueError(f"shaders[{i}] 中缺少 'file' 字段")
            if 'options' not in shader:
                shader['options'] = []

        print(f"✓ 成功解析配置文件: {len(shaders)} 个着色器文件")
        if shader_root:
            print(f"✓ 使用着色器根目录: {shader_root}")

        return common_options, shaders, shader_root, include_dirs

    except Exception as e:
        print(f"✗ 解析配置文件时出错: {e}")
        return None, None, None, None


def compile_shader(
        slangc_path: str,
        shader_root: str,
        output_dir: str,
        common_options: List[str],
        shader_alias: str,
        input_file: str,
        shader_options: List[str],
        include_dirs: List[str]
) -> Tuple[bool, str]:
    """编译单个着色器文件

    Args:
        slangc_path: slangc编译器路径
        shader_root: 着色器根目录
        output_dir: 输出目录
        common_options: 通用编译选项
        shader_alias: 着色器别名或文件名，用于生成输出文件
        input_file: 输入文件完整路径
        shader_options: 着色器特定选项
        include_dirs: 包含目录列表

    Returns:
        Tuple[bool, str]: (是否成功, 输出信息或错误信息)
    """
    try:
        # 生成输出文件名
        shader_name = Path(shader_alias).with_suffix('.spv')
        shader_name = str(shader_name).replace(os.sep, '.').replace('/', '.')
        output_file = os.path.join(output_dir, shader_name)

        # 构建编译命令
        args: List[str] = [slangc_path]

        # 添加包含目录
        for include_dir in include_dirs:
            include_path = Path(shader_root).absolute().joinpath(include_dir)
            args.append(f'-I{include_path}\\')

        # 添加编译选项
        args.extend(common_options)
        args.extend(shader_options)
        args.extend(['-o', output_file])
        args.append(input_file)

        print(f"\n正在编译: {shader_alias} << {input_file}")
        print(f"输出文件: {output_file}")
        # 取消打印命令，因为并行时会刷屏
        # print(f"编译命令: {' '.join(args)}")

        # 执行编译
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)

        if result.returncode == 0:
            print(f"✓ 编译成功: {shader_alias}")
            return True, result.stdout
        else:
            print(f"✗ 编译失败: {shader_alias}")
            print(f"错误信息: {result.stderr}")

            # 如果编译失败，删除目标文件
            if os.path.exists(output_file):
                os.remove(output_file)
                print(f"已删除编译失败的目标文件: {output_file}")

            return False, result.stderr

    except subprocess.TimeoutExpired:
        print(f"✗ 编译超时: {shader_alias}")
        # 删除可能部分生成的文件
        output_file = os.path.join(output_dir, f"{Path(shader_alias).stem}.spv")
        if os.path.exists(output_file):
            os.remove(output_file)
        return False, "编译超时"
    except Exception as e:
        print(f"✗ 编译过程中发生异常: {e}")
        return False, str(e)


def main() -> None:
    """主函数"""
    parser = argparse.ArgumentParser(description='SLANG着色器批量编译工具 (并行版)')
    parser.add_argument('slangc_path', help='slangc编译器路径')
    parser.add_argument('output_dir', help='输出目录（相对路径）')
    parser.add_argument('config_file', help='配置文件路径（相对路径）')

    # 新增-j参数，用于指定并行度
    default_jobs = multiprocessing.cpu_count()
    parser.add_argument(
        '-j', '--jobs',
        type=int,
        default=default_jobs,
        help=f'允许并行执行的任务数 (默认: {default_jobs})'
    )

    args = parser.parse_args()

    # 获取当前脚本所在目录的绝对路径
    script_dir = Path(__file__).parent.absolute()

    # 处理相对路径
    slangc_path = Path(args.slangc_path) if Path(args.slangc_path).is_absolute() else script_dir.joinpath(args.slangc_path)
    output_dir = Path(args.output_dir).absolute()
    config_file = Path(args.config_file).absolute()

    print("=" * 50)
    print("SLANG着色器批量编译工具 (并行版)")
    print("=" * 50)
    print(f"编译器路径: {slangc_path}")
    print(f"输出目录: {output_dir}")
    print(f"配置文件: {config_file}")
    print(f"并行度 (-j): {args.jobs}")
    print("-" * 50)

    if not slangc_path.exists():
        print(f"slang c path {slangc_path} not found, using default")
        slangc_path = "slangc"

    # 步骤1: 检查slangc编译器
    if not check_slangc_compiler(slangc_path):
        sys.exit(1)

    # 步骤2: 检查JSON配置文件
    if not check_json_file(config_file):
        sys.exit(1)

    # 步骤3: 解析JSON配置
    common_options, shaders, shader_root, include_dirs = parse_config(config_file)
    if common_options is None or shaders is None:
        sys.exit(1)

    # 创建输出目录
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"✓ 输出目录已创建/确认: {output_dir}")

    # 步骤4: 并行编译所有着色器
    print("\n开始编译着色器...")
    print("-" * 50)

    success_count = 0
    fail_count = 0

    if shader_root:
        shader_root = config_file.parent.joinpath(shader_root).resolve()

    with ProcessPoolExecutor(max_workers=args.jobs) as executor:
        futures = {}
        for shader_config in shaders:
            shader_file: str = shader_config['file']
            shader_options: List[str] = shader_config['options']
            shader_alias: str = shader_config.get('alias', shader_file)

            # 处理着色器文件的路径
            if shader_root:
                full_shader_path = shader_root.joinpath(shader_file).resolve()
            else:
                full_shader_path = script_dir / shader_file

            if not full_shader_path.is_file():
                print(f"✗ 着色器文件不存在: {full_shader_path}")
                fail_count += 1
                continue

            # 提交任务到进程池
            future = executor.submit(
                compile_shader,
                str(slangc_path),
                shader_root,
                str(output_dir),
                common_options,
                shader_alias,
                str(full_shader_path),
                shader_options,
                include_dirs
            )
            futures[future] = shader_alias

        print(f"已提交 {len(futures)} 个编译任务，使用 {args.jobs} 个并行进程...")

        # 获取已完成任务的结果
        for future in as_completed(futures):
            shader_alias = futures[future]
            try:
                success, message = future.result()
                if success:
                    success_count += 1
                else:
                    fail_count += 1
            except Exception as e:
                print(f"✗ 运行编译任务 '{shader_alias}' 时发生意外错误: {e}")
                fail_count += 1

    # 输出总结
    print("\n" + "=" * 50)
    print("编译总结:")
    print(f"成功: {success_count} 个文件")
    print(f"失败: {fail_count} 个文件")
    print(f"总计: {len(shaders)} 个文件")

    if fail_count > 0:
        print("\n注意: 有编译失败的文件，已自动删除对应的.spv目标文件")
        sys.exit(1)
    else:
        print("\n✓ 所有着色器编译完成!")


if __name__ == "__main__":
    main()