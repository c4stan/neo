import renderdoc as rd
from PySide2 import QtWidgets, QtCore
import sys

print("Pixel Debugger script loaded")

class PixelDebuggerExtension:
    def __init__(self, window: QtWidgets.QWidget, controller: rd.ReplayController):
        self.window = window
        self.controller = controller
        self.dialog = None

    def show_dialog(self):
        if self.dialog is None:
            self.dialog = PixelDebuggerDialog(self.controller)
        self.dialog.show()
        self.dialog.raise_()
        self.dialog.activateWindow()


class PixelDebuggerDialog(QtWidgets.QDialog):
    def __init__(self, controller: rd.ReplayController):
        super().__init__()
        self.controller = controller

        # Set up the dialog
        self.setWindowTitle("Compute Shader Pixel Debugger")
        self.setMinimumSize(300, 200)

        # UI Elements
        self.pixel_x_label = QtWidgets.QLabel("Pixel X:")
        self.pixel_x_input = QtWidgets.QSpinBox()
        self.pixel_x_input.setRange(0, 10000)

        self.pixel_y_label = QtWidgets.QLabel("Pixel Y:")
        self.pixel_y_input = QtWidgets.QSpinBox()
        self.pixel_y_input.setRange(0, 10000)

        self.debug_button = QtWidgets.QPushButton("Debug Thread")
        self.debug_button.clicked.connect(self.debug_pixel_thread)

        self.result_label = QtWidgets.QLabel("")

        # Layout
        layout = QtWidgets.QVBoxLayout()
        grid = QtWidgets.QGridLayout()
        grid.addWidget(self.pixel_x_label, 0, 0)
        grid.addWidget(self.pixel_x_input, 0, 1)
        grid.addWidget(self.pixel_y_label, 1, 0)
        grid.addWidget(self.pixel_y_input, 1, 1)
        layout.addLayout(grid)
        layout.addWidget(self.debug_button)
        layout.addWidget(self.result_label)
        self.setLayout(layout)

    def debug_pixel_thread(self):
        pixel_x = self.pixel_x_input.value()
        pixel_y = self.pixel_y_input.value()

        # Perform debugging
        pipe: rd.PipeState = self.controller.GetPipelineState()
        compute_shader = pipe.GetShader(rd.ShaderStage.Compute)
        if compute_shader == rd.ResourceId.Null():
            self.result_label.setText("No compute shader bound to the current event.")
            return

        thread_group_size = compute_shader.GetDispatchThreadsPerGroup()
        thread_group_x = pixel_x // thread_group_size[0]
        thread_group_y = pixel_y // thread_group_size[1]
        local_thread_x = pixel_x % thread_group_size[0]
        local_thread_y = pixel_y % thread_group_size[1]

        debug_result = self.controller.DebugThread(
            rd.ShaderStage.Compute,
            rd.PixelValue(uint=(thread_group_x, thread_group_y, 0), 
                          uint=(local_thread_x, local_thread_y, 0))
        )

        if debug_result.status != rd.ReplayStatus.Succeeded:
            self.result_label.setText(f"Failed to debug the thread: {debug_result.status}")
            return

        self.result_label.setText(f"Debugging thread group ({thread_group_x}, {thread_group_y}) "
                                  f"local thread ({local_thread_x}, {local_thread_y}).")


def register_panel(window: QtWidgets.QWidget, controller: rd.ReplayController):
    """
    Registers the Pixel Debugger extension as a panel in RenderDoc.
    """
    extension = PixelDebuggerExtension(window, controller)

    # Add to the Extensions menu
    action = QtWidgets.QAction("Pixel Debugger", window)
    action.triggered.connect(extension.show_dialog)
    window.ExtensionsMenu.addAction(action)
