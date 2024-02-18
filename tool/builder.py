import importlib
import traceback
import os
import sys
import platform

import builder_impl as builder

tool_path = os.path.normpath(os.path.abspath(os.path.dirname(__file__)))
root_path = os.path.normpath(os.path.join(tool_path, '..'))
workspace_paths = ['module', 'test', 'app'] 
builder.init(root_path, tool_path, workspace_paths)

args = sys.argv[1:]
if len(args) > 0:
    # parse the args and execute a single command
    string = ''
    for arg in args:
        string += arg + ' '
    builder.parse(string)
else:
    # run the cli
    #if platform.system() == 'Windows':
    #    os.system('cls')
    #else:
    #    os.system('clear')
    builder.print_header()
    builder.print_help()
    while True:
        try:
            string = input('> ')
            if string == 'exit' or string == 'quit':
                quit()
            elif (string == 'reload' or string == 'update'):
                try:
                    importlib.reload(builder)
                    builder.reload(root_path, tool_path, workspace_paths)
                    print('Reload successful.')
                except Exception as e:
                    print('Reload error: ' + str(e))
            else:
                builder.parse(string)
        except EOFError as e:
            pass
        except Exception as e:
            print('Parse error: ' + traceback.format_exc())
