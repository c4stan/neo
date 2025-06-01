import importlib
import traceback
import os
import sys
import platform

import commands

def print_header():
    print('****************************************************')
    print('********************  NEO  CLI  ********************')
    print('****************************************************')

tool_path = os.path.normpath(os.path.abspath(os.path.dirname(__file__)))
root_path = os.path.normpath(os.path.join(tool_path, '..'))
workspace_paths = ['module', 'test', 'app'] 

args = sys.argv[1:]
if len(args) > 0:
    # parse the args and execute a single command
    commands.init(root_path, tool_path, workspace_paths)
    string = ''
    for arg in args:
        string += arg + ' '
    commands.execute(string)
else:
    # run the cli
    if platform.system() == 'Windows':
        os.system('cls')
    elif platform.system() == 'Linux':
        os.system('clear')
    print_header()
    commands.print_help()
    commands.init(root_path, tool_path, workspace_paths)
    while True:
        try:
            string = input('> ')
            if string == 'exit' or string == 'quit':
                quit()
            elif (string == 'reload' or string == 'update'):
                try:
                    importlib.reload(commands)
                    commands.reload(root_path, tool_path, workspace_paths)
                    print('Reload successful.')
                except Exception as e:
                    print('Reload error: ' + str(e))
            else:
                commands.execute(string)
        except EOFError as e:
            pass
        except Exception as e:
            print('Parse error: ' + traceback.format_exc())
