import sublime
import sublime_plugin
import subprocess
import os.path
import platform

import bindings_reader

def run(self):

    # TODO read and use bindings
    if platform.system() == 'Windows':
        astyle_path = "astyle.exe"
        options_path = "AStyleOptions"
    elif platform.system() == 'Linux':
        astyle_path = "astyle"
        options_path = "AStyleOptions"

    file_name = self.view.window().active_view().file_name()

    opts = '--options=' + options_path

    cmd = astyle_path + ' ' + opts + ' ' + file_name

    startupinfo = subprocess.STARTUPINFO()
    startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    
    print("AStyle format")
    p = subprocess.Popen(cmd, startupinfo=startupinfo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)
    p.wait()
    if p.stderr is not None:
        stderr = p.stderr.read()
        print(stderr.decode())
    if p.stdout is not None:
        stdout = p.stdout.read()
        print(stdout.decode())

class Astyle(sublime_plugin.TextCommand):
    def run(self, edit):
        run(self)

class EventListener(sublime_plugin.EventListener):
    def on_post_save(self, view):
        file_name = view.window().active_view().file_name()
        _, ext = os.path.splitext(file_name)
        if ext == '.h' or ext == '.c' or ext == '.inl' or ext == '.frag' or ext == '.vert' or ext == '.glsl':
            view.run_command('astyle')
