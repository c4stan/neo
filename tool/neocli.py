import importlib
import traceback
import os
import sys
import platform

import neocli_impl as cli

args = sys.argv[1:]

tool_path = os.path.normpath(os.path.abspath(os.path.dirname(__file__)))
root_path = os.path.normpath(os.path.join(tool_path, '..'))
workspace_paths = ['modules', 'tests', 'apps'] 
cli.init(root_path, tool_path, workspace_paths)

if len(args) > 0:
    # parse args to figure out what needs to be done
    string = ''
    for arg in args:
        string += arg + ' '
    cli.parse(string)
else:
    # run the cli
    if platform.system() == 'Windows':
        os.system('cls')
    else:
        os.system('clear')
    cli.print_header()
    cli.print_help()
    while True:
        try:
            string = input('> ')
            if string == 'exit' or string == 'quit':
                quit()
            elif (string == 'reload' or string == 'update'):
                try:
                    importlib.reload(cli)
                    cli.reload(root_path, tool_path, workspace_paths)
                    print('Reload successful.')
                except Exception as e:
                    print('Reload error: ' + str(e))
            else:
                cli.parse(string)
        except EOFError as e:
            pass
        except Exception as e:
            print('Parse error: ' + traceback.format_exc())
