import sys
import os
import shlex
import argparse
import qtawesome as qta
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                               QHBoxLayout, QLineEdit, QPushButton, 
                               QFileDialog, QComboBox, QSlider, QFrame, 
                               QTextEdit, QGridLayout, QSizePolicy,
                               QMessageBox, QLabel, QProgressBar)
from PySide6.QtCore import Qt, QUrl, QTimer, QSize, QProcess, QSettings, Signal, Slot
from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput
from PySide6.QtMultimediaWidgets import QVideoWidget
from PySide6.QtGui import QTextCursor

from widgets import ClickableSlider, FixedAspectRatioWidget
from styles import get_stylesheet
from utils import get_ffmpeg_path, find_ffmpeg, check_ffmpeg_installer

class VideoProcessorApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("TFT Video Tool")
        self.resize(1000, 750)
        self.center_window()
        
        self.settings = QSettings("TFTVideoTool", "App")
        self.is_dark_mode = False
        self.input_path = ""
        self.duration = 0
        self.updating_from_code = False
        self.process = None
        self.was_playing_before_seek = False
        self.output_dir = ""
        self.batch_files = []
        self.batch_index = 0
        self.batch_total = 0
        
        self.setup_ui()
        self.load_settings()
        self.apply_theme()
        self.apply_preset()
        self.init_process()

    def center_window(self):
        frame_geo = self.frameGeometry()
        screen = self.screen().availableGeometry().center()
        frame_geo.moveCenter(screen)
        self.move(frame_geo.topLeft())

    def showEvent(self, event):
        super().showEvent(event)
        QTimer.singleShot(100, self.update_logic)
        
        # Check for ffmpeg on startup
        ffmpeg_path = get_ffmpeg_path()
        if not os.path.exists(ffmpeg_path):
            if not check_ffmpeg_installer():
                self.txt_log.append("Warning: FFmpeg not found!")
                self.txt_log.append("Install FFmpeg to enable video conversion.")
                if sys.platform == "win32":
                    self.txt_log.append("Download: https://ffmpeg.org/download.html")

    def closeEvent(self, event):
        self.save_settings()
        super().closeEvent(event)

    def init_process(self):
        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self.handle_stdout)
        self.process.readyReadStandardError.connect(self.handle_stderr)
        self.process.finished.connect(self.process_finished)

    def setup_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(10)

        header_layout = QHBoxLayout()
        
        self.btn_open = QPushButton("📂")
        self.btn_open.setObjectName("btn_open")
        self.btn_open.setFixedSize(40, 40)
        self.btn_open.setCursor(Qt.PointingHandCursor)
        self.btn_open.clicked.connect(self.open_file)
        
        self.btn_batch = QPushButton("📁")
        self.btn_batch.setObjectName("btn_batch")
        self.btn_batch.setFixedSize(40, 40)
        self.btn_batch.setCursor(Qt.PointingHandCursor)
        self.btn_batch.clicked.connect(self.open_batch)
        self.btn_batch.setToolTip("Batch convert all videos in folder")
        
        self.lbl_file_path = QLineEdit()
        self.lbl_file_path.setPlaceholderText("No video selected... (or batch mode)")
        self.lbl_file_path.setReadOnly(True)
        self.lbl_file_path.setFixedHeight(40)
        
        self.btn_theme = QPushButton()
        self.btn_theme.setObjectName("btn_theme")
        self.btn_theme.setFixedSize(40, 40)
        self.btn_theme.setCursor(Qt.PointingHandCursor)
        self.btn_theme.clicked.connect(self.toggle_theme)

        header_layout.addWidget(self.btn_open)
        header_layout.addWidget(self.btn_batch)
        header_layout.addWidget(self.lbl_file_path)
        header_layout.addWidget(self.btn_theme)
        main_layout.addLayout(header_layout)

        content_layout = QHBoxLayout()
        content_layout.setSpacing(15)

        # Preview section
        preview_container = QWidget()
        preview_layout = QVBoxLayout(preview_container)
        preview_layout.setContentsMargins(0, 0, 0, 0)
        
        self.aspect_ratio_box = FixedAspectRatioWidget()
        self.video_widget = QVideoWidget()
        self.aspect_ratio_box.set_child_widget(self.video_widget)
        
        self.media_player = QMediaPlayer()
        self.audio_output = QAudioOutput()
        self.media_player.setAudioOutput(self.audio_output)
        self.media_player.setVideoOutput(self.video_widget)
        self.media_player.playbackStateChanged.connect(self.media_state_changed)

        self.preview_spacer = QWidget()
        self.preview_spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        preview_layout.addWidget(self.aspect_ratio_box, 1)
        preview_layout.addWidget(self.preview_spacer, 1)

        # Bottom controls
        bottom_controls = QWidget()
        bottom_layout = QVBoxLayout(bottom_controls)
        bottom_layout.setContentsMargins(0, 10, 0, 0)
        bottom_layout.setSpacing(5)

        ctrl_layout = QHBoxLayout()
        
        self.btn_play = QPushButton()
        self.btn_play.setObjectName("btn_play")
        self.btn_play.setFixedSize(40, 30)
        self.btn_play.setFlat(True) 
        self.btn_play.setCursor(Qt.PointingHandCursor)
        self.btn_play.clicked.connect(self.toggle_play)
        
        self.slider_seek = ClickableSlider(Qt.Horizontal)
        self.slider_seek.setFixedHeight(20)
        self.slider_seek.setCursor(Qt.PointingHandCursor)
        self.slider_seek.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.slider_seek.sliderPressed.connect(self.on_slider_pressed)
        self.slider_seek.sliderMoved.connect(self.set_position)
        self.slider_seek.sliderReleased.connect(self.on_slider_released)

        self.lbl_time = QLabel("00:00 / 00:00")
        self.lbl_time.setAlignment(Qt.AlignCenter)
        self.lbl_time.setFixedWidth(100)

        ctrl_layout.addWidget(self.btn_play)
        ctrl_layout.addWidget(self.slider_seek)
        ctrl_layout.addWidget(self.lbl_time)

        self.combo_aspect = QComboBox()
        self.combo_aspect.addItems(["Fit", "Cover", "Stretch"])
        self.combo_aspect.currentIndexChanged.connect(self.change_aspect_ratio_mode)
        self.combo_aspect.currentIndexChanged.connect(self.update_logic)

        bottom_layout.addLayout(ctrl_layout)
        bottom_layout.addWidget(self.combo_aspect)
        preview_layout.addWidget(bottom_controls)

        # Settings section
        settings_container = QWidget()
        settings_layout = QVBoxLayout(settings_container)
        settings_layout.setContentsMargins(0, 0, 0, 0)
        
        grid = QGridLayout()
        grid.setVerticalSpacing(15)
        grid.setHorizontalSpacing(10)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        
        # Presets with 240x320 portrait
        self.combo_presets = QComboBox()
        base_config = {"fps": "30", "q": "10", "vcodec": "mjpeg", "acodec": "mp3", "ar": "44100", "suffix": "_mjpeg.mjpeg"}
        def make_data(w, h):
            d = base_config.copy(); d.update({"w": w, "h": h}); return d

        self.combo_presets.addItem("320x170 (Landscape)", make_data("320", "170"))
        self.combo_presets.addItem("280x240 (Square)", make_data("280", "240"))
        self.combo_presets.addItem("170x320 (Portrait)", make_data("170", "320"))
        self.combo_presets.addItem("240x320 (Portrait 2.8\")", make_data("240", "320"))
        self.combo_presets.addItem("320x480 (Landscape 3.5\")", make_data("320", "480"))
        self.combo_presets.addItem("240x280 (Portrait Small)", make_data("240", "280"))
        self.combo_presets.addItem("Custom", "custom")
        self.combo_presets.currentIndexChanged.connect(self.apply_preset)

        self.combo_strategy = QComboBox()
        self.combo_strategy.addItem("Portrait", "portrait")
        self.combo_strategy.addItem("Landscape", "landscape")
        self.combo_strategy.activated.connect(self.swap_dimensions)

        grid.addWidget(self.combo_presets, 0, 0)
        grid.addWidget(self.combo_strategy, 0, 1)

        self.txt_width = QLineEdit()
        self.txt_width.setPlaceholderText("W")
        self.txt_width.setAlignment(Qt.AlignCenter)
        self.lbl_x = QLabel("x")
        self.lbl_x.setAlignment(Qt.AlignCenter)
        self.lbl_x.setFixedWidth(15)
        self.txt_height = QLineEdit()
        self.txt_height.setPlaceholderText("H")
        self.txt_height.setAlignment(Qt.AlignCenter)

        size_layout = QHBoxLayout()
        size_layout.addWidget(self.txt_width, 1) 
        size_layout.addWidget(self.lbl_x, 0)
        size_layout.addWidget(self.txt_height, 1)
        grid.addLayout(size_layout, 1, 0, 1, 2)

        self.txt_fps = QLineEdit(); self.txt_fps.setPlaceholderText("FPS")
        self.txt_qscale = QLineEdit(); self.txt_qscale.setPlaceholderText("Q (1-31)")
        grid.addWidget(self.txt_fps, 2, 0)
        grid.addWidget(self.txt_qscale, 2, 1)

        # Codec options with more quality presets
        self.combo_vcodec = QComboBox()
        self.combo_vcodec.addItems(["mjpeg", "cinepak"])
        self.combo_vcodec.currentIndexChanged.connect(self.sync_suffix_with_codec)
        self.combo_acodec = QComboBox()
        self.combo_acodec.addItems(["mp3", "pcm", "none"])
        grid.addWidget(self.combo_vcodec, 3, 0)
        grid.addWidget(self.combo_acodec, 3, 1)

        # Quality preset
        self.combo_quality = QComboBox()
        self.combo_quality.addItems(["High Quality (Q=2)", "Medium (Q=10)", "Low (Q=20)", "Custom"])
        self.combo_quality.currentIndexChanged.connect(self.on_quality_changed)
        grid.addWidget(self.combo_quality, 4, 0, 1, 2)

        self.txt_ar = QLineEdit(); self.txt_ar.setPlaceholderText("Hz")
        self.txt_suffix = QLineEdit(); self.txt_suffix.setPlaceholderText("Ext")
        grid.addWidget(self.txt_ar, 5, 0)
        grid.addWidget(self.txt_suffix, 5, 1)

        settings_layout.addLayout(grid)
        
        self.txt_output_name = QLineEdit()
        self.txt_output_name.setPlaceholderText("Output Filename (Optional)")
        self.txt_output_name.textChanged.connect(self.update_logic)
        settings_layout.addWidget(self.txt_output_name)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        self.progress_bar.setFixedHeight(20)
        self.lbl_progress = QLabel("")
        self.lbl_progress.setVisible(False)
        settings_layout.addWidget(self.progress_bar)
        settings_layout.addWidget(self.lbl_progress)
        
        self.txt_command = QTextEdit()
        self.txt_command.setReadOnly(True)
        self.txt_command.setPlaceholderText("FFmpeg command...")
        self.txt_command.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        settings_layout.addWidget(self.txt_command)

        self.btn_export = QPushButton("EXPORT")
        self.btn_export.setFixedHeight(50)
        self.btn_export.setCursor(Qt.PointingHandCursor)
        self.btn_export.clicked.connect(self.run_ffmpeg)
        settings_layout.addWidget(self.btn_export)

        content_layout.addWidget(preview_container, 6)
        content_layout.addWidget(settings_container, 4)
        
        main_layout.addLayout(content_layout)

        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setFixedHeight(120)
        self.txt_log.setFrameShape(QFrame.NoFrame)
        self.txt_log.setObjectName("console_log")
        main_layout.addWidget(self.txt_log)

        self.media_player.positionChanged.connect(self.position_changed)
        self.media_player.durationChanged.connect(self.duration_changed)
        
        widgets_to_validate = [self.txt_width, self.txt_height, self.txt_fps, self.txt_qscale, self.txt_ar, self.txt_suffix]
        for w in widgets_to_validate: w.textChanged.connect(self.validate_inputs)
        
        widgets = [self.txt_width, self.txt_height, self.txt_fps, self.txt_qscale, self.txt_ar, self.txt_suffix, 
                   self.combo_strategy, self.combo_vcodec, self.combo_acodec]
        for w in widgets:
            if isinstance(w, QLineEdit): w.textChanged.connect(self.update_logic)
            elif isinstance(w, QComboBox): w.currentIndexChanged.connect(self.update_logic)

        self.validate_inputs() 
        self.update_logic()

    def on_quality_changed(self, index):
        quality_map = {"2": "2", "10": "10", "20": "20"}
        if index < 3:
            q_values = ["2", "10", "20"]
            self.txt_qscale.setText(q_values[index])
            self.updating_from_code = True
            self.combo_quality.setCurrentIndex(3)  # Switch to custom
            self.updating_from_code = False

    def validate_inputs(self):
        if self.updating_from_code: return
        
        fields = [self.txt_width, self.txt_height, self.txt_fps, self.txt_qscale, self.txt_ar, self.txt_suffix]
        is_valid = True
        for field in fields:
            if not field.text().strip():
                is_valid = False
                field.setProperty("error", True)
            else:
                field.setProperty("error", False)
            field.style().unpolish(field)
            field.style().polish(field)

        if self.process and self.process.state() == QProcess.Running:
             self.btn_export.setEnabled(False)
        else:
            self.btn_export.setEnabled(is_valid)

    def sync_suffix_with_codec(self):
        vcodec = self.combo_vcodec.currentText()
        if vcodec == "mjpeg": self.txt_suffix.setText("_mjpeg.mjpeg")
        elif vcodec == "cinepak": self.txt_suffix.setText("_cinepak.avi")

    def apply_preset(self):
        if self.updating_from_code: return
        data = self.combo_presets.currentData()
        self.updating_from_code = True 
        if isinstance(data, dict):
            w = data.get("w", ""); h = data.get("h", "")
            self.txt_width.setText(w); self.txt_height.setText(h)
            self.txt_fps.setText(data.get("fps", ""))
            self.txt_qscale.setText(data.get("q", ""))
            self.txt_ar.setText(data.get("ar", ""))
            self.txt_suffix.setText(data.get("suffix", ""))
            idx_v = self.combo_vcodec.findText(data.get("vcodec", "")); 
            if idx_v != -1: self.combo_vcodec.setCurrentIndex(idx_v)
            idx_a = self.combo_acodec.findText(data.get("acodec", "")); 
            if idx_a != -1: self.combo_acodec.setCurrentIndex(idx_a)
            try:
                if int(w) >= int(h):
                    idx = self.combo_strategy.findData("landscape")
                    if idx != -1: self.combo_strategy.setCurrentIndex(idx)
                else:
                    idx = self.combo_strategy.findData("portrait")
                    if idx != -1: self.combo_strategy.setCurrentIndex(idx)
            except ValueError: pass
        elif data == "custom":
            self.txt_width.setText(""); self.txt_height.setText("")
            self.txt_fps.setText(""); self.txt_qscale.setText("")
            self.txt_ar.setText(""); self.txt_suffix.setText("")
        self.updating_from_code = False
        self.validate_inputs()
        self.update_logic()

    def sync_preset_from_text(self):
        if self.updating_from_code: return
        w = self.txt_width.text(); h = self.txt_height.text()
        self.updating_from_code = True
        current_data = self.combo_presets.currentData()
        match = False
        if isinstance(current_data, dict):
            if current_data.get("w") == w and current_data.get("h") == h: match = True
        if not match:
            custom_idx = self.combo_presets.findData("custom")
            if custom_idx != -1: self.combo_presets.setCurrentIndex(custom_idx)
        self.updating_from_code = False

    def load_settings(self):
        val = self.settings.value("is_dark_mode", False)
        self.is_dark_mode = (str(val).lower() == 'true')
    
    def save_settings(self):
        self.settings.setValue("is_dark_mode", self.is_dark_mode)

    def toggle_theme(self):
        self.is_dark_mode = not self.is_dark_mode
        self.apply_theme()
        if self.media_player.playbackState() == QMediaPlayer.PlayingState:
            self.update_play_button_icon("pause")
        else:
            self.update_play_button_icon("play")

    def update_play_button_icon(self, state):
        icon_color = "#ffffff" if self.is_dark_mode else "#000000"
        if state == "play": icon = qta.icon('fa5s.play', color=icon_color)
        else: icon = qta.icon('fa5s.pause', color=icon_color)
        self.btn_play.setIcon(icon)
        self.btn_play.setIconSize(QSize(20, 20))

    def media_state_changed(self, state):
        if self.media_player.playbackState() == QMediaPlayer.PlayingState:
            self.update_play_button_icon("pause")
        else:
            self.update_play_button_icon("play")

    def apply_theme(self):
        sheet, icon_col = get_stylesheet(self.is_dark_mode)
        self.setStyleSheet(sheet)
        if self.is_dark_mode: self.btn_theme.setIcon(qta.icon('fa5s.moon', color=icon_col))
        else: self.btn_theme.setIcon(qta.icon('fa5s.sun', color=icon_col))
        self.btn_theme.setIconSize(QSize(20, 20))
        if self.media_player.playbackState() == QMediaPlayer.PlayingState: self.update_play_button_icon("pause")
        else: self.update_play_button_icon("play")

    def swap_dimensions(self):
        w = self.txt_width.text(); h = self.txt_height.text()
        self.txt_width.blockSignals(True); self.txt_height.blockSignals(True)
        self.txt_width.setText(h); self.txt_height.setText(w)
        self.txt_width.blockSignals(False); self.txt_height.blockSignals(False)
        self.sync_preset_from_text()
        self.update_logic()

    def update_logic(self):
        has_dims = self.aspect_ratio_box.set_target_ratio(self.txt_width.text(), self.txt_height.text())
        if has_dims: self.preview_spacer.hide()
        else: self.preview_spacer.show()
        self.update_command()

    def change_aspect_ratio_mode(self, index):
        modes = [Qt.KeepAspectRatio, Qt.KeepAspectRatioByExpanding, Qt.IgnoreAspectRatio]
        self.video_widget.setAspectRatioMode(modes[index])
        s = self.video_widget.size()
        self.video_widget.resize(s.width(), s.height()-1)
        self.video_widget.resize(s)

    def open_file(self):
        file_name, _ = QFileDialog.getOpenFileName(self, "Open Video", "", "Video (*.mp4 *.avi *.mkv *.mov *.wmv)")
        if file_name:
            self.input_path = file_name
            self.batch_files = []
            self.lbl_file_path.setText(os.path.basename(file_name))
            self.media_player.setSource(QUrl.fromLocalFile(file_name))
            self.media_player.play()
            self.validate_inputs()
            self.update_logic()

    def open_batch(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Folder with Videos")
        if folder:
            self.batch_files = []
            for f in os.listdir(folder):
                if f.lower().endswith(('.mp4', '.avi', '.mkv', '.mov', '.wmv')):
                    self.batch_files.append(os.path.join(folder, f))
            
            if self.batch_files:
                self.input_path = self.batch_files[0]
                self.batch_index = 0
                self.batch_total = len(self.batch_files)
                self.lbl_file_path.setText(f"Batch: {self.batch_total} videos")
                self.media_player.setSource(QUrl.fromLocalFile(self.input_path))
                self.media_player.play()
                self.validate_inputs()
                self.update_logic()
                self.txt_log.append(f">>> Batch mode: {self.batch_total} videos loaded")
            else:
                QMessageBox.information(self, "No Videos", "No video files found in selected folder.")

    def toggle_play(self):
        if not self.input_path and not self.batch_files: return 
        if self.media_player.playbackState() == QMediaPlayer.PlayingState:
            self.media_player.pause()
        else:
            self.media_player.play()
    
    def play_video(self): 
        self.media_player.play()
    
    def pause_video(self): 
        self.media_player.pause()
    
    def on_slider_pressed(self):
        if not self.input_path: return
        if self.media_player.playbackState() == QMediaPlayer.PlayingState:
            self.was_playing_before_seek = True; self.media_player.pause()
        else: self.was_playing_before_seek = False

    def on_slider_released(self):
        if not self.input_path: return
        if self.was_playing_before_seek:
            self.media_player.play()
        else:
            self.media_player.pause()
            
    def set_position(self, pos): self.media_player.setPosition(pos)
    
    def format_time(self, ms):
        seconds = (ms // 1000) % 60
        minutes = (ms // 60000)
        return f"{minutes:02}:{seconds:02}"

    def update_time_label(self):
        current = self.media_player.position()
        total = self.media_player.duration()
        self.lbl_time.setText(f"{self.format_time(current)} / {self.format_time(total)}")

    def position_changed(self, pos): 
        if not self.slider_seek.isSliderDown(): self.slider_seek.setValue(pos)
        self.update_time_label()

    def duration_changed(self, dur): 
        self.duration = dur; self.slider_seek.setRange(0, dur)
        self.update_time_label()

    def get_command_string(self, specific_output_path=None):
        source = self.input_path if not self.batch_files else self.batch_files[self.batch_index] if self.batch_index < len(self.batch_files) else ""
        if not source: return ""
        
        w_str = self.txt_width.text()
        h_str = self.txt_height.text()
        fps = self.txt_fps.text()
        q = self.txt_qscale.text()
        ar = self.txt_ar.text()
        suffix = self.txt_suffix.text()
        
        if not (w_str and h_str and fps and q and ar and suffix): return ""
        try: 
            w_int = int(w_str)
            h_int = int(h_str)
        except ValueError: return ""

        vcodec = self.combo_vcodec.currentText()
        acodec = self.combo_acodec.currentText()
        ac_cmd = "mp3" if acodec == "mp3" else ("pcm_u8" if acodec == "pcm" else "none")
        aspect_mode = self.combo_aspect.currentText()
        out_w = w_int
        out_h = h_int

        if vcodec == "cinepak":
            if out_w % 4 != 0: out_w = int(round(out_w / 4) * 4)
            if out_h % 4 != 0: out_h = int(round(out_h / 4) * 4)

        inp = f'"{source}"'
        
        if specific_output_path:
            out = f'"{specific_output_path}"'
        else:
            output_name = self.txt_output_name.text().strip()
            out_base = output_name if output_name else os.path.splitext(os.path.basename(source))[0]
            out = f'"{out_base}{suffix}"'

        ffmpeg_exe = get_ffmpeg_path()
        vf_parts = [f"fps={fps}"]
        
        if aspect_mode == "Stretch":
            vf_parts.append(f"scale={out_w}:{out_h}:flags=lanczos")
        elif aspect_mode == "Fit":
            vf_parts.append(f"scale={out_w}:{out_h}:force_original_aspect_ratio=decrease:flags=lanczos")
            vf_parts.append(f"pad={out_w}:{out_h}:(ow-iw)/2:(oh-ih)/2")
        elif aspect_mode == "Cover":
            vf_parts.append(f"scale={out_w}:{out_h}:force_original_aspect_ratio=increase:flags=lanczos")
            vf_parts.append(f"crop={out_w}:{out_h}")
        vf = ",".join(vf_parts)

        if acodec == "none":
            cmd = (f'"{ffmpeg_exe}" -y -i {inp} -c:v {vcodec} -q:v {q} -an -vf "{vf}" {out}')
        else:
            cmd = (f'"{ffmpeg_exe}" -y -i {inp} -ac 2 -ar {ar} -af loudnorm '
                   f'-c:a {ac_cmd} -c:v {vcodec} -q:v {q} -vf "{vf}" {out}')
        return cmd

    def update_command(self):
        cmd = self.get_command_string(specific_output_path=None)
        self.txt_command.setText(cmd)

    def run_ffmpeg(self):
        if not self.btn_export.isEnabled(): return
        
        if self.batch_files and self.batch_index < len(self.batch_files):
            self.run_batch_ffmpeg()
        else:
            self.run_single_ffmpeg()

    def run_single_ffmpeg(self):
        if not self.input_path: return
        ffmpeg_exe = get_ffmpeg_path()
        if not os.path.exists(ffmpeg_exe):
            QMessageBox.critical(self, "Error", f"FFmpeg not found at:\n{ffmpeg_exe}")
            return
        if self.process.state() == QProcess.Running: return

        suffix = self.txt_suffix.text()
        user_name = self.txt_output_name.text().strip()
        
        if user_name:
            if not user_name.endswith(suffix) and not any(user_name.endswith(s) for s in ['.avi', '.mp4', '.mkv']): 
                user_name += suffix
            suggestion = user_name
        else:
            base_name = os.path.splitext(os.path.basename(self.input_path))[0]
            suggestion = f"{base_name}{suffix}"

        initial_dir = os.path.dirname(self.input_path)
        file_path, _ = QFileDialog.getSaveFileName(
            self, 
            "Save Video As",
            os.path.join(initial_dir, suggestion),
            "Video Files (*.avi *.mjpeg *.mp4 *.mkv)"
        )

        if not file_path:
            self.txt_log.append(">>> Export cancelled by user.")
            return

        self.start_ffmpeg_process(file_path)

    def run_batch_ffmpeg(self):
        ffmpeg_exe = get_ffmpeg_path()
        if not os.path.exists(ffmpeg_exe):
            QMessageBox.critical(self, "Error", f"FFmpeg not found at:\n{ffmpeg_exe}")
            return
        if self.process.state() == QProcess.Running: return
        
        self.batch_index = 0
        self.process_next_batch()

    def process_next_batch(self):
        if self.batch_index >= len(self.batch_files):
            self.txt_log.append(f">>> Batch complete! Processed {self.batch_total} files.")
            self.progress_bar.setVisible(False)
            self.lbl_progress.setVisible(False)
            return
        
        input_file = self.batch_files[self.batch_index]
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        suffix = self.txt_suffix.text()
        output_name = f"{base_name}{suffix}"
        output_dir = os.path.dirname(input_file)
        output_path = os.path.join(output_dir, output_name)
        
        self.lbl_file_path.setText(f"[{self.batch_index+1}/{self.batch_total}] {base_name}")
        self.progress_bar.setValue(int(100 * self.batch_index / self.batch_total))
        self.lbl_progress.setText(f"Processing {self.batch_index+1} of {self.batch_total}")
        self.progress_bar.setVisible(True)
        self.lbl_progress.setVisible(True)
        
        self.start_ffmpeg_process(output_path)

    def start_ffmpeg_process(self, output_path):
        self.txt_log.clear()
        self.txt_log.append(f">>> Saving to: {output_path}")
        self.txt_log.append(">>> Starting FFmpeg...")
        
        cmd_str = self.get_command_string(specific_output_path=output_path)
        self.txt_command.setText(cmd_str) 

        args = shlex.split(cmd_str)
        self.btn_export.setEnabled(False)
        self.btn_export.setText("RUNNING...")
        self.process.start(args[0], args[1:])

    def handle_stdout(self):
        data = self.process.readAllStandardOutput()
        text = data.data().decode('utf-8', errors='ignore')
        self.append_log(text)
        
        # Parse progress from ffmpeg output
        if 'time=' in text:
            self.parse_ffmpeg_progress(text)

    def handle_stderr(self):
        data = self.process.readAllStandardError()
        self.append_log(data.data().decode('utf-8', errors='ignore'))

    def parse_ffmpeg_progress(self, text):
        # Extract time= from ffmpeg output like "time=00:00:05.23"
        for line in text.split('\n'):
            if 'time=' in line:
                try:
                    time_str = line.split('time=')[1].split()[0]
                    parts = time_str.split(':')
                    if len(parts) == 3:
                        hours, mins, secs = parts
                        current_time = int(hours) * 3600 + int(mins) * 60 + float(secs)
                        if self.duration > 0:
                            progress = min(int(100 * current_time / (self.duration / 1000)), 99)
                            self.progress_bar.setValue(progress)
                except:
                    pass

    def append_log(self, text):
        self.txt_log.moveCursor(QTextCursor.End); 
        self.txt_log.insertPlainText(text); 
        self.txt_log.moveCursor(QTextCursor.End)

    def process_finished(self):
        self.btn_export.setEnabled(True)
        self.btn_export.setText("EXPORT")
        
        if self.batch_files and self.batch_index < len(self.batch_files):
            self.batch_index += 1
            self.process_next_batch()
        else:
            self.append_log("\n>>> FFmpeg Finished.")
            if self.batch_files:
                self.lbl_file_path.setText(f"Batch: {self.batch_total} videos complete")
                self.progress_bar.setValue(100)

def run_cli(args):
    """Run in CLI mode for headless conversion"""
    ffmpeg_exe = get_ffmpeg_path()
    if not os.path.exists(ffmpeg_exe):
        print(f"Error: FFmpeg not found at {ffmpeg_exe}")
        return 1
    
    if not os.path.exists(args.input):
        print(f"Error: Input file not found: {args.input}")
        return 1
    
    suffix = args.suffix or (".mjpeg" if args.vcodec == "mjpeg" else ".avi")
    
    if args.output:
        output_path = args.output
    else:
        base_name = os.path.splitext(os.path.basename(args.input))[0]
        output_path = os.path.join(os.path.dirname(args.input) or ".", f"{base_name}{suffix}")
    
    fps = args.fps or 30
    q = args.quality or 10
    ar = args.audio_rate or "44100"
    
    out_w = args.width
    out_h = args.height
    
    if args.vcodec == "cinepak":
        if out_w % 4 != 0: out_w = int(round(out_w / 4) * 4)
        if out_h % 4 != 0: out_h = int(round(out_h / 4) * 4)
    
    vf_parts = [f"fps={fps}"]
    
    if args.aspect == "stretch":
        vf_parts.append(f"scale={out_w}:{out_h}:flags=lanczos")
    elif args.aspect == "cover":
        vf_parts.append(f"scale={out_w}:{out_h}:force_original_aspect_ratio=increase:flags=lanczos")
        vf_parts.append(f"crop={out_w}:{out_h}")
    else:  # fit
        vf_parts.append(f"scale={out_w}:{out_h}:force_original_aspect_ratio=decrease:flags=lanczos")
        vf_parts.append(f"pad={out_w}:{out_h}:(ow-iw)/2:(oh-ih)/2")
    
    vf = ",".join(vf_parts)
    
    ac_cmd = "mp3" if args.audio_codec == "mp3" else ("pcm_u8" if args.audio_codec == "pcm" else "none")
    
    if args.audio_codec == "none":
        cmd = f'"{ffmpeg_exe}" -y -i "{args.input}" -c:v {args.vcodec} -q:v {q} -an -vf "{vf}" "{output_path}"'
    else:
        cmd = f'"{ffmpeg_exe}" -y -i "{args.input}" -ac 2 -ar {ar} -af loudnorm -c:a {ac_cmd} -c:v {args.vcodec} -q:v {q} -vf "{vf}" "{output_path}"'
    
    print(f"Converting: {args.input}")
    print(f"Output: {output_path}")
    print(f"Command: {cmd}")
    print()
    
    import subprocess
    result = subprocess.run([ffmpeg_exe] + shlex.split(cmd)[1:], capture_output=False)
    print(f"\nDone! Output: {output_path}")
    return 0

if __name__ == "__main__":
    # Parse CLI arguments
    parser = argparse.ArgumentParser(description="TFT Video Tool - Convert videos for ESP32 display")
    parser.add_argument("--cli", action="store_true", help="Run in CLI mode")
    parser.add_argument("--input", help="Input video file")
    parser.add_argument("--output", help="Output video file")
    parser.add_argument("--width", type=int, default=240, help="Output width")
    parser.add_argument("--height", type=int, default=320, help="Output height")
    parser.add_argument("--fps", type=int, default=30, help="Frames per second")
    parser.add_argument("--quality", type=int, default=10, help="MJPEG quality (1-31)")
    parser.add_argument("--vcodec", default="mjpeg", choices=["mjpeg", "cinepak"], help="Video codec")
    parser.add_argument("--acodec", default="mp3", choices=["mp3", "pcm", "none"], help="Audio codec")
    parser.add_argument("--audio-rate", default="44100", help="Audio sample rate")
    parser.add_argument("--aspect", default="fit", choices=["fit", "cover", "stretch"], help="Aspect ratio mode")
    parser.add_argument("--suffix", help="Output suffix/extension")
    
    args = parser.parse_args()
    
    if args.cli and args.input:
        sys.exit(run_cli(args))
    else:
        app = QApplication(sys.argv)
        window = VideoProcessorApp()
        window.show()
        sys.exit(app.exec())