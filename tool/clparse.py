import os
import shutil
import signal
import subprocess
import json
import platform
import configparser

import importlib
import makegen
import bindings_reader

if platform.system() == 'Windows':
    import win32gui
    import win32con

class Color:
    OKBLUE  = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL    = '\033[91m'
    ENDC    = '\033[0m'

INDEX = makegen.Index()
BINDINGS = {}
PATH_STACK = [os.getcwd()]
SUBPROCESS = None

def get_index():
    global INDEX
    return INDEX

def get_binding(name):
    global BINDINGS
    return BINDINGS[name]

def push_path(path):
    PATH_STACK.append(path)
    os.chdir(PATH_STACK[-1])

def pop_path():
    PATH_STACK.pop()
    os.chdir(PATH_STACK[-1])

def hook_signal_handler():
    original_sigint = signal.getsignal(signal.SIGINT)
    def sigint_handler(signum, frame):
        global SUBPROCESS
        if SUBPROCESS is not None and SUBPROCESS.poll() is None:
            SUBPROCESS.kill()
            print(Color.OKBLUE + "PID " + str(SUBPROCESS.pid) + " terminated." + Color.ENDC)
            SUBPROCESS = None
        else:
            SUBPROCESS = None
            print(Color.WARNING + "No process to terminate." + Color.ENDC)

    signal.signal(signal.SIGINT, sigint_handler)

def init(root_path, tool_path, workspace_paths):
    global BINDINGS
    global INDEX
    global PATH_STACK
    hook_signal_handler()
    PATH_STACK = []
    push_path(root_path)
    INDEX.init(root_path, tool_path, workspace_paths)
    BINDINGS = bindings_reader.read('tool/bindings')
    if not BINDINGS:
        print(Color.FAIL + 'Bindings file not found' + Color.ENDC)

def reload(root_path, tool_path, workspace_paths):
    importlib.reload(makegen)
    init(root_path, tool_path, workspace_paths)

def print_header():
    print('****************************************************')
    print('********************  NEO  CLI  ********************')
    print('****************************************************')

def print_help():
    print('\t' + Color.OKGREEN + 'help' + Color.ENDC + ' to print this')
    print('\t' + Color.OKGREEN + 'exit' + Color.ENDC + ' to exit')
    print('\t' + Color.OKGREEN + 'list' + Color.ENDC + ' to list workspaces')
    print('\t' + Color.OKGREEN + 'open' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to open a workspace')
    print('\t' + Color.OKGREEN + 'build' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to build a workspace')
    print('\t\t' + Color.OKBLUE + '-v' + Color.ENDC + ' to enable verbose build log output')
    print('\t\t' + Color.OKBLUE + '-asm' + Color.ENDC + ' to output asm')
    print('\t\t' + Color.OKBLUE + '-pp' + Color.ENDC + ' to output the preprocessor result')
    print('\t\t' + Color.OKBLUE + '-o' + Color.ENDC + ' to enable optimization flags')
    print('\t\t' + Color.OKBLUE + '-d' + Color.ENDC + ' to enable permissive warnings')
    print('\t' + Color.OKGREEN + 'makegen' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to run makegen on a workspace')
    print('\t' + Color.OKGREEN + 'clear' + Color.ENDC + ' to clear the console')
    print('\t' + Color.OKGREEN + 'clean' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to clean a workspace')
    print('\t' + Color.OKGREEN + 'run' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to run an app executable')
    print('\t' + Color.OKGREEN + 'debug' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to debug an app executable using Visual Studio')
    print('\t' + Color.OKGREEN + 'debug-fixup' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to fixup the debug env of an app')
    print('\t' + Color.OKGREEN + 'explorer' + Color.OKBLUE + ' <name>' + Color.ENDC + ' to run explorer on a workspace')
    print('\t' + Color.OKGREEN + 'cmd' + Color.OKBLUE + ' <name> <cmd>' + Color.ENDC + ' to run a system cmd on a workspace')
    print('\t' + Color.OKGREEN + 'create' + Color.OKBLUE + ' <root> <name>' + Color.ENDC + ' to create a new workspace under the root')
    print('\t' + Color.OKGREEN + 'gitpush' + Color.OKBLUE + ' <comment>' + Color.ENDC + ' to git push local changes')
    print('\t' + Color.OKGREEN + 'gitpull' + Color.OKBLUE + Color.ENDC + ' to git pull remote changes')
    print('\t' + Color.OKGREEN + 'gitstatus' + Color.ENDC + ' to get the git status')
    print('\t' + Color.OKGREEN + 'title' + Color.OKBLUE + ' <title>' + Color.ENDC + ' to format a code comment title')
    print('\t' + Color.OKGREEN + 'showremote' + Color.OKBLUE + ' <workspace> <file>' + Color.ENDC + ' to show the remote version of the file')
    print('\t' + Color.OKGREEN + 'showstash' + Color.OKBLUE + ' <workspace> <file>' + Color.ENDC + ' to show the latest stash version of the file')
    print('\t' + Color.OKGREEN + 'killeditor' + Color.ENDC + ' to kill the text editor process')
    print('')

def format_title_string(words):
    line = '// ======================================================================================= //'

    title = ''
    for word in words:
        title += word + ' '
    if title != '':
        title = title[:-1]

    text = ''
    text_size = len(title) * 2 - 1
    if title == '':
        text_size = 0
    else:
        for c in title:
            text += c.upper() + ' '
        text = text[:-1]

    tab_size = (len(line) - text_size) / 2 - 2
    tab = '//'
    i = 0
    while i < tab_size:
        tab += ' '
        i += 1

    full_string = line + '\n' + tab + text + '\n' + line
    return full_string

def workspace_is_self(name):
    return name == 'neo' or name == 'self' or name == 'this'

def workspace_is_all(name):
    return name == 'all'

def get_workspace_path(name):
    index = get_index()
    if workspace_is_self(name):
        return index.root_path
    if name == index.tool_name:
        return index.tool_path
    if name in index.workspace_names:
        return index.workspace_names[name]
    if name in index.workspace_map:
        return index.workspace_map[name]
    print(Color.FAIL + 'Unknown workspace ' + name + Color.ENDC)
    return ''

def validate_workspace(name):
    index = get_index()
    if workspace_is_self(name):
        return True
    if workspace_is_all(name):
        return True
    if not name in index.workspace_map and name != index.tool_name and name not in index.workspace_names:
        print(Color.FAIL + name + ' is not a registered workspace.' + Color.ENDC)
        return False
    path = get_workspace_path(name)
    if not os.path.isdir(path):
        print(Color.FAIL + 'Workspace is registered but not found on filesystem at path ' + path + Color.ENDC)
        return False
    return True

def get_workspace_name(name):
    index = get_index()
    if workspace_is_self(name):
        return 'neo'
    if workspace_is_all(name):
        all_name = ''
        for key, path in index.workspace_map.items():
            if all_name != '':
                all_name += ', '
            all_name += key
        return all_name
    return name

def open_workspace(name):
    if not validate_workspace(name):
        return
    if platform.system() == 'Windows':
        found = False
        def enum_window_callback(hwnd, ctx):
            nonlocal found
            title = win32gui.GetWindowText(hwnd)
            target = '(' + get_workspace_name(name) + ') - Sublime Text'
            if target in title:
                found = True
                win32gui.ShowWindow(hwnd, win32con.SW_MAXIMIZE)
                # Nonsense hacky workaround to get SetForegroundWindow to work ???
                # https://stackoverflow.com/questions/14295337/win32gui-setactivewindow-error-the-specified-procedure-could-not-be-found
                import win32com.client
                shell = win32com.client.Dispatch("WScript.Shell")
                shell.SendKeys('%')
                win32gui.SetForegroundWindow(hwnd)
        # Try to bring an already existing window for the workspace to focus
        win32gui.EnumWindows(enum_window_callback, None)
        if found:
            return
    # Create a new sublime window
    # TODO on Linux this causes SIGINT to kill sublime since it's spawned as a subprocess. TODO use subprocess.popen(..., start_new_session=True) instead
    if workspace_is_all(name):
        path = get_index().root_path
    else:
        path = get_workspace_path(name)
    '''
    if workspace_is_all(name):
        all_name = get_workspace_name(name)
        names = all_name.split(',')
        first = True
        for n in names:
            path = get_workspace_path(n.strip())
            if first:
                first = False
                os.system('"' + SUBL + '" ' + path)
            else:
                os.system('"' + SUBL + '" ' + path + ' -a')
    else:'''
    if not os.path.isdir(path):
        print(Color.FAIL + 'Workspace is registered but not found on filesystem at path ' + path + Color.ENDC)
        return
    os.system('"' + get_binding('text_editor') + '" ' + path)
    if platform.system() == 'Windows':
        # Bring the new window to focus
        win32gui.EnumWindows(enum_window_callback, None)

def kill_editor():
    if platform.system() == 'Windows':
        os.system('taskkill /f /im ' + get_binding('text_editor_process'))
    elif platform.system() == 'Linux':
        # TODO
        skip

def makegen_workspace(name, flags):
    build_flags = makegen.BUILD_FLAG_NONE
    if '-v' in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_VERBOSE
    path = get_workspace_path(name)
    push_path(path)
    generator = makegen.Generator(get_index(), name, build_flags)
    generator.load()
    generator.generate()
    pop_path()

def build_workspace(name, flags):
    if not validate_workspace(name):
        return
    # https://www.gnu.org/software/make/manual/html_node/Options-Summary.html
    make_flags = '-O' # If it's a multi thread build, make sure the output is properly formatted. If it's a single thread build this does nothing.
    build_flags = makegen.BUILD_FLAG_NONE
    if not '-s' in flags:
        make_flags += ' -j16' # Multi thread build. 16 concurrent jobs
    if '-v' in flags or '-verbose' in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_VERBOSE
    else:
        make_flags += ' -s' # Disable verbose output. Do not print each make recipe.
    if '-pp' in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_OUTPUT_PP
    if '-asm' in flags:
        if '-pp' in flags:
            print('\n' + Color.FAIL + "-pp and -asm are exclusive, can't have both. Build aborted." + Color.ENDC + '\n')
            return
        build_flags = build_flags | makegen.BUILD_FLAG_OUTPUT_ASM
    if '-o' not in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_OPTIMIZATION_OFF
    if '-w' in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_PERMISSIVE_WARNINGS
    if '-r' in flags:
        build_flags = build_flags | makegen.BUILD_FLAG_PERMISSIVE_WARNINGS
        build_flags = build_flags | makegen.BUILD_FLAG_RELOAD
        #build_flags = build_flags | makegen.BUILD_FLAG_VERBOSE
        #build_flags = build_flags | makegen.BUILD_FLAG_OUTPUT_CHANGES_TO_PIPE
    path = get_workspace_path(name)
    push_path(path)
    generator = makegen.Generator(get_index(), name, build_flags)
    generator.load()
    generator.generate()
    push_path(path + '/' + generator.get_build_path())
    make_target = 'all'
    if '-r' in flags:
        make_target = 'reload'
        generator.alias_target()
    if platform.system() == 'Windows':
        cmd = 'printf Building...\\n && mingw32-make ' + make_flags + ' ' + make_target
    elif platform.system() == 'Linux':
        print('Building...')
        cmd = 'make ' + make_flags + ' ' + make_target
    result = os.system(cmd)
    pop_path()
    pop_path()
    if result == 0:
        changelist = generator.gather_dlls()
        if '-r' in  flags:
            generator.output_build_changes(changelist)
        print('\n' + Color.OKGREEN + 'Build succeded.' + Color.ENDC + '\n')
    else:
        if '-r' in  flags:
            generator.output_build_error()
        print('\n' + Color.FAIL + 'Build failed.' + Color.ENDC + '\n')

def clear_workspace(name):
    result = 0
    workspaces = []
    if workspace_is_all(name):
        index = get_index()
        workspaces = list(index.workspace_map.keys())
    else:
        workspaces = [name]

    for workspace in workspaces:
        path = get_workspace_path(workspace)
        push_path(path)
        deletes = ['output', 'modules', 'build']
        for delete in deletes:
            delete_path = delete
            if os.path.exists(delete_path):
                if platform.system() == 'Windows':
                    cmd = 'rmdir /s /q "' + delete_path + '"'
                elif platform.system() == 'Linux':
                    cmd = 'rm -rf "' + delete_path + '"'
                print('Deleting ' + workspace + '/' + delete_path)
                result = os.system(cmd)
        pop_path()
        if result != 0:
            print('\n' + Color.FAIL + 'Clear failed.' + Color.ENDC + '\n')
            break
    
    if result == 0:
        print('\n' + Color.OKGREEN + 'Clear succeded.' + Color.ENDC + '\n')
'''    
    else:
        if not validate_workspace(name):
            return

        path = get_workspace_path(name)
        push_path(path)
        delete_path = 'output'
        if os.path.exists(delete_path):
            cmd = 'rmdir /s /q "' + delete_path + '"'
            print('Deleting ' + delete_path)
            result = os.system(cmd)
        pop_path()
        if result == 0:
            print('\n' + Color.OKGREEN + 'Clean succeded.' + Color.ENDC + '\n')
        else:
            print('\n' + Color.FAIL + 'Clean failed.' + Color.ENDC + '\n')
'''

def list_workspaces():
    index = get_index()
    for key, list in index.workspace_lists.items():
        print(Color.OKBLUE + key + ':' + Color.ENDC)
        for name in list:
            print('\t' + Color.OKGREEN + name + Color.ENDC)

def explorer_workspace(name):
    if not validate_workspace(name):
        return
    path = get_workspace_path(name)
    if platform.system() == 'Windows':
        path = path.replace('/', '\\')
        os.system('explorer ' + path)
    elif platform.system() == 'Linux' :
        os.system('nautilus ' + path)

def cmd_workspace(name, tokens):
    if not validate_workspace(name):
        return
    cmd = ''
    for token in tokens:
        cmd += ' ' + token
    push_path(get_workspace_path(name))
    os.system(cmd)
    pop_path()

def run_app(name, flags, params):
    if not validate_workspace(name):
        return
    global SUBPROCESS
    if SUBPROCESS is not None and SUBPROCESS.poll() is None:
        print(Color.FAIL + 'Another subprocess is already running. Can\'t run more than one at the same time.' + Color.ENDC)
        return
    path = get_workspace_path(name)
    push_path(path)

    makedef_path = 'makedef'
    makedef = makegen.parse_makedef(makedef_path, None)

    config = 'debug'
    if ('-o' in flags):
        config = 'release'

    env_vars = os.environ.copy()
    if ('-rd' in flags):
        env_vars["ENABLE_VULKAN_RENDERDOC_CAPTURE"] = "1"

    if makedef['output'] == ['app']:
        cmd = './build/' + config + '/output/std_launcher.exe'
        SUBPROCESS = subprocess.Popen([cmd, name], env = env_vars)
    else:
        cmd = './build/' + config + '/output/' + name + '.exe'
        SUBPROCESS = subprocess.Popen([cmd] + params, env = env_vars)

    if ('-a' in flags):
        debug_process()

    pop_path()

def debug_process():
    cmd = 'vsjitdebugger -p ' + str(SUBPROCESS.pid)
    os.system(cmd)

def debug_app(name, flags, params):
    if not validate_workspace(name):
        return
    path = get_workspace_path(name)
    push_path(path)

    makedef_path = makegen.normpath(path + '/' + 'makedef')
    makedef = makegen.parse_makedef(makedef_path, None)

    config = 'debug'
    if ('-o' in flags):
        config = 'release'

    params = ' '.join(params)
    if platform.system() == 'Windows':
        if makedef['output'] == ['app']:
            cmd = "start \"cmd\" \"" + get_binding('devenv') + "\"" + ' /debugexe ' + 'build\\' + config + '\\output\\std_launcher.exe' + ' ' + name + ' ' + params
        else:
            #cmd = 'start ..\\remedybg.exe ' + 'output\\debug\\' + name + '.exe'
            cmd = "start \"cmd\" \"" + get_binding('devenv') + "\"" + ' /debugexe ' + 'build\\' + config + '\\output\\' + name + '.exe' + ' ' + params
    elif platform.system() == 'Linux':
        # TODD try https://github.com/nakst/gf
        cmd = 'code .'
    os.system(cmd)
    pop_path()

def fixup_debug_app(name, flags):
    if not validate_workspace(name):
        print(Color.FAIL + name + ' is not a registered app workspace.' + Color.ENDC)
        return False
    path = get_workspace_path(name)
    push_path(path)

    if platform.system() == 'Linux':
        makedef_path = makegen.normpath(path + '/' + 'makedef')
        makedef = makegen.parse_makedef(makedef_path, None)

        config = 'debug'
        if ('-o' in flags):
            config = 'release'

        if makedef['output'] == ['app']:
            program_path = 'build/' + config + '/output/std_launcher.exe'
            args = name
        else:
            program_path = 'build/' + config + '/output/' + name + '.exe'
            args = ''

        launch_json = '{'\
    '"version": "0.2.0",'\
    '"configurations": ['\
        '{'\
            '"name": "(gdb) Launch",'\
            '"type": "cppdbg",'\
            '"request": "launch",'\
            '"program": "${workspaceRoot}/' + program_path + '",'\
            '"args": ["' + args + '"],'\
            '"stopAtEntry": false,'\
            '"cwd": "${workspaceFolder}",'\
            '"environment": [],'\
            '"externalConsole": false,'\
            '"MIMode": "gdb",'\
            '"setupCommands": ['\
                '{'\
                    '"description": "Enable pretty-printing for gdb",'\
                    '"text": "-enable-pretty-printing",'\
                    '"ignoreFailures": true'\
                '}'\
                '{'\
                    '"description": "In this mode GDB will be attached to both processes after a call to fork() or vfork().",'\
                    '"text": "-gdb-set detach-on-fork true",'\
                    '"ignoreFailures": true'\
              '},'\
              '{'\
                    '"description": "The new process is debugged after a fork. The parent process runs unimpeded.",'\
                    '"text": "-gdb-set follow-fork-mode parent",'\
                    '"ignoreFailures": true'\
              '}'\
            ']'\
        '}'\
    ']'\
'}'
        if not os.path.exists('.vscode'):
            os.system('mkdir .vscode')

        launch_json_file = open('.vscode/launch.json', 'w')
        launch_json_file.write(launch_json)
        launch_json_file.close()

    pop_path()

def create_local_workspace(root, name):
    path = os.path.join(root, name)
    #push_path('../')
    print(path)
    os.system('mkdir ' + path)
    push_path(path)
    os.system('mkdir public')
    os.system('mkdir private')

    makedef_file = open('makedef', 'w')
    makedef_file.write(
        "name = " + name + "\n"\
        "code = public, private\n"\
        "defs = public.def\n"\
        "configs = debug, release\n"\
        "output = dll\n"\
        "deps = std\n"\
    )
    makedef_file.close()

    public_def_file = open('public.def', 'w')
    public_def_file.close()

    gitignore_file = open('.gitignore', 'w')
    gitignore_file.write(
        '*\n'\
        '!.gitignore\n'\
        '!makedef\n'\
        '!*.def\n'\
        '!private/\n'\
        '!private/**\n'\
        '!public/\n'\
        '!public/**\n'\
    )
    gitignore_file.close()

    '''
    index_file = open('index', 'r')
    content = index_file.read()
    parser = configparser.RawConfigParser(allow_no_value=True, delimiters=('\n'))
    parser.read_string(content)
    parser.set('LOCAL', name)
    index_file.close()
    index_file = open('index', 'w')
    parser.write(index_file)
    index_file.close()
    '''

    pop_path()
    #pop_path()

def git_push(tokens):
    comment = ''
    for token in tokens:
        if comment:
            comment += ' '
        comment += token

    if not comment:
        print(Color.FAIL + 'Provide a comment.' + Color.ENDC)
        return

    print(Color.OKBLUE + 'git add .' + Color.ENDC)
    os.system('git add .')
    print(Color.OKBLUE + 'git commit -m "' + comment + '"'  + Color.ENDC)
    os.system('git commit -m "' + comment + '"')
    print(Color.OKBLUE + 'git push'  + Color.ENDC)
    os.system('git push')

def git_pull():
    os.system('git pull')

def git_status():
    os.system('git status')

def show_committed_version(workspace, filename):
    path = get_workspace_path(workspace)
    if platform.system() == 'Windows':
        base = '\\neo\\'
        offset = path.find(base) + len(base)
        path = path[offset:]
        path += '/' + filename
        path = path.replace('\\', '/')
    elif platform.system() == 'Linux':
        base = '/neo/'
        offset = path.find(base) + len(base)
        path = path[offset:]
        path += '/' + filename

    if platform.system() == 'Windows':
        os.system('git show HEAD:' + path + ' | subl.exe -')
    elif platform.system() == 'Linux':
        os.system('git show HEAD:' + path + ' | subl -')

def show_stash_version(workspace, filename):
    path = get_workspace_path(workspace)
    base = '\\neo\\'
    offset = path.find(base) + len(base)
    path = path[offset:]
    if platform.system() == 'Windows':
        path += '/' + filename
        path = path.replace('\\', '/')
    if platform.system() == 'Windows':
        os.system('git show stash@{0}:' + path + ' | subl.exe -')
    elif platform.system() == 'Linux':
        os.system('git show stash@{0}:' + path + ' | subl -')

def make_title(words):
    title = format_title_string(words)
    print(title)
    if platform.system() == 'Windows':
        subprocess.run(['clip.exe'], input=title.strip().encode('ascii'), check=True)
    elif platform.system() == 'Linux':
        p = subprocess.Popen(['xclip', '-selection', 'clipboard'], stdin=subprocess.PIPE)
        p.communicate(input=title.strip().encode('ascii'))

def clear_console():
    if platform.system() == 'Windows':
        os.system('cls')
    elif platform.system() == 'Linux':
        os.system('clear')

def parse(string):
    tokens = string.split(' ')
    cmd = tokens[0]

    if cmd == 'help':
        print_help()
    elif cmd == 'open':
        open_workspace(tokens[1])
    elif cmd == 'list':
        list_workspaces()
    elif cmd == 'build':
        build_workspace(tokens[1], tokens[2:])
    elif cmd == 'clear' and len(tokens) == 1:
        clear_console()
    elif cmd == 'clean' or cmd == 'clear':
        clear_workspace(tokens[1])
    elif cmd == 'explorer':
        explorer_workspace(tokens[1])
    elif cmd == 'cmd':
        cmd_workspace(tokens[1], tokens[2:])
    elif cmd == 'run':
        args = tokens[2:]
        if '--' in args:
            split = args.index('--')
            flags = args[0:split]
            params = args[split + 1:]
        else:
            flags = args[0:]
            params = []
        run_app(tokens[1], flags, params)
    elif cmd == 'debug':
        if len(tokens) == 1:
            debug_process()
        else:
            args = tokens[2:]
            if '--' in args:
                split = args.index('--')
                flags = args[0:split]
                params = args[split + 1:]
            else:
                flags = args[0:]
                params = []
            debug_app(tokens[1], flags, params)
    elif cmd == 'create':
        create_local_workspace(tokens[1], tokens[2])
    elif cmd == 'gitpush':
        git_push(tokens[1:])
    elif cmd == 'gitpull':
        git_pull()
    elif cmd == 'gitstatus':
        git_status()
    elif cmd == 'title':
        make_title(tokens[1:])
    elif cmd == 'makegen':
        makegen_workspace(tokens[1], tokens[2:])
    elif cmd == 'debug-fixup':
        fixup_debug_app(tokens[1], tokens[2:])
    elif cmd == 'showremote':
        show_committed_version(tokens[1], tokens[2])
    elif cmd == 'showstash':
        show_stash_version(tokens[1], tokens[2])
    elif cmd == 'killeditor':
        kill_editor()
    elif cmd == '':
        pass
    else:
        print(Color.FAIL + 'Can\'t do that.' + Color.ENDC)
        print_help()
