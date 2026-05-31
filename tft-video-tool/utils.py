import sys
import os
import subprocess

def get_ffmpeg_path():
    """Get path to ffmpeg executable, checking multiple locations"""
    
    # If running as frozen app (PyInstaller)
    if getattr(sys, 'frozen', False):
        base_path = sys._MEIPASS
        ffmpeg_path = os.path.join(base_path, 'bin', 'ffmpeg.exe')
        if os.path.exists(ffmpeg_path):
            return ffmpeg_path
    
    # Check local bin directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    local_bin = os.path.join(script_dir, 'bin')
    
    # Check common ffmpeg locations based on OS
    if sys.platform == "win32":
        # Windows: check in bin folder first, then PATH
        ffmpeg_in_bin = os.path.join(local_bin, 'ffmpeg.exe')
        if os.path.exists(ffmpeg_in_bin):
            return ffmpeg_in_bin
        
        # Check PATH for ffmpeg
        path_result = find_in_path('ffmpeg.exe')
        if path_result:
            return path_result
        
        # Check common installation locations
        common_paths = [
            "C:\\ffmpeg\\bin\\ffmpeg.exe",
            "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
            "C:\\Program Files (x86)\\ffmpeg\\bin\\ffmpeg.exe",
        ]
        for p in common_paths:
            if os.path.exists(p):
                return p
                
    elif sys.platform == "darwin":
        # macOS: check brew path first
        try:
            result = subprocess.run(['brew', '--prefix', 'ffmpeg'], 
                                   capture_output=True, text=True, timeout=10)
            if result.returncode == 0:
                brew_ffmpeg = os.path.join(result.stdout.strip(), 'bin', 'ffmpeg')
                if os.path.exists(brew_ffmpeg):
                    return brew_ffmpeg
        except:
            pass
        
        # Check PATH
        path_result = find_in_path('ffmpeg')
        if path_result:
            return path_result
            
    else:
        # Linux
        path_result = find_in_path('ffmpeg')
        if path_result:
            return path_result
        
        # Check common Linux paths
        common_paths = [
            "/usr/bin/ffmpeg",
            "/usr/local/bin/ffmpeg",
            "/snap/bin/ffmpeg",
        ]
        for p in common_paths:
            if os.path.exists(p):
                return p
    
    return os.path.join(local_bin, 'ffmpeg.exe' if sys.platform == "win32" else 'ffmpeg')

def find_in_path(executable):
    """Search for executable in PATH"""
    if sys.platform == "win32":
        executable = executable.replace('/', '\\')
        path_sep = ';'
    else:
        path_sep = ':'
    
    path = os.environ.get('PATH', '')
    for directory in path.split(path_sep):
        candidate = os.path.join(directory.strip(), executable)
        if os.path.exists(candidate) and os.access(candidate, os.X_OK):
            return candidate
        # Also check without extension on Windows
        if sys.platform == "win32" and not candidate.endswith('.exe'):
            if os.path.exists(candidate + '.exe'):
                return candidate + '.exe'
    return None

def check_ffmpeg_installer():
    """Check if ffmpeg is available, offer installation tips if not"""
    ffmpeg_path = get_ffmpeg_path()
    if os.path.exists(ffmpeg_path):
        try:
            result = subprocess.run([ffmpeg_path, '-version'], 
                                   capture_output=True, timeout=10)
            if result.returncode == 0:
                return True
        except:
            pass
    
    # FFmpeg not found - provide installation guidance
    print("\n" + "="*50)
    print("FFmpeg not found! Please install FFmpeg.")
    print("="*50)
    
    if sys.platform == "win32":
        print("\nTo install FFmpeg on Windows:")
        print("  Option 1: Download from https://ffmpeg.org/download.html")
        print("  Option 2: Run: winget install ffmpeg")
        print("  Option 3: Use Chocolatey: choco install ffmpeg")
        print("\nAfter installation, either:")
        print("  - Add FFmpeg to your PATH")
        print("  - Place ffmpeg.exe in the bin/ folder next to this script")
    elif sys.platform == "darwin":
        print("\nTo install FFmpeg on macOS:")
        print("  - Run: brew install ffmpeg")
    else:
        print("\nTo install FFmpeg on Linux:")
        print("  - Ubuntu/Debian: sudo apt install ffmpeg")
        print("  - Fedora: sudo dnf install ffmpeg")
        print("  - Arch: sudo pacman -S ffmpeg")
    
    print()
    return False

def get_ffmpeg_version():
    """Get installed FFmpeg version string"""
    ffmpeg_path = get_ffmpeg_path()
    if os.path.exists(ffmpeg_path):
        try:
            result = subprocess.run([ffmpeg_path, '-version'], 
                                   capture_output=True, text=True, timeout=10)
            if result.returncode == 0:
                first_line = result.stdout.split('\n')[0]
                return first_line
        except:
            pass
    return "FFmpeg not found"