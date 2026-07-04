"""
dao_core_c.py — C 求值引擎的 Python 绑定

C 引擎在 WSL 中作为独立进程运行，
Python 通过 stdin/stdout IPC 与之通信。

架构：
  Python runtime  ←→  子进程 (dao_core 独立模式)
                          ↕
                        C 求值引擎 (lval_eval)
"""

import subprocess
import os
import threading
import atexit

class DaoCEngine:
    """C 求值引擎的 Python 包装。"""
    
    def __init__(self):
        self._proc = None
        self._lock = threading.Lock()
        self._ready = False
    
    def start(self):
        """启动 C 引擎子进程。"""
        if self._ready:
            return
        
        # 查找 dao_core.so 的位置（与此文件同目录）
        so_dir = os.path.dirname(os.path.abspath(__file__))
        so_path = os.path.join(so_dir, "dao_core.so")
        
        # WSL 路径
        import platform
        # 如果是 WSL 内部，直接用 /mnt/ 路径
        # 如果是 Windows，调用 wsl 命令
        
        c_code_path = os.path.join(so_dir, "dao_core.c")
        wsl_path = self._to_wsl_path(so_dir)
        wsl_c_path = f"{wsl_path}/dao_core.c"
        
        # 编译为独立二进制（如果还没编译）
        build_cmd = [
            "wsl", "-d", "kali-linux", "--",
            "bash", "-c",
            f"cd {wsl_path} && "
            f"gcc -DDAO_STANDALONE -o dao_core_bin dao_core.c -lm -Wall -O2 2>/dev/null"
        ]
        subprocess.run(build_cmd, capture_output=True)
        
        # 启动 REPL 子进程
        proc_cmd = [
            "wsl", "-d", "kali-linux", "--",
            "bash", "-c",
            f"cd {wsl_path} && ./dao_core_bin"
        ]
        
        self._proc = subprocess.Popen(
            proc_cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        
        # 读取启动信息（"Dao Core C Engine v0.1" + "dao> "）
        line = self._proc.stdout.readline()
        if "Dao Core" not in line:
            raise RuntimeError(f"C 引擎启动失败: {line}")
        
        # 读取第一个提示符
        prompt = self._proc.stdout.readline()
        
        self._ready = True
        atexit.register(self.stop)
    
    def _to_wsl_path(self, win_path):
        """Windows 路径转 WSL 路径。"""
        # D:\Tools\Dao\dao → /mnt/d/Tools/Dao/dao
        drive = win_path[0].lower()
        rest = win_path[2:].replace("\\", "/")
        return f"/mnt/{drive}{rest}"
    
    def eval(self, lisp_code: str) -> str:
        """在 C 引擎中求值 Lisp 代码。"""
        if not self._ready:
            self.start()
        
        with self._lock:
            # 发送代码
            self._proc.stdin.write(lisp_code + "\n")
            self._proc.stdin.flush()
            
            # 读取结果（结果行, 然后 "dao> " 提示符）
            result = self._proc.stdout.readline().strip()
            prompt = self._proc.stdout.readline()  # consume "dao> "
            
            return result
    
    def stop(self):
        """停止 C 引擎。"""
        if self._proc and self._ready:
            try:
                self._proc.stdin.write("exit\n")
                self._proc.stdin.flush()
                self._proc.wait(timeout=3)
            except:
                self._proc.kill()
            self._ready = False
    
    def __del__(self):
        self.stop()


# 全局单例
_engine = None

def get_engine() -> DaoCEngine:
    global _engine
    if _engine is None:
        _engine = DaoCEngine()
    return _engine

def eval_lisp(code: str) -> str:
    """快捷函数：在 C 引擎中求值。"""
    return get_engine().eval(code)
