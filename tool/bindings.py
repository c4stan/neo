import os

BINDINGS = {}

def get(name, default = None):
    error_color = '\033[91m'
    clear_color = '\033[0m'

    global BINDINGS
    if name in BINDINGS:
        return BINDINGS[name]
    else:
        if default is None:
            print(error_color + 'Missing binding: ' + name + clear_color)
        return default

def load(path = 'bindings'):
    global BINDINGS
    BINDINGS = {}
    if os.path.exists(path):
        with open(path) as file:
            for line in file:
                exp = line.split('=')
                name = exp[0].strip()
                value = exp[1].strip()
                BINDINGS[name] = value
    else:
        return False
    return True
