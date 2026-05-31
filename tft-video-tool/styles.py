def get_stylesheet(is_dark_mode):
    if is_dark_mode:
        bg_main = "#1e1e1e"
        bg_input = "#2d2d2d"
        text_col = "#e0e0e0"
        btn_bg = "#333333"
        btn_hover = "#444444"
        accent = "#3498db"
        border = "#333"
        cmd_bg = "#121212"
        cmd_text = "#81c784"
        icon_col = "#ffffff"
        err_bg = "#4a2a2a"
        err_border = "#e74c3c"
        log_bg = "#000000"
        log_text = "#00ff00"
    else:
        bg_main = "#f8f9fa"
        bg_input = "#ffffff"
        text_col = "#212529"
        btn_bg = "#e9ecef"
        btn_hover = "#dee2e6"
        accent = "#0d6efd"
        border = "#ced4da"
        cmd_bg = "#f0f0f0"
        cmd_text = "#333"
        icon_col = "#000000"
        err_bg = "#fff5f5"
        err_border = "#ff6b6b"
        log_bg = "#1e1e1e"
        log_text = "#ffffff"

    return f"""
        QMainWindow {{ background-color: {bg_main}; }}
        QWidget {{ color: {text_col}; font-family: 'Segoe UI', sans-serif; font-size: 13px; }}
        QLabel {{ color: {text_col}; font-weight: bold; }}
        
        QLineEdit {{ 
            background-color: {bg_input}; border: 1px solid {border}; border-radius: 4px; padding: 6px;
        }}
        QLineEdit:focus {{ border: 1px solid {accent}; }}
        QLineEdit[error="true"] {{
            border: 1px solid {err_border}; background-color: {err_bg};
        }}
        
        QComboBox {{ 
            background-color: {bg_input}; border: 1px solid {border}; border-radius: 4px; padding: 4px; 
        }}
        QComboBox::drop-down {{ border: none; }}
        QComboBox QAbstractItemView {{
            background-color: {bg_input}; color: {text_col};
            selection-background-color: {accent}; selection-color: white; border: 1px solid {border};
        }}
        
        QPushButton {{ 
            background-color: {btn_bg}; border: none; border-radius: 4px; 
        }}
        QPushButton:hover {{ background-color: {btn_hover}; }}
        
        QPushButton[text="EXPORT"] {{ 
            background-color: {accent}; color: white; font-weight: bold; letter-spacing: 1px;
        }}
        QPushButton[text="EXPORT"]:hover {{
            background-color: {accent}; opacity: 0.9;
        }}
        QPushButton[text="EXPORT"]:disabled {{
            background-color: {btn_bg}; color: {text_col}; opacity: 0.5;
        }}
        
        QPushButton#btn_open, QPushButton#btn_theme {{
            background-color: {bg_input}; 
            border: 1px solid {border};
            border-radius: 4px;
        }}
        QPushButton#btn_open:hover, QPushButton#btn_theme:hover {{
            border: 1px solid {accent};
        }}

        QPushButton#btn_play {{
            background-color: transparent; border: 1px solid transparent; border-radius: 4px;
        }}
        QPushButton#btn_play:hover {{
            background-color: {btn_hover}; border: 1px solid {border};
        }}

        QSlider::groove:horizontal {{ height: 4px; background: {btn_bg}; border-radius: 2px; }}
        QSlider::handle:horizontal {{ 
            background: {accent}; width: 12px; margin: -4px 0; border-radius: 6px; 
        }}
        
        QTextEdit {{ background-color: {cmd_bg}; color: {cmd_text}; padding: 5px; font-family: Consolas; border: 1px solid {border}; border-radius: 4px; }}
        
        QTextEdit#console_log {{
            background-color: {log_bg}; color: {log_text};
            font-family: 'Consolas', 'Courier New', monospace; border-top: 2px solid {accent};
        }}
    """, icon_col