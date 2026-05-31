from PySide6.QtWidgets import QWidget, QSlider
from PySide6.QtCore import Qt
from PySide6.QtGui import QResizeEvent, QMouseEvent

class ClickableSlider(QSlider):
    def mousePressEvent(self, event: QMouseEvent):
        if event.button() == Qt.LeftButton:
            val = self.minimum() + ((self.maximum() - self.minimum()) * event.position().x()) / self.width()
            val = int(val)
            self.setValue(val)
            self.sliderMoved.emit(val) 
            event.accept()
        super().mousePressEvent(event)

class FixedAspectRatioWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.target_width = 0
        self.target_height = 0
        self.child_widget = None
        self.setStyleSheet("background-color: #000; border-radius: 6px;")
        self.setVisible(False) 

    def set_target_ratio(self, w, h):
        try:
            val_w = float(w)
            val_h = float(h)
            if val_w > 0 and val_h > 0:
                self.target_width = val_w
                self.target_height = val_h
                self.setVisible(True)
                self.update_layout()
                return True
            else:
                self.setVisible(False)
                return False
        except ValueError:
            self.setVisible(False)
            return False

    def set_child_widget(self, widget):
        self.child_widget = widget
        self.child_widget.setParent(self)
        self.child_widget.show()

    def resizeEvent(self, event: QResizeEvent):
        self.update_layout()
        super().resizeEvent(event)

    def update_layout(self):
        if not self.isVisible() or not self.child_widget or self.target_height == 0:
            return
        
        rect = self.geometry()
        available_w = rect.width()
        available_h = rect.height()
        
        if available_w <= 1 or available_h <= 1:
            return

        ratio = self.target_width / self.target_height

        new_w = available_w
        new_h = available_w / ratio

        if new_h > available_h:
            new_h = available_h
            new_w = available_h * ratio

        x = (available_w - new_w) / 2
        y = (available_h - new_h) / 2
        
        self.child_widget.setGeometry(int(x), int(y), int(new_w), int(new_h))