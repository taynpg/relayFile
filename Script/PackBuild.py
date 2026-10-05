#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
relayFile 一键构建 + 打包脚本（整合原 Go 构建脚本 PackVS/PackMinGW/PackMinClang/UnixBuild
与两个打包批处理 cmdBuild.bat / cmdBuild-mingw64.bat 的全部流程，替代 golang 写法）。

Usage:
    python PackBuild.py [--dry-run] [--no-color]

    --dry-run  : 只打印将执行的命令，不改动任何文件
    --no-color : 强制关闭彩色输出（输出被重定向到文件/管道时也会自动关闭）

流程（关键项运行时交互选择）：
    0. 首先询问是否只构建独立的 ServerAlone（无 Qt 依赖）：
         是 -> 选择 dev/release、编译器后仅构建 ServerAlone，随即结束，无任何打包步骤；
         否 -> 进入下面的主工程流程。
    1. 选择工具链：
         Windows : VS(MSVC) / MinGW(msys2 ucrt64) / Clang(msys2 clang64)
         非Windows: Unix（询问是否构建 GUI）
    2. 选择 dev / release（release 额外传 -DRELEASE_MARK=ON，VERSION_DEV 标记不同）
    3. 是否清理已存在的构建目录（对应 Go 的 RemoveBuildDir）
    4. cmake 配置 + 编译（--config Release --parallel；MinGW/Clang 会注入工具链 PATH）
    5. Windows 下继续打包（默认是）：
         windeployqt -> (MinGW/Clang) clangPack -> 拷贝 licenses
         -> 解析 relayFileVersion.h -> makensis 生成安装包
       非 Windows 只构建，跳过 Windows 专有打包步骤。

所有默认路径均可在选择工具链后的提示中直接回车采用，或输入自定义路径覆盖；
也支持环境变量 QT_LIB_ROOT 覆盖 Qt 部署根目录（与原 bat 语义一致）。
"""

import os
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent   # 对应 Go 的 ProjectRoot()/bat 的 %SCRIPT_DIR%..
CONFIG = "Release"              # 与 Go 脚本一致：dev/release 都按 Release 配置编译
SERVER_ALONE_DIR = REPO_ROOT / "ServerAlone"   # 无 Qt 依赖的独立 Server


# ============================ 数据定义 ============================

@dataclass
class Preset:
    key: str
    label: str
    # 构建目录名按 dev/release 分别给出（与 Go 脚本命名保持一致）
    build_dir_dev: str
    build_dir_release: str
    qt_lib_root: str                  # windeployqt 所在根（其下 bin/windeployqt.exe）
    toolchain_bin: str | None = None  # 需要注入到 PATH 的工具链 bin 目录
    cmake_generator: str | None = None
    cmake_extra: list[str] = field(default_factory=list)
    use_clangpack: bool = False       # 打包时是否额外执行 clangPack
    windows: bool = True


def windows_presets() -> dict[str, Preset]:
    return {
        "1": Preset(
            key="1",
            label="VS (MSVC)",
            build_dir_dev="build-dev-msvc",
            build_dir_release="build-release-msvc",
            qt_lib_root=r"C:\Qt\6.10.3\msvc2022_64",
            # VS 生成器由 cmake 自动选择，仅指定 -A x64（同 PackVS.go）
            cmake_extra=["-A", "x64"],
        ),
        "2": Preset(
            key="2",
            label="MinGW (msys2 ucrt64, GCC)",
            build_dir_dev="build-mingw",
            build_dir_release="build-release-mingw",
            qt_lib_root=r"C:\msys64\ucrt64",
            toolchain_bin=r"C:\msys64\ucrt64\bin",
            cmake_generator="MinGW Makefiles",
            cmake_extra=["-DRF_USE_MINGW=ON"],
            use_clangpack=True,
        ),
        "3": Preset(
            key="3",
            label="Clang (msys2 clang64)",
            build_dir_dev="build-dev-clang",
            build_dir_release="build-release-clang",
            qt_lib_root=r"C:\msys64\clang64",
            toolchain_bin=r"C:\msys64\clang64\bin",
            cmake_generator="MinGW Makefiles",
            # 与 PackMinClang.go 一致：不传 RF_USE_MINGW
            use_clangpack=True,
        ),
    }


def unix_preset() -> Preset:
    return Preset(
        key="u",
        label="Unix",
        build_dir_dev="build-dev-unix",
        build_dir_release="build-release-unix",
        qt_lib_root="",
        cmake_generator=None,
        windows=False,
    )


# ============================ 通用工具 ============================

def die(msg: str) -> None:
    print(f"[ERROR] {msg}")
    sys.exit(1)


def ask_choice(prompt: str, options: list[str], default: str) -> str:
    """通用单选，回车取默认值，非法输入重试。options 为可选项列表。"""
    suffix = f"[{'/'.join(options)}，默认 {default}]"
    while True:
        ans = input(f"{prompt} {suffix}: ").strip() or default
        if ans in options:
            return ans
        print("  无效输入，请重新选择。")


def ask_yes_no(prompt: str, default: bool) -> bool:
    d = "Y" if default else "N"
    while True:
        ans = input(f"{prompt} [Y/n，默认 {d}]: ").strip().lower()
        if ans == "":
            return default
        if ans in ("y", "yes"):
            return True
        if ans in ("n", "no"):
            return False
        print("  请输入 y 或 n。")


DRY_RUN = False
# 交互式终端默认保留彩色（ANSI 转义原样透传，Windows Terminal 可正常着色）；
# 输出被重定向到文件/管道时自动关闭，避免日志混入 ESC 裸码。--no-color 可强制关闭。
COLOR = sys.stdout.isatty()


def run(cmd: list[str], what: str, cwd: Path | None = None, env: dict | None = None,
        raw_console: bool = False) -> None:
    """执行外部命令；dry-run 只打印不执行。

    raw_console=True：子进程直接继承当前控制台句柄（用于 clangPack/windeployqt 等
        自行 isatty() 判定彩色的终端工具，管道会让它们误判为非终端而关掉颜色）。
    raw_console=False（默认，用于 cmake/make）：始终经管道实时透传，把进度刷新用的
        裸回车 \\r 规范成 \\n（ANSI 色码原样透传，由最终终端决定是否着色）。
        这样无论宿主终端如何处理子进程直写的 \\r，都不会出现 ◙ 裸字符。"""
    shown = " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd)
    prefix = f"[DRY-RUN] " if DRY_RUN else "    > "
    print(f"{prefix}{shown}" + (f"   (dir={cwd})" if cwd else ""))
    if env:
        only_extra = {k: v for k, v in env.items() if k not in os.environ or os.environ[k] != v}
        for k, v in only_extra.items():
            print(f"         env {k}={v}")
    if DRY_RUN:
        return
    argv = [str(c) for c in cmd]
    cwd_s = str(cwd) if cwd else None

    if raw_console and COLOR:
        # 交互终端：继承控制台句柄，spdlog 等工具可自行检测到真终端而输出彩色
        try:
            code = subprocess.call(argv, cwd=cwd_s, env=env)
            if code != 0:
                die(f"{what} 失败 (exit {code})")
        except FileNotFoundError:
            die(f"{what} 未找到或不在 PATH 中: {cmd[0]}")
        return

    # 管道透传：ANSI 色码原样保留；裸 \r 一律规范为 \n（跨读取块用 pending 拼接，
    # 避免 \r\n 被 256 字节边界切开时误判）。
    proc = None
    pending_cr = False
    try:
        proc = subprocess.Popen(
            argv,
            cwd=cwd_s,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        out = sys.stdout.buffer
        assert proc.stdout is not None
        while True:
            chunk = proc.stdout.read(256)
            if not chunk:
                if pending_cr:
                    out.write(b"\n")
                    pending_cr = False
                break
            if pending_cr:
                # 上一块结尾是 \r：本块以 \n 开头则二者合为一个换行，否则补上一个换行
                chunk = (b"" if chunk.startswith(b"\n") else b"\n") + chunk
                pending_cr = False
            if chunk.endswith(b"\r"):
                # 可能与下一块开头的 \n 成对，先摘出来等下一块
                pending_cr = True
                chunk = chunk[:-1]
            chunk = chunk.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
            if chunk:
                out.write(chunk)
                out.flush()
        code = proc.wait()
        if code != 0:
            die(f"{what} 失败 (exit {code})")
    except FileNotFoundError:
        die(f"{what} 未找到或不在 PATH 中: {cmd[0]}")
    finally:
        if proc and proc.poll() is None:
            proc.kill()


def toolchain_env(toolchain_bin: str | None) -> dict:
    """组装子进程环境：工具链 bin 前置进 PATH（同 Go 的 ContainsPath 语义）。
    彩色策略：COLOR 开启时置 CLICOLOR_FORCE，使遵循 CLICOLOR 约定的工具
    （子进程 stdout 是管道而非 tty）仍然输出 ANSI 颜色；关闭时置 CLICOLOR=0。
    编译器诊断色另外由 -DCMAKE_COLOR_DIAGNOSTICS 强制。"""
    env = dict(os.environ)
    if COLOR:
        env["CLICOLOR_FORCE"] = "1"
        env.pop("CLICOLOR", None)
    else:
        env["CLICOLOR"] = "0"
    if toolchain_bin:
        cur = env.get("PATH", "")
        if toolchain_bin not in cur.split(os.pathsep):
            env["PATH"] = toolchain_bin + os.pathsep + cur
    return env


# ============================ 交互配置 ============================

def choose_preset_windows() -> Preset:
    presets = windows_presets()
    print("请选择构建/打包工具链：")
    print("  [1] VS    (MSVC)                 C:\\Qt\\6.10.3\\msvc2022_64")
    print("  [2] MinGW (msys2 ucrt64, GCC)    C:\\msys64\\ucrt64（windeployqt + clangPack）")
    print("  [3] Clang (msys2 clang64)        C:\\msys64\\clang64（windeployqt + clangPack）")
    key = ask_choice("请选择工具链", ["1", "2", "3"], "1")
    preset = presets[key]

    # 允许覆盖默认路径（回车采用默认）
    qt = input(f"Qt/工具链根目录 [默认 {preset.qt_lib_root}]: ").strip()
    if qt:
        preset.qt_lib_root = qt.rstrip("\\/")
        if preset.toolchain_bin:
            preset.toolchain_bin = str(Path(preset.qt_lib_root) / "bin")
    # 环境变量覆盖优先级最高（保持原 bat 语义）
    env_qt = os.environ.get("QT_LIB_ROOT")
    if env_qt:
        preset.qt_lib_root = env_qt.rstrip("\\/")
        if preset.toolchain_bin:
            preset.toolchain_bin = str(Path(preset.qt_lib_root) / "bin")
        print(f"(QT_LIB_ROOT 环境变量覆盖: {preset.qt_lib_root})")
    return preset


def configure_interactively() -> tuple[Preset, bool, bool, bool, bool]:
    """返回：预设、是否release、是否清理、是否GUI、是否打包。"""
    is_windows = platform.system() == "Windows"
    if is_windows:
        preset = choose_preset_windows()
        use_gui = True   # Windows 三个 Go 脚本均默认构建 GUI（QApplication）
        do_pack = ask_yes_no("构建完成后是否执行打包（windeployqt/clangPack/NSIS）", True)
    else:
        preset = unix_preset()
        print(f"非 Windows 平台（{platform.system()}），使用 Unix 预设，仅构建不执行 Windows 打包。")
        use_gui = ask_yes_no("是否构建 GUI 版本", False)
        do_pack = False

    is_release = ask_yes_no("是否构建 release 版本（否则 dev）", False)
    do_clean = ask_yes_no("是否先删除已存在的构建目录（全新构建）", True)

    print("\n------------------------------------------------------------")
    print(f" 项目根目录 : {REPO_ROOT}")
    print(f" 工具链     : {preset.label}")
    print(f" 构建模式   : {'release' if is_release else 'dev'}")
    print(f" 构建目录   : {(preset.build_dir_release if is_release else preset.build_dir_dev)}")
    print(f" GUI        : {use_gui}")
    print(f" 清理重建   : {do_clean}")
    print(f" 构建后打包 : {do_pack}")
    print("------------------------------------------------------------\n")
    return preset, is_release, do_clean, use_gui, do_pack


# ============================ 构建 ============================

def build(preset: Preset, is_release: bool, do_clean: bool, use_gui: bool) -> Path:
    build_name = preset.build_dir_release if is_release else preset.build_dir_dev
    build_dir = REPO_ROOT / build_name

    if do_clean and build_dir.exists():
        print(f"[build] 删除已有构建目录: {build_dir}")
        if not DRY_RUN:
            shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    # ---- cmake 配置 ----
    cmake_args = ["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir)]
    # 颜色：交互式终端开启（编译器诊断色 + Makefile 的 Building 行着色）
    color_flag = "ON" if COLOR else "OFF"
    cmake_args += [
        f"-DCMAKE_COLOR_DIAGNOSTICS={color_flag}",
        f"-DCMAKE_COLOR_MAKEFILE={color_flag}",
        "-DRF_BUILD_TESTS=OFF",
    ]
    if preset.cmake_generator:
        cmake_args += ["-G", preset.cmake_generator]
    if preset.windows:
        # 单配置生成器（MinGW/Clang）需要显式 CMAKE_BUILD_TYPE；多配置 VS 传了也无害，
        # Go 脚本对 MinGW/Clang 传了 Release，VS 未传——这里仅单配置生成器传。
        if preset.cmake_generator is not None:
            cmake_args += [f"-DCMAKE_BUILD_TYPE={CONFIG}"]
        cmake_args += ["-DQT_DEFAULT_MAJOR_VERSION=6"]
        if use_gui:
            cmake_args += ["-DQAPPLICATION_CLASS=QApplication"]
    else:
        cmake_args += [f"-DCMAKE_BUILD_TYPE={CONFIG}", "-DQT_DEFAULT_MAJOR_VERSION=6"]
        cmake_args += ["-DQAPPLICATION_CLASS=QApplication"] if use_gui else ["-DRF_USE_GUI=OFF"]
        if use_gui:
            cmake_args += ["-DRF_USE_GUI=ON"]

    cmake_args += list(preset.cmake_extra)
    if is_release:
        cmake_args += ["-DRELEASE_MARK=ON"]

    env = toolchain_env(preset.toolchain_bin)
    print("[build] cmake 配置")
    run(cmake_args, "cmake 配置", cwd=REPO_ROOT, env=env)

    # ---- cmake 编译 ----
    build_args = ["cmake", "--build", str(build_dir), "--config", CONFIG, "--parallel"]
    print("[build] cmake 编译")
    run(build_args, "cmake 编译", cwd=REPO_ROOT, env=env)
    return build_dir


# ============================ 独立 Server（ServerAlone，无 Qt） ============================

def choose_server_alone_toolchain() -> tuple[str | None, str | None, str]:
    """返回 (generator, toolchain_bin, label)。
    ServerAlone 无 Qt 依赖，只需选择编译器/生成器；非 Windows 用平台默认。"""
    if platform.system() != "Windows":
        return None, None, f"Unix ({platform.system()}) 默认"
    presets = windows_presets()
    print("请选择 ServerAlone 编译器：")
    print("  [1] VS    (MSVC)")
    print("  [2] MinGW (msys2 ucrt64, GCC)")
    print("  [3] Clang (msys2 clang64)")
    key = ask_choice("请选择编译器", ["1", "2", "3"], "2")  # 独立版一直用 MinGW 验证，默认它
    p = presets[key]
    return p.cmake_generator, p.toolchain_bin, p.label


def build_server_alone() -> None:
    is_release = ask_yes_no("是否构建 release 版本（否则 dev）", False)
    do_clean = ask_yes_no("是否先删除已存在的构建目录（全新构建）", False)
    generator, toolchain_bin, label = choose_server_alone_toolchain()

    build_name = "build-release" if is_release else "build-dev"
    build_dir = SERVER_ALONE_DIR / build_name

    print("\n------------------------------------------------------------")
    print(" 目标       : ServerAlone（独立 Server，无 Qt 依赖，仅构建不打包）")
    print(f" 编译器     : {label}")
    print(f" 构建模式   : {'release' if is_release else 'dev'}")
    print(f" 构建目录   : {build_dir}")
    print(f" 清理重建   : {do_clean}")
    print("------------------------------------------------------------\n")

    if not SERVER_ALONE_DIR.exists():
        die(f"ServerAlone 目录不存在: {SERVER_ALONE_DIR}")
    if do_clean and build_dir.exists():
        print(f"[ServerAlone] 删除已有构建目录: {build_dir}")
        if not DRY_RUN:
            shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    color_flag = "ON" if COLOR else "OFF"
    cmake_args = [
        "cmake", "-S", str(SERVER_ALONE_DIR), "-B", str(build_dir),
        f"-DCMAKE_COLOR_DIAGNOSTICS={color_flag}",
        f"-DCMAKE_COLOR_MAKEFILE={color_flag}",
    ]
    if generator:
        cmake_args += ["-G", generator, f"-DCMAKE_BUILD_TYPE={CONFIG}"]
    if is_release:
        cmake_args += ["-DRELEASE_MARK=ON"]

    env = toolchain_env(toolchain_bin)
    print("[ServerAlone] cmake 配置")
    run(cmake_args, "cmake 配置", cwd=SERVER_ALONE_DIR, env=env)

    build_args = ["cmake", "--build", str(build_dir), "--config", CONFIG, "--parallel"]
    print("[ServerAlone] cmake 编译")
    run(build_args, "cmake 编译", cwd=SERVER_ALONE_DIR, env=env)
    print(f"\nServerAlone 构建完成: {build_dir}")


# ============================ 打包 ============================

def parse_version_h(version_file: Path) -> tuple[str, str]:
    """从 relayFileVersion.h 解析 VERSION_NUM / VERSION_GIT_COMMIT。"""
    text = version_file.read_text(encoding="utf-8", errors="replace")

    def pick(name: str) -> str | None:
        m = re.search(rf"^\s*#\s*define\s+{name}\s+\"?([^\"\s]+)\"?", text, re.MULTILINE)
        return m.group(1) if m else None

    version, commit = pick("VERSION_NUM"), pick("VERSION_GIT_COMMIT")
    if not version:
        die(f"无法从 {version_file} 解析 VERSION_NUM")
    if not commit:
        die(f"无法从 {version_file} 解析 VERSION_GIT_COMMIT")
    return version, commit


def pack(build_dir: Path, preset: Preset, is_release: bool) -> None:
    total = 5 if preset.use_clangpack else 4
    n = 0

    def step(title: str) -> None:
        nonlocal n
        n += 1
        print(f"[pack {n}/{total}] {title}")

    # 1. 递归查找 relayFileGui.exe
    step("查找 relayFileGui.exe")
    if DRY_RUN:
        gui_exe = build_dir / "bin" / CONFIG / "relayFileGui.exe"
        print(f"[DRY-RUN] 假设产物: {gui_exe}")
    else:
        matches = sorted(build_dir.rglob("relayFileGui.exe"))
        if not matches:
            die(f"在以下目录下未找到 relayFileGui.exe: {build_dir}")
        gui_exe = matches[0]
    bin_dir = gui_exe.parent
    print(f"    bin dir: {bin_dir}")

    # 兜底：删除开发用 *Test.exe / *Bench.exe（正常情况下 RF_BUILD_TESTS=OFF 已不会生成）
    stale_dev_exes = []
    if not DRY_RUN:
        for pattern in ("*Test.exe", "*Bench.exe"):
            stale_dev_exes.extend(bin_dir.glob(pattern))
    if DRY_RUN:
        print("[DRY-RUN] 将删除 bin 目录下所有 *Test.exe 与 *Bench.exe")
    for t in stale_dev_exes:
        print(f"    remove: {t.name}")
        t.unlink()

    # 2. windeployqt
    step("windeployqt 部署 Qt 运行库")
    windeployqt = Path(preset.qt_lib_root) / "bin" / "windeployqt.exe"
    if not DRY_RUN and not windeployqt.is_file():
        die(f"windeployqt 不存在: {windeployqt}\n[HINT] 检查 Qt/工具链根目录（当前: {preset.qt_lib_root}）")
    run([windeployqt, gui_exe], "windeployqt", raw_console=True)

    # 3. clangPack（仅 MinGW/Clang）
    if preset.use_clangpack:
        step("clangPack 收集工具链运行依赖")
        run(["clangPack", "-e", gui_exe, "-r", "-c", str(Path(preset.qt_lib_root) / "bin")],
            "clangPack", raw_console=True)

    # 4. licenses
    step("拷贝 licenses")
    licenses_dir = REPO_ROOT / "licenses"
    dst_licenses = bin_dir / "licenses"
    if licenses_dir.is_dir():
        print(f"    copy licenses -> {dst_licenses}")
        if not DRY_RUN:
            shutil.copytree(licenses_dir, dst_licenses, dirs_exist_ok=True)
    else:
        print(f"[WARN] licenses 目录不存在，已跳过: {licenses_dir}")
    license_file = REPO_ROOT / "LICENSE"
    if license_file.is_file() and not DRY_RUN:
        dst_licenses.mkdir(parents=True, exist_ok=True)
        shutil.copy2(license_file, dst_licenses / "LICENSE")

    # 5. 版本 + makensis
    step("解析版本并生成安装包")
    version_file = build_dir / "relayFileVersion.h"
    if not DRY_RUN and not version_file.is_file():
        die(f"版本文件不存在: {version_file}")
    if DRY_RUN:
        version, commit = "0.0", "dryrun0"
    else:
        version, commit = parse_version_h(version_file)
    print(f"    version: {version}  mark: {'release' if is_release else 'dev'}  commit: {commit}")

    build_mark = "release" if is_release else "dev"
    nsis_exe = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "NSIS" / "makensis.exe"
    if not DRY_RUN and not nsis_exe.is_file():
        die(f"makensis 不存在: {nsis_exe}")
    nsi_file = SCRIPT_DIR / "relayFile.nsi"
    run(
        [
            nsis_exe,
            f"/DSRC_BIN_DIR={bin_dir}",
            f"/DOUT_DIR={build_dir}",
            f"/DPRODUCT_VERSION={version}",
            f"/DBUILD_MARK={build_mark}",
            f"/DCOMMIT_ID={commit}",
            nsi_file,
        ],
        "makensis",
        raw_console=True,
    )
    setup_name = f"relayFile-setup-x64-v{version}-{build_mark}-{commit}.exe"
    print(f"Done: {build_dir / setup_name}")


# ============================ 入口 ============================

def main() -> None:
    global DRY_RUN, COLOR
    DRY_RUN = "--dry-run" in sys.argv
    if "--no-color" in sys.argv:
        COLOR = False

    # 最前置分支：独立 Server（ServerAlone，无 Qt 依赖）——只构建，不做任何后续操作
    if ask_yes_no("是否只构建独立的 ServerAlone（无 Qt 依赖，构建后即结束）", False):
        build_server_alone()
        print("\n全部完成。")
        return

    preset, is_release, do_clean, use_gui, do_pack = configure_interactively()
    build_dir = build(preset, is_release, do_clean, use_gui)

    if do_pack and preset.windows:
        pack(build_dir, preset, is_release)
    elif do_pack and not preset.windows:
        print("[pack] 非 Windows 平台，跳过 windeployqt/NSIS 打包步骤。")
    print("\n全部完成。")


if __name__ == "__main__":
    main()
