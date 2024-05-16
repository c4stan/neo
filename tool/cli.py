import importlib
import traceback
import os
import sys
import platform

import clparse as parser

tool_path = os.path.normpath(os.path.abspath(os.path.dirname(__file__)))
root_path = os.path.normpath(os.path.join(tool_path, '..'))
workspace_paths = ['module', 'test', 'app'] 

args = sys.argv[1:]
if len(args) > 0:
    # parse the args and execute a single command
    parser.init(root_path, tool_path, workspace_paths)
    string = ''
    for arg in args:
        string += arg + ' '
    parser.parse(string)
else:
    # run the cli
    if platform.system() == 'Windows':
        os.system('cls')
    else:
        os.system('clear')
    parser.print_header()
    parser.print_help()
    parser.init(root_path, tool_path, workspace_paths)
    while True:
        try:
            string = input('> ')
            if string == 'exit' or string == 'quit':
                quit()
            elif (string == 'reload' or string == 'update'):
                try:
                    importlib.reload(parser)
                    parser.reload(root_path, tool_path, workspace_paths)
                    print('Reload successful.')
                except Exception as e:
                    print('Reload error: ' + str(e))
            else:
                parser.parse(string)
        except EOFError as e:
            pass
        except Exception as e:
            print('Parse error: ' + traceback.format_exc())
